//
// TD-private iOS Bonjour advertisement and browsing skeleton.
//
// This only exposes the Bonjour control plane. It deliberately does not create
// connections, inspect endpoints, or interact with TD's UDP/IPX networking.
//

#include "bonjour_discovery.h"

#include "common/debugstring.h"

#include <Network/Network.h>
#include <dispatch/dispatch.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <net/if.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>
#include <mutex>

namespace {

constexpr const char* kBonjourServiceType = "_vctd._tcp";
constexpr size_t kControlRecordSize = 12;
constexpr unsigned short kLegacyUdpPort = 1234;

dispatch_queue_t Bonjour_Queue = nullptr;
nw_listener_t Bonjour_Listener = nullptr;
nw_browser_t Bonjour_Browser = nullptr;
size_t Bonjour_Browse_Result_Count = 0;
std::mutex Bonjour_Endpoint_Mutex;
unsigned char Bonjour_Pending_Endpoint[4] = {};
bool Bonjour_Has_Pending_Endpoint = false;
std::mutex Bonjour_Control_Mutex;
nw_connection_t Bonjour_Browser_Control = nullptr;
bool Bonjour_Browser_Control_Active = false;

bool Is_Usable_IPv4(const unsigned char ipv4[4])
{
    if (ipv4[0] == 0 || ipv4[0] == 127 || ipv4[0] >= 224) {
        return false;
    }
    if (ipv4[0] == 192 && ipv4[1] == 0 && ipv4[2] == 0) {
        return false;
    }
    return !(ipv4[0] == 255 && ipv4[1] == 255 && ipv4[2] == 255 && ipv4[3] == 255);
}

int Private_LAN_Priority(const unsigned char ipv4[4])
{
    if (ipv4[0] == 192 && ipv4[1] == 168) {
        return 0;
    }
    if (ipv4[0] == 10) {
        return 1;
    }
    if (ipv4[0] == 172 && ipv4[1] >= 16 && ipv4[1] <= 31) {
        return 2;
    }
    return -1;
}

bool Is_Private_LAN_IPv4(const unsigned char ipv4[4])
{
    return Is_Usable_IPv4(ipv4) && Private_LAN_Priority(ipv4) >= 0;
}

bool Select_Host_LAN_IPv4(unsigned char ipv4[4])
{
    struct ifaddrs* interfaces = nullptr;
    if (getifaddrs(&interfaces) != 0) {
        return false;
    }

    bool found = false;
    for (int priority = 0; priority <= 2 && !found; ++priority) {
        for (struct ifaddrs* entry = interfaces; entry != nullptr; entry = entry->ifa_next) {
            if (entry->ifa_addr == nullptr || entry->ifa_addr->sa_family != AF_INET
                || (entry->ifa_flags & IFF_UP) == 0 || (entry->ifa_flags & IFF_LOOPBACK) != 0) {
                continue;
            }

            const sockaddr_in* address = reinterpret_cast<const sockaddr_in*>(entry->ifa_addr);
            const unsigned char* candidate = reinterpret_cast<const unsigned char*>(&address->sin_addr);
            if (Private_LAN_Priority(candidate) == priority && Is_Private_LAN_IPv4(candidate)) {
                memcpy(ipv4, candidate, 4);
                found = true;
                break;
            }
        }
    }

    freeifaddrs(interfaces);
    return found;
}

bool Select_Path_LAN_IPv4(nw_connection_t connection, unsigned char ipv4[4])
{
    nw_path_t path = nw_connection_copy_current_path(connection);
    nw_endpoint_t endpoint = path != nullptr ? nw_path_copy_effective_local_endpoint(path) : nullptr;
    const sockaddr* address = endpoint != nullptr ? nw_endpoint_get_address(endpoint) : nullptr;
    const sockaddr_in* candidate =
        address != nullptr && address->sa_family == AF_INET ? reinterpret_cast<const sockaddr_in*>(address) : nullptr;
    bool found = candidate != nullptr && Is_Private_LAN_IPv4(reinterpret_cast<const unsigned char*>(&candidate->sin_addr));
    if (found) {
        memcpy(ipv4, &candidate->sin_addr, 4);
    }
    if (endpoint != nullptr) {
        nw_release(endpoint);
    }
    if (path != nullptr) {
        nw_release(path);
    }
    return found;
}

void Queue_Pending_Endpoint(const unsigned char ipv4[4])
{
    std::lock_guard<std::mutex> lock(Bonjour_Endpoint_Mutex);
    memcpy(Bonjour_Pending_Endpoint, ipv4, sizeof(Bonjour_Pending_Endpoint));
    Bonjour_Has_Pending_Endpoint = true;
}

void Finish_Browser_Control(nw_connection_t connection)
{
    bool owns_connection = false;
    {
        std::lock_guard<std::mutex> lock(Bonjour_Control_Mutex);
        if (Bonjour_Browser_Control == connection) {
            Bonjour_Browser_Control = nullptr;
            Bonjour_Browser_Control_Active = false;
            owns_connection = true;
        }
    }

    if (owns_connection) {
        nw_connection_cancel(connection);
        nw_release(connection);
    }
}

void Cancel_Browser_Control()
{
    nw_connection_t connection = nullptr;
    {
        std::lock_guard<std::mutex> lock(Bonjour_Control_Mutex);
        connection = Bonjour_Browser_Control;
        Bonjour_Browser_Control = nullptr;
        Bonjour_Browser_Control_Active = false;
    }

    if (connection != nullptr) {
        nw_connection_cancel(connection);
        nw_release(connection);
    }
}

void Send_Host_Control_Record(nw_connection_t connection, void (^finish)(void))
{
    unsigned char ipv4[4];
    if (!Select_Host_LAN_IPv4(ipv4) && !Select_Path_LAN_IPv4(connection, ipv4)) {
        DBG_LOG("BONJOUR_DIAG no suitable host LAN IPv4");
        DBG_LOG("BONJOUR_DIAG host control record rejected");
        finish();
        return;
    }
    DBG_LOG("BONJOUR_DIAG host LAN IPv4 selected");

    unsigned char* record = static_cast<unsigned char*>(malloc(kControlRecordSize));
    if (record == nullptr) {
        DBG_LOG("BONJOUR_DIAG host control record rejected");
        finish();
        return;
    }

    record[0] = 'V';
    record[1] = 'C';
    record[2] = 'T';
    record[3] = 'D';
    record[4] = 1;
    record[5] = 0;
    memcpy(record + 6, ipv4, sizeof(ipv4));
    record[10] = static_cast<unsigned char>(kLegacyUdpPort >> 8);
    record[11] = static_cast<unsigned char>(kLegacyUdpPort & 0xff);

    dispatch_data_t payload =
        dispatch_data_create(record, kControlRecordSize, Bonjour_Queue, DISPATCH_DATA_DESTRUCTOR_FREE);
    if (payload == nullptr) {
        free(record);
        DBG_LOG("BONJOUR_DIAG host control record rejected");
        finish();
        return;
    }
    nw_connection_send(connection, payload, NW_CONNECTION_DEFAULT_MESSAGE_CONTEXT, true, ^(nw_error_t error) {
        if (error != nullptr) {
            DBG_LOG("BONJOUR_DIAG host control record failed: domain=%d error=%d", nw_error_get_error_domain(error),
                    nw_error_get_error_code(error));
        } else {
            DBG_LOG("BONJOUR_DIAG host control record sent");
        }
        finish();
    });
}

void Start_Browser_Control(nw_browse_result_t result)
{
    {
        std::lock_guard<std::mutex> lock(Bonjour_Control_Mutex);
        if (Bonjour_Browser_Control_Active) {
            return;
        }
        Bonjour_Browser_Control_Active = true;
    }

    nw_endpoint_t endpoint = nw_browse_result_copy_endpoint(result);
    nw_parameters_t parameters = nw_parameters_create_secure_tcp(NW_PARAMETERS_DISABLE_PROTOCOL,
                                                                   NW_PARAMETERS_DEFAULT_CONFIGURATION);
    if (endpoint == nullptr || parameters == nullptr) {
        if (parameters != nullptr) {
            nw_release(parameters);
        }
        if (endpoint != nullptr) {
            nw_release(endpoint);
        }
        std::lock_guard<std::mutex> lock(Bonjour_Control_Mutex);
        Bonjour_Browser_Control_Active = false;
        DBG_LOG("BONJOUR_DIAG browser control rejected");
        return;
    }
    nw_connection_t connection = nw_connection_create(endpoint, parameters);
    nw_release(parameters);
    nw_release(endpoint);

    if (connection == nullptr) {
        std::lock_guard<std::mutex> lock(Bonjour_Control_Mutex);
        Bonjour_Browser_Control_Active = false;
        DBG_LOG("BONJOUR_DIAG browser control rejected");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(Bonjour_Control_Mutex);
        Bonjour_Browser_Control = connection;
    }

    nw_connection_set_state_changed_handler(connection, ^(nw_connection_state_t state, nw_error_t error) {
        switch (state) {
        case nw_connection_state_ready:
            DBG_LOG("BONJOUR_DIAG browser control ready");
            nw_connection_receive(connection, kControlRecordSize, kControlRecordSize,
                                  ^(dispatch_data_t content, nw_content_context_t context, bool is_complete, nw_error_t receive_error) {
                                      (void)context;
                                      (void)is_complete;
                                      const void* bytes = nullptr;
                                      size_t size = 0;
                                      dispatch_data_t mapped =
                                          content != nullptr ? dispatch_data_create_map(content, &bytes, &size) : nullptr;
                                      if (receive_error != nullptr || mapped == nullptr || size != kControlRecordSize) {
                                          DBG_LOG("BONJOUR_DIAG browser control record rejected");
                                      } else {
                                          const unsigned char* record = static_cast<const unsigned char*>(bytes);
                                          bool valid = record[0] == 'V' && record[1] == 'C' && record[2] == 'T' && record[3] == 'D'
                                              && record[4] == 1 && record[5] == 0 && record[10] == (kLegacyUdpPort >> 8)
                                              && record[11] == (kLegacyUdpPort & 0xff) && Is_Usable_IPv4(record + 6);
                                          if (valid) {
                                              Queue_Pending_Endpoint(record + 6);
                                              DBG_LOG("BONJOUR_DIAG browser control record accepted; endpoint queued");
                                          } else {
                                              DBG_LOG("BONJOUR_DIAG browser control record rejected");
                                          }
                                      }
                                      Finish_Browser_Control(connection);
                                  });
            break;
        case nw_connection_state_failed:
            DBG_LOG("BONJOUR_DIAG browser control failed: domain=%d error=%d", nw_error_get_error_domain(error),
                    nw_error_get_error_code(error));
            Finish_Browser_Control(connection);
            break;
        case nw_connection_state_cancelled:
            Finish_Browser_Control(connection);
            break;
        default:
            break;
        }
    });
    nw_connection_set_queue(connection, Bonjour_Queue);
    nw_connection_start(connection);
    DBG_LOG("BONJOUR_DIAG browser control connection started");
}

void Ensure_Bonjour_Queue()
{
    if (Bonjour_Queue == nullptr) {
        Bonjour_Queue = dispatch_queue_create("com.vanillaconquer.td.bonjour", DISPATCH_QUEUE_SERIAL);
    }
}

void Log_Listener_State(nw_listener_state_t state, nw_error_t error)
{
    switch (state) {
    case nw_listener_state_ready:
        if (Bonjour_Listener != nullptr) {
            DBG_LOG("BONJOUR_DIAG listener ready: port=%u", nw_listener_get_port(Bonjour_Listener));
        }
        break;
    case nw_listener_state_failed:
        DBG_LOG("BONJOUR_DIAG listener failed: domain=%d error=%d", nw_error_get_error_domain(error),
                nw_error_get_error_code(error));
        break;
    case nw_listener_state_cancelled:
        DBG_LOG("BONJOUR_DIAG listener cancelled");
        break;
    default:
        break;
    }
}

void Log_Browser_State(nw_browser_state_t state, nw_error_t error)
{
    switch (state) {
    case nw_browser_state_ready:
        DBG_LOG("BONJOUR_DIAG browser ready");
        break;
    case nw_browser_state_failed:
        DBG_LOG("BONJOUR_DIAG browser failed: error=%d", nw_error_get_error_code(error));
        break;
    case nw_browser_state_cancelled:
        DBG_LOG("BONJOUR_DIAG browser cancelled");
        break;
    default:
        break;
    }
}

} // namespace

