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

namespace {

constexpr const char* kBonjourServiceType = "_vctd._tcp";

dispatch_queue_t Bonjour_Queue = nullptr;
nw_listener_t Bonjour_Listener = nullptr;
nw_browser_t Bonjour_Browser = nullptr;
size_t Bonjour_Browse_Result_Count = 0;

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
        DBG_LOG("BONJOUR_DIAG listener failed: error=%d", nw_error_get_error_code(error));
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
                                                  });
    nw_browser_set_queue(Bonjour_Browser, Bonjour_Queue);
    nw_browser_start(Bonjour_Browser);
    DBG_LOG("BONJOUR_DIAG browser start");
}

void Stop_Browse()
{
    if (Bonjour_Browser == nullptr) {
        return;
    }

    nw_browser_cancel(Bonjour_Browser);
    nw_release(Bonjour_Browser);
    Bonjour_Browser = nullptr;
    Bonjour_Browse_Result_Count = 0;
    DBG_LOG("BONJOUR_DIAG browser stop");
}

} // namespace TDBonjourDiscovery
