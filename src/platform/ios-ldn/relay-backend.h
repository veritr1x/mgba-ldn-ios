/* MPL-2.0; see the repository LICENSE. */
#ifndef IOS_RELAY_BACKEND_H
#define IOS_RELAY_BACKEND_H
#include <mgba/internal/gba/sio/rfu.h>
typedef bool (*IOSRelaySend)(void *, const uint8_t ip[4], const uint8_t *, size_t);
typedef bool (*IOSRelayCanSend)(void *);
typedef void (*IOSRelayLog)(void *, const char *);
/* All calls, including RFU callbacks, are confined to the emulator/UI thread. */
struct GBASIORFUBackend *IOSRelayCreate(IOSRelaySend, IOSRelayCanSend, IOSRelayLog, void *);
bool IOSRelayConfigure(struct GBASIORFUBackend *, const uint8_t *, size_t, uint32_t now);
void IOSRelayReceive(struct GBASIORFUBackend *, const uint8_t ip[4], const uint8_t *, size_t);
void IOSRelayTick(struct GBASIORFUBackend *, uint32_t now);
void IOSRelayStop(struct GBASIORFUBackend *);
#endif