namespace TDBonjourDiscovery {

void Start_Host()
{
    if (Bonjour_Listener != nullptr) {
        return;
    }

    Ensure_Bonjour_Queue();

    nw_parameters_t parameters = nw_parameters_create_secure_tcp(NW_PARAMETERS_DISABLE_PROTOCOL,
                                                                   NW_PARAMETERS_DEFAULT_CONFIGURATION);
    Bonjour_Listener = nw_listener_create(parameters);
    nw_release(parameters);

    nw_advertise_descriptor_t descriptor = nw_advertise_descriptor_create_bonjour_service(nullptr, kBonjourServiceType, nullptr);
    nw_listener_set_advertise_descriptor(Bonjour_Listener, descriptor);
    nw_release(descriptor);

    nw_listener_set_state_changed_handler(Bonjour_Listener, ^(nw_listener_state_t state, nw_error_t error) {
        Log_Listener_State(state, error);
    });
    nw_listener_set_new_connection_handler(Bonjour_Listener, ^(nw_connection_t connection) {
        __block bool finished = false;
        void (^finish)(void) = ^{
            if (!finished) {
                finished = true;
                nw_connection_cancel(connection);
                nw_release(connection);
            }
        };

        nw_retain(connection);
        DBG_LOG("BONJOUR_DIAG host control connection accepted");
        nw_connection_set_state_changed_handler(connection, ^(nw_connection_state_t state, nw_error_t error) {
            switch (state) {
            case nw_connection_state_ready:
                DBG_LOG("BONJOUR_DIAG host control connection ready");
                Send_Host_Control_Record(connection, finish);
                break;
            case nw_connection_state_failed:
                DBG_LOG("BONJOUR_DIAG host control failed: domain=%d error=%d", nw_error_get_error_domain(error),
                        nw_error_get_error_code(error));
                finish();
                break;
            case nw_connection_state_cancelled:
                finish();
                break;
            default:
                break;
            }
        });
        nw_connection_set_queue(connection, Bonjour_Queue);
        nw_connection_start(connection);
    });
    nw_listener_set_queue(Bonjour_Listener, Bonjour_Queue);
    nw_listener_start(Bonjour_Listener);
    DBG_LOG("BONJOUR_DIAG listener start");
}

void Stop_Host()
{
    if (Bonjour_Listener == nullptr) {
        return;
    }

    nw_listener_cancel(Bonjour_Listener);
    nw_release(Bonjour_Listener);
    Bonjour_Listener = nullptr;
    DBG_LOG("BONJOUR_DIAG listener stop");
}

void Start_Browse()
{
    if (Bonjour_Browser != nullptr) {
        return;
    }

    Ensure_Bonjour_Queue();
    Bonjour_Browse_Result_Count = 0;

    nw_browse_descriptor_t descriptor = nw_browse_descriptor_create_bonjour_service(kBonjourServiceType, nullptr);
    nw_parameters_t parameters = nw_parameters_create();
    Bonjour_Browser = nw_browser_create(descriptor, parameters);
    nw_release(parameters);
    nw_release(descriptor);

    nw_browser_set_state_changed_handler(Bonjour_Browser, ^(nw_browser_state_t state, nw_error_t error) {
        Log_Browser_State(state, error);
    });
    nw_browser_set_browse_results_changed_handler(Bonjour_Browser,
                                                  ^(nw_browse_result_t old_result, nw_browse_result_t new_result, bool changes_complete) {
                                                      nw_browse_result_change_t changes =
                                                          nw_browse_result_get_changes(old_result, new_result);
                                                      bool added = (changes & nw_browse_result_change_result_added) != 0;
                                                      bool removed = (changes & nw_browse_result_change_result_removed) != 0;

                                                      if (added) {
                                                          ++Bonjour_Browse_Result_Count;
                                                      }
                                                      if (removed && Bonjour_Browse_Result_Count > 0) {
                                                          --Bonjour_Browse_Result_Count;
                                                      }

                                                      DBG_LOG("BONJOUR_DIAG browser results: count=%zu added=%d removed=%d complete=%d",
                                                              Bonjour_Browse_Result_Count, added ? 1 : 0, removed ? 1 : 0,
                                                              changes_complete ? 1 : 0);
                                                      if (added) {
                                                          Start_Browser_Control(new_result);
                                                      }
                                                  });
    nw_browser_set_queue(Bonjour_Browser, Bonjour_Queue);
    nw_browser_start(Bonjour_Browser);
    DBG_LOG("BONJOUR_DIAG browser start");
}

void Stop_Browse()
{
    Cancel_Browser_Control();
    {
        std::lock_guard<std::mutex> lock(Bonjour_Endpoint_Mutex);
        Bonjour_Has_Pending_Endpoint = false;
    }

    if (Bonjour_Browser == nullptr) {
        return;
    }

    nw_browser_cancel(Bonjour_Browser);
    nw_release(Bonjour_Browser);
    Bonjour_Browser = nullptr;
    Bonjour_Browse_Result_Count = 0;
    DBG_LOG("BONJOUR_DIAG browser stop");
}

bool Take_Pending_Endpoint(unsigned char ipv4[4])
{
    std::lock_guard<std::mutex> lock(Bonjour_Endpoint_Mutex);
    if (!Bonjour_Has_Pending_Endpoint) {
        return false;
    }

    memcpy(ipv4, Bonjour_Pending_Endpoint, sizeof(Bonjour_Pending_Endpoint));
    Bonjour_Has_Pending_Endpoint = false;
    return true;
}

} // namespace TDBonjourDiscovery
