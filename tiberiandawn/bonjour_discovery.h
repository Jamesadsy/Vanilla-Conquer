//
// TD-private Bonjour discovery lifecycle bridge.
//
// The implementation is intentionally available only to iOS builds. Other
// platforms retain the existing multiplayer behaviour through harmless no-ops.
//

#ifndef TIBERIANDAWN_BONJOUR_DISCOVERY_H
#define TIBERIANDAWN_BONJOUR_DISCOVERY_H

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace TDBonjourDiscovery {

#if defined(__APPLE__) && TARGET_OS_IOS
void Start_Host();
void Stop_Host();
void Start_Browse();
void Stop_Browse();
#else
inline void Start_Host() {}
inline void Stop_Host() {}
inline void Start_Browse() {}
inline void Stop_Browse() {}
#endif

} // namespace TDBonjourDiscovery

#endif
