/* MPL-2.0. Experimental two-player Pia 6.39 host; no LDN radio emulation. */
#ifndef IOS_PIA_HOST_H
#define IOS_PIA_HOST_H
#include "ldn-pia-connect.h"
struct PiaHost {
    uint8_t mac[6], ip[4], peerMac[6], peerIp[4], ssid[16];
    uint16_t var, peerVar;
    bool native, netAcked;
    uint8_t maxStations;
    bool learnPeerVar; /* Native guest chooses its identity; lock after authenticated join. */
    unsigned state; /* 0 Net, 1 Session finalization, 2 connected */
    uint32_t deadline;
    uint8_t update[256]; size_t updateSize;
    struct LdnPiaOutMessage out[8]; unsigned count;
};
void PiaHostInit(struct PiaHost *, const uint8_t ssid[16]);
bool PiaHostReceive(struct PiaHost *, uint8_t proto, const uint8_t *, size_t);
void PiaHostTick(struct PiaHost *, uint32_t now);
bool PiaHostDrain(struct PiaHost *, struct LdnPiaOutMessage *);
#endif
