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
/* Check after servicing transport, before each emulated frame. A false result
 * holds emulation only; transport ticking/receiving must continue. */
bool IOSRelayCanAdvanceFrame(struct GBASIORFUBackend *);
void IOSRelayStop(struct GBASIORFUBackend *);
/* Explicit emulator laboratory mode; never enabled by stock LDN metadata.
 * Metadata: MGL1, random SSID[16], RFU host id LE16, next slot, broadcast[6] LE32. */
#define IOS_LAB_METADATA_SIZE 47
void IOSRelaySetLabHost(struct GBASIORFUBackend *, bool enabled);
bool IOSRelayLabAdvertisement(struct GBASIORFUBackend *, uint8_t out[IOS_LAB_METADATA_SIZE]);
bool IOSRelayConfigureLab(struct GBASIORFUBackend *, const uint8_t *, size_t, bool host, uint32_t now);
void IOSRelayUpdateLabAdvertisement(struct GBASIORFUBackend *, const uint8_t *, size_t);
/* Native LDN AP is owned by the relay; host Pia/RFU runs on the emulator thread. */
void IOSRelayEnableNativeHost(struct GBASIORFUBackend *, bool enabled);
/* Gift scripts already speak the Switch protocol; do not apply trade/save shims. */
void IOSRelaySetGiftHost(struct GBASIORFUBackend *, bool enabled);
bool IOSRelayNativeAdvertisement(struct GBASIORFUBackend *, uint8_t out[122]);
bool IOSRelayConfigureNativeHost(struct GBASIORFUBackend *, const uint8_t *, size_t, uint32_t now);
#endif
