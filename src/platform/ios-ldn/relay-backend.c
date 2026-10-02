/* Derived from mGBA LDN's rfu-broadcast.c. Copyright (c) 2026 mgba_ldn contributors.
 * Mozilla Public License 2.0: https://mozilla.org/MPL/2.0/ */
#include "relay-backend.h"
#include "ldn.h"
#include "ldn-pia.h"
#include "ldn-pia-connect.h"
#include "ldn-pia-reliable.h"
#include "trade-shim.h"
#include "pia-host.h"
#include "host-trade-shim.h"
#include "relay/relay_protocol.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
enum { kPiaMaxTiled = 5 + 8 + LDN_PIA_RELIABLE_MAX_PAYLOAD + 2 + 16 };
struct PiaPktids {
	uint16_t dst[4];
	uint16_t next[4];
	unsigned count;
};

static uint16_t _nextPktid(struct PiaPktids* ids, uint16_t dst) {
	for (unsigned i = 0; i < ids->count; ++i) {
		if (ids->dst[i] == dst) {
			uint16_t id = ids->next[i];
			ids->next[i] = id < 0xFFFF ? id + 1 : 1;
			return id;
		}
	}
	if (ids->count < sizeof(ids->dst) / sizeof(ids->dst[0])) {
		ids->dst[ids->count] = dst;
		ids->next[ids->count] = 2;
		++ids->count;
		return 1;
	}
	return 1;
}

struct IOSRelay {
	struct GBASIORFUBackend d;
	struct GBASIORFU *rfu;
    bool labMode, labHost, labAdvertising, nativeHostEnabled, nativeHost, giftHost;
    bool nativeAcceptPending, nativeConnectSeen, nativeConnectAcked;
    uint16_t nativeConnectNext;
    uint8_t labSSID[16], labNextSlot;
    uint16_t labDeviceId, labClientId;
    uint32_t labWords[6];
    struct PiaHost host;
    bool labConnectPending;
	struct LdnPiaCrypto piaCrypto;
	struct LdnPiaConnect piaConn;
	struct LdnPiaReliable* piaReliable; // heap-allocated - too large for an inline struct member (see the project notes)
	bool piaOpenedStream;
	uint8_t piaOurMac[6];
	uint8_t piaHostMac[6];
	uint8_t piaOurIp[4];
	uint8_t piaHostIp[4];
	uint8_t piaBroadcastIp[4];
	uint16_t piaHostVar; // mirrors ldn-pia-join.c's PiaSender.hostVar - resynced from piaConn.hostVar once known
	uint64_t piaNonceCounter;
	struct PiaPktids piaPktids;
	unsigned piaTick;
    bool reliableUnsent[LDN_PIA_RELIABLE_MAX_INFLIGHT];
    uint32_t pendingK[256];unsigned pendingKHead,pendingKCount;
    uint32_t windowLogAt;

	// Emulator-frame layer on top of the Reliable stream (see _gba* helpers): the Switch host does not understand raw
	// RFU bytes, only `57 <type> <len:u16 LE> <body>` frames - a 'C' connect request from us, its 'A' accept, 'T' slot
	// carriers both ways, and a 'K' ack from us for every host 'T'.
	uint16_t piaConnectId; // our self-chosen, nonzero RFU connection id (the host just echoes it back)
	bool piaConnectQueued; // 'C' has been queued
	bool piaHostTSeen; // the host's own 'T' slot stream has started (its first idle keepalive arrived)
	bool piaAccepted; // the host's 'A' arrived - only then do the game's slots go out as 'T' frames
	uint32_t piaTs; // per-NEW-frame 'T' counter
	uint32_t piaKSeq; // joiner-global 'K' counter (+1 from 1)

	// Sits between the game's slots and the Switch's: a retail cartridge runs one post-trade standby round fewer than
	// the Switch release, which deadlocks both at "Communication standby" (see trade-shim.h). Serves the Wireless
	// Adapter and the RFU Cable Wrapper alike (the wrapper's wireless side is a retail-like child). Reset for each
	// session, and owned by the emulation thread like the rest of the session.
	struct LdnTradeShim piaShim;
    struct HostTradeShim hostTrade;

	// Child frames waiting for the Switch. The Switch's game takes about one child frame per datagram it sends and validates
	// a mod-8 sequence tag on every command, so a burst (the wrapper answers a backlog of host frames all at once) or a
	// dropped frame desyncs it. Frames therefore wait here and leave one per emulated frame, each against a credit earned by
	// one host 'T' (at most two saved up), stamped as they leave; when nothing is waiting the last idle frame is repeated.
	// This is what GB-Link's ESP32 firmware does (pia_link.c) and what its trade shim's tag stamping presumes.
	struct {
		uint8_t data[128];
		uint16_t length;
	} piaOut[64];
	int piaOutHead, piaOutCount;
    unsigned nativeRfuCoalesced, nativeRfuQueuePeak;
    unsigned nativeHeldFrames;
	uint8_t piaLastQueued[128];
	uint16_t piaLastQueuedLength;
	uint8_t piaIdle[128];
	uint16_t piaIdleLength;
	bool piaHasIdle;
	int piaCredits;
    bool piaActive, searching, requested, notified, failed;
    uint16_t piaDeviceId;
    uint32_t now, lastBeacon, lastReceive, started, requestedAt;
    int lastState;
    IOSRelaySend send;
    IOSRelayCanSend canSend;
    IOSRelayLog log;
    void *user;
    struct LdnRfuBeacon beacon;
    unsigned received, decrypted, rejected, transmitted;
    struct {
        uint64_t raw, packed, wire, compressed, rxWire;
        unsigned txBins[6], rxBins[6];
    } sizes;
    uint32_t sizeLogAt;
};
static void relayTrace(struct GBASIORFU *rfu, const char *format, ...) {
    if (!rfu || !rfu->backend) return;
    struct IOSRelay *b=(struct IOSRelay *)rfu->backend;
    char text[800]="TRACE ";va_list args;va_start(args,format);vsnprintf(text+6,sizeof(text)-6,format,args);va_end(args);
    b->log(b->user,text);
}
// ---- Emulator ("gba") frames carried inside Reliable payloads - see pokeldn/frlgsim's gbaframe.py --------------

enum { kGbaMarker = 0x57, kGbaC = 0x43, kGbaA = 0x41, kGbaT = 0x54, kGbaK = 0x4B, kGbaD = 0x44 };

static bool _piaSendRaw(struct IOSRelay* broadcast, uint8_t proto, uint16_t dst, uint16_t src, bool establishing, bool footer,
                        bool compress, bool haveMsgFlags, uint8_t msgFlags, const uint8_t* payload, size_t length);

/* Reliable frames stay in the window until the BLE link can accept a datagram.
 * The pump tiles acknowledgements and multiple game frames together instead of
 * paying a BLE request/read round trip for each tiny K or T frame. */
static bool _reliableQueue(struct IOSRelay* broadcast, const uint8_t* payload, size_t length) {
	uint16_t seq = 0;
	bool queued = LdnPiaReliableSend(broadcast->piaReliable, payload, length, broadcast->now, &seq);
	relayTrace(broadcast->rfu, "PIA    queue reliable seq=%04X %zu bytes type=%c queued=%d", seq, length, length > 1 ? payload[1] : '?', queued);
	if (queued) {
		broadcast->reliableUnsent[seq % LDN_PIA_RELIABLE_MAX_INFLIGHT] = true;
	}
	if (!queued) {
        broadcast->failed = true;
        broadcast->log(broadcast->user, "Reliable window full; reconnect required");
    }
    return queued;
}

static void _gbaSendConnect(struct IOSRelay* broadcast) {
	// 57 43 02 00 <connect_id:2> - the reference writes the id as the two bytes given (e.g. 67 79).
	uint8_t frame[6] = {kGbaMarker, kGbaC, 2, 0, (uint8_t) broadcast->piaConnectId, (uint8_t) (broadcast->piaConnectId >> 8)};
	broadcast->piaConnectQueued = _reliableQueue(broadcast, frame, sizeof(frame));
}

// The game's client slot (LLSF header + payload, exactly as it would have sent to a real adapter) -> a child 'T'
// frame: 57 54 <body_len:u16 LE> | <ts:u32 LE> 00 <slot_len:u8> 00 00 | <slot, zero-padded to a multiple of 4>.
static void _gbaSendSlot(struct IOSRelay* broadcast, const uint8_t* slot, size_t length) {
	uint8_t frame[4 + 8 + LDN_PIA_RELIABLE_MAX_PAYLOAD];
	size_t padded = (length + 3) & ~(size_t) 3;
	if (length > 255 || 8 + padded > LDN_PIA_RELIABLE_MAX_PAYLOAD) {
		return;
	}
	uint32_t ts = ++broadcast->piaTs;
	size_t bodyLength = 8 + padded;
	frame[0] = kGbaMarker;
	frame[1] = kGbaT;
	frame[2] = (uint8_t) bodyLength;
	frame[3] = (uint8_t) (bodyLength >> 8);
	frame[4] = (uint8_t) ts;
	frame[5] = (uint8_t) (ts >> 8);
	frame[6] = (uint8_t) (ts >> 16);
	frame[7] = (uint8_t) (ts >> 24);
	frame[8] = 0;
	frame[9] = (uint8_t) length;
	frame[10] = 0;
	frame[11] = 0;
	memset(&frame[12], 0, padded);
	memcpy(&frame[12], slot, length);
	_reliableQueue(broadcast, frame, 4 + bodyLength);
}

// The words of a slot, for the trace: only frames that carry a command (an idle slot is all zero after its header).
static void _traceSlot(struct IOSRelay* broadcast, const char* what, const uint8_t* slot, size_t length) {
	char hex[3 * 16 + 1];
	size_t shown = length < 16 ? length : 16;
	for (size_t i = 0; i < shown; ++i) {
		snprintf(&hex[i * 3], 4, "%02X ", slot[i]);
	}
	hex[shown * 3] = 0;
	relayTrace(broadcast->rfu, "PIA    %s %s", what, hex);
}

static bool _slotIdle(const uint8_t* p, size_t length) {
	for (size_t i = 2; i < length; ++i) {
		if (p[i]) {
			return false;
		}
	}
	return true;
}

// Whether a queued child frame is superseded by the ones behind it, so the Switch can go without it: an idle slot, or a
// held-keys report of nothing or of a direction (the next report says the same or newer). A button report (A, READY,
// EXIT_ROOM) happens once and must arrive.
static bool _slotSheddable(const uint8_t* p, size_t length) {
	if (length < 6 || (((p[0] | (p[1] << 8)) >> 10) & 15) != 4) {
		return false;
	}
	if (p[3] == 0) {
		return true;
	}
	if (p[3] != 0xBE) {
		return false;
	}
	return p[4] == 0 || (p[4] >= 0x11 && p[4] <= 0x15);
}

enum { PIA_OUT_SLOTS = 64, PIA_OUT_BYTES = 128, PIA_OUT_SHED_AT = 3 };

// Every child slot bound for the host goes through here, the game's own and the shim's injected ones alike: it waits in the
// queue (see the struct) and is stamped with the next consecutive sequence tag only when it leaves (_gbaPumpChild), so the
// host, which drops the child after five out-of-sequence commands, sees the tags of exactly the frames it is sent.
static void _gbaSendChildSlot(struct IOSRelay* broadcast, uint8_t* slot, size_t length) {
	if (!length || length > PIA_OUT_BYTES) {
		return;
	}
	if (_slotIdle(slot, length)) {
		memcpy(broadcast->piaIdle, slot, length);
		broadcast->piaIdleLength = (uint16_t) length;
		broadcast->piaHasIdle = true;
	}
	// Only an exact repeat of the frame queued last is collapsed (an RFU-level retransmit of something the peer has not
	// consumed); anything that differs, its tag included, is a distinct frame.
	if (broadcast->piaLastQueuedLength == length && !memcmp(broadcast->piaLastQueued, slot, length)) {
		return;
	}
	if (broadcast->piaOutCount >= PIA_OUT_SLOTS) {
		// Full: an idle frame only repeats the child's empty state, so it is the one to give up.
		if (_slotIdle(slot, length)) {
			return;
		}
		int victim = -1;
		for (int i = broadcast->piaOutCount - 1; i >= 0 && victim < 0; --i) {
			int at = (broadcast->piaOutHead + i) % PIA_OUT_SLOTS;
			if (_slotIdle(broadcast->piaOut[at].data, broadcast->piaOut[at].length)) {
				victim = i;
			}
		}
		if (victim < 0) {
			broadcast->failed = true;
            broadcast->log(broadcast->user, "Child queue overflow; reconnect required");
			return;
		}
		for (int i = victim; i < broadcast->piaOutCount - 1; ++i) {
			broadcast->piaOut[(broadcast->piaOutHead + i) % PIA_OUT_SLOTS] =
			    broadcast->piaOut[(broadcast->piaOutHead + i + 1) % PIA_OUT_SLOTS];
		}
		--broadcast->piaOutCount;
	}
	int at = (broadcast->piaOutHead + broadcast->piaOutCount) % PIA_OUT_SLOTS;
	memcpy(broadcast->piaOut[at].data, slot, length);
	broadcast->piaOut[at].length = (uint16_t) length;
	++broadcast->piaOutCount;
	// A backlog is lag on the game's own screen, and superseded frames (idle ones, key reports) in it can be shed, oldest
	// first and never the newest.
	for (int i = 0; broadcast->piaOutCount > PIA_OUT_SHED_AT && i < broadcast->piaOutCount - 1;) {
		int slotAt = (broadcast->piaOutHead + i) % PIA_OUT_SLOTS;
		if (!_slotSheddable(broadcast->piaOut[slotAt].data, broadcast->piaOut[slotAt].length)) {
			++i;
			continue;
		}
		for (int j = i; j < broadcast->piaOutCount - 1; ++j) {
			broadcast->piaOut[(broadcast->piaOutHead + j) % PIA_OUT_SLOTS] =
			    broadcast->piaOut[(broadcast->piaOutHead + j + 1) % PIA_OUT_SLOTS];
		}
		--broadcast->piaOutCount;
	}
	memcpy(broadcast->piaLastQueued, slot, length);
	broadcast->piaLastQueuedLength = (uint16_t) length;
}

// Once per emulated frame (the Switch's own ~59.7 Hz cadence): the next waiting child frame, or a repeat of the last idle
// one, goes out against one credit.
static void _gbaPumpChild(struct IOSRelay* broadcast) {
	if (broadcast->pendingKCount || broadcast->piaCredits <= 0 || (uint16_t)(broadcast->piaReliable->outSeq-broadcast->piaReliable->windowLo) >= LDN_PIA_RELIABLE_MAX_INFLIGHT-4) {
		return;
	}
	uint8_t slot[PIA_OUT_BYTES];
	size_t length;
	if (broadcast->piaOutCount > 0) {
		int at = broadcast->piaOutHead;
		broadcast->piaOutHead = (broadcast->piaOutHead + 1) % PIA_OUT_SLOTS;
		--broadcast->piaOutCount;
		length = broadcast->piaOut[at].length;
		memcpy(slot, broadcast->piaOut[at].data, length);
		LdnTradeShimStamp(&broadcast->piaShim, slot, length);
		if (length >= 4 && slot[3]) {
			_traceSlot(broadcast, "child cmd (stamped):", slot, length);
		}
	} else if (broadcast->piaHasIdle) {
		length = broadcast->piaIdleLength;
		memcpy(slot, broadcast->piaIdle, length);
	} else {
		return;
	}
	--broadcast->piaCredits;
	_gbaSendSlot(broadcast, slot, length);
}

// 57 4b 0c 00 <k_seq:u32><mid:u32><acked_host_ts:u32>, all LE - one per unique host 'T'.
static void _gbaQueueAck(struct IOSRelay* broadcast, uint32_t ackedTs) {
	uint8_t frame[16] = {kGbaMarker, kGbaK, 12, 0};
	uint32_t kSeq = ++broadcast->piaKSeq;
	uint32_t mid = 1;
	for (int i = 0; i < 4; ++i) {
		frame[4 + i] = (uint8_t) (kSeq >> (8 * i));
		frame[8 + i] = (uint8_t) (mid >> (8 * i));
		frame[12 + i] = (uint8_t) (ackedTs >> (8 * i));
	}
	_reliableQueue(broadcast, frame, sizeof(frame));
}

/* K acknowledgements wait in order until there is a reliable-window slot.
 * Child traffic yields to them. No successful host command is silently shed. */
static void _gbaFlushAcks(struct IOSRelay *b) {
    while (b->pendingKCount && b->piaReliable->localOpened &&
           !b->piaReliable->unacked[b->piaReliable->outSeq % LDN_PIA_RELIABLE_MAX_INFLIGHT].used) {
        _gbaQueueAck(b,b->pendingK[b->pendingKHead]);
        b->pendingKHead=(b->pendingKHead+1)%256;b->pendingKCount--;
    }
}
static void _gbaSendAck(struct IOSRelay *b,uint32_t ts) {
    _gbaFlushAcks(b);
    if(b->pendingKCount==256){b->failed=true;b->log(b->user,"Pending K acknowledgements full after sustained backpressure; reconnect required");return;}
    b->pendingK[(b->pendingKHead+b->pendingKCount)%256]=ts;b->pendingKCount++;
    _gbaFlushAcks(b);
}

// One delivered (in-order, non-stream-open) Reliable payload: zero or more frames back to back.
static void _labHostReceive(struct IOSRelay *,const uint8_t *,size_t);
static void _gbaReceive(struct IOSRelay* broadcast, const uint8_t* data, size_t length) {
    if(broadcast->labHost){_labHostReceive(broadcast,data,length);return;}
	while (length >= 4 && data[0] == kGbaMarker) {
		uint8_t type = data[1];
		size_t bodyLength = data[2] | ((size_t) data[3] << 8);
		if (4 + bodyLength > length) {
			relayTrace(broadcast->rfu, "PIA    gba frame '%c' truncated (%zu of %zu body bytes)", type, length - 4, bodyLength);
			return;
		}
		const uint8_t* body = &data[4];
		if (type == kGbaA) {
			if (bodyLength != 6 || (uint16_t)(body[2] | (body[3] << 8)) != broadcast->piaConnectId) return;
            broadcast->piaAccepted = true;
            if (!broadcast->notified && broadcast->requested) {
                broadcast->notified = true;
                GBASIORFUConnectResult(broadcast->rfu, true, broadcast->piaDeviceId, 0);
                broadcast->log(broadcast->user, "Game host accepted RFU connection (A)");
            }
			relayTrace(broadcast->rfu, "PIA    host ACCEPTED our connect ('A' body %zu bytes)", bodyLength);
		} else if (type == kGbaT && bodyLength >= 8) {
			uint32_t ts = body[0] | (body[1] << 8) | (body[2] << 16) | ((uint32_t) body[3] << 24);
			size_t slotLength = body[4];
			relayTrace(broadcast->rfu, "PIA    host 'T' ts=%u slot_len=%zu", ts, slotLength);
			broadcast->piaHostTSeen = true;
			_gbaSendAck(broadcast, ts);
			if (broadcast->piaCredits < 2) {
				++broadcast->piaCredits; // one child frame may follow each host frame
			}
			if (slotLength > 1 && 8 + slotLength <= bodyLength) {
				uint8_t slot[256];
				uint8_t pre[4 * LDN_TRADE_SHIM_HOST_FRAME];
				memcpy(slot, &body[8], slotLength);
				if (slotLength >= 31 && (slot[3] | slot[4] | slot[17] | slot[18])) {
					// slot 0 (the host) and slot 1 (its echo of us), first 6 bytes of each
					char words[64];
					snprintf(words, sizeof(words), "%02X%02X%02X%02X%02X%02X | %02X%02X%02X%02X%02X%02X", slot[3], slot[4], slot[5], slot[6], slot[7],
					         slot[8], slot[17], slot[18], slot[19], slot[20], slot[21], slot[22]);
					relayTrace(broadcast->rfu, "PIA    host cmd hdr=%02X%02X%02X %s", slot[0], slot[1], slot[2], words);
				}
				size_t preLength = broadcast->labMode?0:LdnTradeShimHost(&broadcast->piaShim, slot, slotLength, broadcast->now, pre, sizeof(pre));
				for (size_t at = 0; at + LDN_TRADE_SHIM_HOST_FRAME <= preLength; at += LDN_TRADE_SHIM_HOST_FRAME) {
					GBASIORFUDataReceived(broadcast->rfu, 0, &pre[at], LDN_TRADE_SHIM_HOST_FRAME);
				}
				GBASIORFUDataReceived(broadcast->rfu, 0, slot, slotLength);
			}
		} else if (type == kGbaD) {
			relayTrace(broadcast->rfu, "PIA    host sent 'D' (disconnect)");
			GBASIORFUDisconnected(broadcast->rfu, 0);
            broadcast->failed = true;
		} else {
			relayTrace(broadcast->rfu, "PIA    host gba frame '%c' (%zu body bytes) ignored", type, bodyLength);
		}
		data += 4 + bodyLength;
		length -= 4 + bodyLength;
	}
}
static bool _piaSendTiled(struct IOSRelay *broadcast, uint8_t *tiled, size_t tiledLength,
                          uint16_t dst, uint16_t src, bool establishing, bool footer, bool compress) {
    if (!broadcast->canSend(broadcast->user)) return false;
	size_t rawLength = tiledLength;
	bool compressed = false;
	// The native client compresses any message body of 62 bytes or more (GB-Link's firmware does the same); the
	// Switch decompresses by the flag, so this is for fidelity rather than correctness.
	if (compress || tiledLength >= 62) {
		uint8_t compbuf[kPiaMaxTiled];
		size_t compLength = sizeof(compbuf);
		if (LdnPiaCompress(tiled, tiledLength, compbuf, &compLength) && compLength < tiledLength) {
			memcpy(tiled, compbuf, compLength);
			tiledLength = compLength;
			compressed = true;
		}
	}
	size_t packedLength = tiledLength;
	if (footer) {
		tiled[tiledLength++] = (uint8_t) (broadcast->piaHostVar >> 8);
		tiled[tiledLength++] = (uint8_t) broadcast->piaHostVar;
	}
	size_t beforePad = tiledLength;
	while (tiledLength % 16 != 0) {
		tiled[tiledLength++] = 0xFF;
	}
	uint8_t pad = (uint8_t) (tiledLength - beforePad);

	struct LdnPiaHeader header;
	header.dst = dst;
	header.src = src;
	header.pktid = establishing ? 0 : _nextPktid(&broadcast->piaPktids, dst);
	header.enc = 0x90;
	header.flags = (uint8_t) ((pad << 4) | (compressed ? 1 : 0) | (establishing ? 2 : 0));
	header.footer = footer ? 2 : 0;
	++broadcast->piaNonceCounter;
	for (int i = 0; i < 8; ++i) {
		header.nonce8[i] = (uint8_t) (broadcast->piaNonceCounter >> (8 * (7 - i)));
	}

	uint8_t datagram[LDN_PIA_CIPHERTEXT_OFFSET + kPiaMaxTiled];
	size_t datagramLength;
	if (!LdnPiaEncrypt(&broadcast->piaCrypto, tiled, tiledLength, broadcast->piaOurIp, &header, datagram, &datagramLength)) {
		return false;
	}
	int rc = broadcast->send(broadcast->user, broadcast->piaHostIp, datagram, datagramLength) ? 0 : -1;
	/* Net discovery is broadcast by the native host. Keep the unicast copy
	 * for delivery and add the identical discovery packet when capacity permits.
	 * The optional copy must not fail a successfully queued unicast. */
	if (!rc && broadcast->nativeHost && establishing && broadcast->canSend(broadcast->user)) {
		if (broadcast->send(broadcast->user, broadcast->piaBroadcastIp, datagram, datagramLength)) {
			++broadcast->transmitted;
			broadcast->sizes.wire += datagramLength;
		}
	}
	if (rc) broadcast->failed = true;
    else {
        ++broadcast->transmitted;
        broadcast->sizes.raw += rawLength;
        broadcast->sizes.packed += packedLength;
        broadcast->sizes.wire += datagramLength;
        broadcast->sizes.compressed += compressed;
        unsigned bin=0;while(bin<5 && datagramLength>(size_t[]){64,96,128,192,256}[bin])++bin;
        ++broadcast->sizes.txBins[bin];
    }
	relayTrace(broadcast->rfu, "PIA    tx dst=%04X src=%04X pktid=%04X flags=%02X footer=%u len=%zu rc=%d", header.dst, header.src,
	               header.pktid, header.flags, header.footer, datagramLength, rc);
	return rc == 0;
}

static bool _piaSendRaw(struct IOSRelay* b, uint8_t proto, uint16_t dst, uint16_t src, bool establishing, bool footer,
                        bool compress, bool haveMsgFlags, uint8_t msgFlags, const uint8_t* payload, size_t length) {
    uint8_t tiled[kPiaMaxTiled];
    if (length > LDN_PIA_RELIABLE_MAX_PAYLOAD + 8) return false;
    size_t n=LdnPiaBuildMessage(proto,payload,length,haveMsgFlags,msgFlags,tiled);
    return _piaSendTiled(b,tiled,n,dst,src,establishing,footer,compress);
}

static void _reliablePump(struct IOSRelay *b) {
    if ((!b->piaOpenedStream && !(b->nativeHost && b->piaReliable->peerOpened)) || !b->canSend(b->user)) return;
    struct LdnPiaReliable *r=b->piaReliable;
    uint8_t tiled[kPiaMaxTiled],inner[8+LDN_PIA_RELIABLE_MAX_PAYLOAD];
    size_t used=0, selected[LDN_PIA_RELIABLE_MAX_INFLIGHT],count=0;
    bool haveOoo=false;
    uint8_t mask[16]={0};
    for (size_t i=0;i<LDN_PIA_RELIABLE_MAX_INFLIGHT;++i) if (r->recvBuf[i].used) {
        haveOoo=true;
        uint16_t bit=(uint16_t)(r->recvBuf[i].seq-r->recvNext-1);
        if (bit<128) mask[bit/8]|=1u<<(bit%8);
    }
    bool ack=r->haveNextAckMs && (int32_t)(b->now-r->nextAckMs)>=0 && (r->ackOwed || haveOoo);
    if (ack) {
        uint8_t body[20];size_t n=LdnPiaBuildBulkAck(r->recvNext,mask,body);
        n=LdnPiaBuildReliableFrame(LDN_PIA_RELIABLE_START_SEQ,LdnPiaReliableSendLow(r),0,body,n,inner);
        used=LdnPiaBuildMessage(10,inner,n,true,0x40,tiled);
    }
    /* A radio RTT sample can precede the next emulator tick. Keep a conservative
       floor so an unusually small sample cannot trigger a retransmission storm. */
    uint32_t rto=1000;
    if (r->rttCount) {
        uint32_t samples[LDN_PIA_RELIABLE_RTT_SAMPLES];
        size_t n=r->rttCount<LDN_PIA_RELIABLE_RTT_SAMPLES?r->rttCount:LDN_PIA_RELIABLE_RTT_SAMPLES;
        memcpy(samples,r->rttSamples,n*sizeof(*samples));
        for (size_t i=1;i<n;++i) for (size_t j=i;j && samples[j]<samples[j-1];--j) {
            uint32_t swap=samples[j];samples[j]=samples[j-1];samples[j-1]=swap;
        }
        rto=33+samples[n/2]*14/10;if(rto<500)rto=500;
    }
    /* New data before retries, sequence order within each class. A 400-byte
       tiled payload fits one negotiated 500-byte BLE frame after all headers. */
    for (unsigned pass=0;pass<2;++pass) for (unsigned off=0;off<LDN_PIA_RELIABLE_MAX_INFLIGHT;++off) {
        uint16_t seq=(uint16_t)(r->windowLo+off);size_t i=seq%LDN_PIA_RELIABLE_MAX_INFLIGHT;
        if (!r->unacked[i].used || r->unacked[i].acked || r->unacked[i].seq!=seq) continue;
        bool fresh=b->reliableUnsent[i];
        if ((pass==0)!=fresh || (!fresh && b->now-r->unacked[i].lastTxMs<rto)) continue;
        size_t need=13+r->unacked[i].length;
        if (used && used+need>400) break;
        size_t n=LdnPiaBuildReliableFrame(seq,LdnPiaReliableSendLow(r),r->unacked[i].flagsA,
                                        r->unacked[i].payload,r->unacked[i].length,inner);
        /* Explicit message flags reset inheritance after a bulk ACK tile. */
        used+=LdnPiaBuildMessage(10,inner,n,true,0,tiled+used);
        selected[count++]=i;
        if (used>=400) break;
    }
    if (!used || !_piaSendTiled(b,tiled,used,b->piaConn.hostVar,b->piaConn.ourVar,false,true,false)) return;
    for (size_t j=0;j<count;++j) {
        size_t i=selected[j];
        relayTrace(b->rfu,"reliable sent seq=%04X fresh=%d bytes=%zu",r->unacked[i].seq,b->reliableUnsent[i],r->unacked[i].length);
        if (!b->reliableUnsent[i]) ++r->unacked[i].resends;
        b->reliableUnsent[i]=false;r->unacked[i].lastTxMs=b->now;
    }
    if (ack) {
        if(b->nativeHost && b->nativeConnectSeen && (uint16_t)(r->recvNext-b->nativeConnectNext)<0x8000)b->nativeConnectAcked=true;
        r->ackOwed=false;r->haveNextAckMs=haveOoo;if(haveOoo)r->nextAckMs=b->now+r->ackPeriodMs;
    }
}

static bool _piaSendMessage(struct IOSRelay* broadcast, const struct LdnPiaOutMessage* msg) {
	return _piaSendRaw(broadcast, msg->proto, msg->dst, msg->src, msg->establishing, msg->footer, msg->compress, false, 0, msg->payload, msg->length);
}
/* Enqueue already translated native frames, including synthetic extra-barrier
 * answers. Keeping this separate prevents translating their counters twice. */
static void _queueLabData(struct IOSRelay *broadcast,const uint8_t *data,size_t length){
    if(broadcast->nativeHost && !broadcast->giftHost && broadcast->piaOutCount){
        unsigned tail=(broadcast->piaOutHead+broadcast->piaOutCount-1)%64;
        if(broadcast->piaOut[tail].length==length && !memcmp(broadcast->piaOut[tail].data,data,length)){
            ++broadcast->nativeRfuCoalesced;return;
        }
    }
    if(broadcast->piaOutCount==64){broadcast->failed=true;broadcast->log(broadcast->user,"Lab RFU queue overflow");return;}
    unsigned at=(broadcast->piaOutHead+broadcast->piaOutCount++)%64;
    if(broadcast->nativeHost && (unsigned)broadcast->piaOutCount>broadcast->nativeRfuQueuePeak)
        broadcast->nativeRfuQueuePeak=broadcast->piaOutCount;
    memcpy(broadcast->piaOut[at].data,data,length);broadcast->piaOut[at].length=length;
}
static void _sendData(struct GBASIORFUBackend* backend, const uint8_t* data, size_t length) {
    struct IOSRelay* broadcast = (struct IOSRelay*) backend;
    if(broadcast->labMode){
        if(!broadcast->piaAccepted || !length || length>RFU_PACKET_MAX)return;
        uint8_t mapped[RFU_PACKET_MAX];memcpy(mapped,data,length);
        if(broadcast->nativeHost && !broadcast->giftHost){
            unsigned before=broadcast->hostTrade.fakeCount;
            if(!HostTradeParent(&broadcast->hostTrade,mapped,length)){
                broadcast->failed=true;broadcast->log(broadcast->user,"Native trade synchronization failed: cannot map save barrier");return;
            }
            if(before!=broadcast->hostTrade.fakeCount){
                char text[128];snprintf(text,sizeof(text),"Native trade: mapping Switch-only save barrier %u (trade %u)",
                    broadcast->hostTrade.fake[before],broadcast->hostTrade.fakeCount);broadcast->log(broadcast->user,text);
            }
        }
        _queueLabData(broadcast,mapped,length);return;
    }
	if (!broadcast->piaActive || !broadcast->piaOpenedStream) {
		relayTrace(broadcast->rfu, "PIA    sendData %zu bytes DROPPED (active=%d opened=%d)", length, broadcast->piaActive, broadcast->piaOpenedStream);
		return;
	}
	if (!broadcast->piaAccepted) {
		relayTrace(broadcast->rfu, "PIA    sendData %zu bytes held back (host has not accepted our connect yet)", length);
		return;
	}
	uint8_t slot[RFU_PACKET_MAX];
	uint8_t reply[2 * LDN_TRADE_SHIM_HOST_FRAME];
	if (length > sizeof(slot)) {
		length = sizeof(slot);
	}
	memcpy(slot, data, length);
	bool forward;
	size_t replyLength = LdnTradeShimChild(&broadcast->piaShim, slot, length, broadcast->now, reply, sizeof(reply), &forward);
	if (forward) {
		relayTrace(broadcast->rfu, "PIA    sendData %zu bytes -> 'T' frame", length);
		_gbaSendChildSlot(broadcast, slot, length);
	} else {
		relayTrace(broadcast->rfu, "PIA    sendData %zu bytes repeat the previous command frame; not sent again", length);
	}
	for (size_t at = 0; at + LDN_TRADE_SHIM_HOST_FRAME <= replyLength; at += LDN_TRADE_SHIM_HOST_FRAME) {
		GBASIORFUDataReceived(broadcast->rfu, 0, &reply[at], LDN_TRADE_SHIM_HOST_FRAME);
	}
}

#include <time.h>
static void _shimLog(void *user, const char *message) {
    struct IOSRelay *b = user;
    b->log(b->user, message);
}
void IOSRelayStop(struct GBASIORFUBackend *backend) {
    struct IOSRelay *b = (struct IOSRelay *)backend;
    if (!b) return;
    if (b->rfu && b->requested) {
        if (b->notified) GBASIORFUDisconnected(b->rfu, 0);
        else GBASIORFUConnectResult(b->rfu, false, b->piaDeviceId, 0);
    }
    if(b->labHost && b->piaAccepted && b->rfu)GBASIORFUDisconnected(b->rfu,0);
    b->labConnectPending=false;
    b->nativeAcceptPending=b->nativeConnectSeen=b->nativeConnectAcked=false;b->nativeConnectNext=0;
    b->piaActive = false;
    b->requested = b->notified = false;
    b->piaAccepted = b->piaOpenedStream = b->piaConnectQueued = false;
    b->piaOutCount = b->piaOutHead = b->piaCredits = 0;
    b->nativeRfuCoalesced=b->nativeRfuQueuePeak=0;
    b->nativeHeldFrames=0;
    b->pendingKCount=b->pendingKHead=0;b->windowLogAt=0;
    b->piaHasIdle = false;
    b->piaLastQueuedLength = 0;
    LdnTradeShimReset(&b->piaShim);
    memset(&b->hostTrade,0,sizeof(b->hostTrade));
}
static bool _init(struct GBASIORFUBackend *backend, struct GBASIORFU *rfu) {
    struct IOSRelay *b=(struct IOSRelay *)backend;
    if (!b->piaReliable) b->piaReliable=calloc(1,sizeof(*b->piaReliable));
    if (!b->piaReliable) return false;
    b->rfu=rfu;return true;
}
static void _deinit(struct GBASIORFUBackend *backend) {
    struct IOSRelay *b = (struct IOSRelay *)backend;
    IOSRelayStop(backend); free(b->piaReliable); b->piaReliable = NULL; b->rfu = NULL;
}
static void _reset(struct GBASIORFUBackend *backend) {
    struct IOSRelay *b = (struct IOSRelay *)backend;
    /* The game resets the virtual adapter before searching. Keep a ready Pia session,
       but a reset during an active game connection requires leaving/rejoining LDN. */
    if (b->requested) { IOSRelayStop(backend); b->log(b->user, "Adapter reset: leave and rejoin the relay session"); }
    b->searching = false;
    if(b->labHost){b->labAdvertising=false;if(b->piaActive)IOSRelayStop(backend);}
}
static void _searchStart(struct GBASIORFUBackend *backend) {
    struct IOSRelay *b = (struct IOSRelay *)backend;
    b->searching = true; b->lastBeacon = b->now - 1000;
    if (b->piaActive) b->log(b->user, "Game is searching for a Wireless Adapter host");
}
static void _searchStop(struct GBASIORFUBackend *backend) { ((struct IOSRelay *)backend)->searching = false; }
static void _connect(struct GBASIORFUBackend *backend, uint16_t id) {
    struct IOSRelay *b = (struct IOSRelay *)backend;
    if (!b->piaActive || b->requested || id != b->beacon.trainerId) {
        GBASIORFUConnectResult(b->rfu, false, id, 0); return;
    }
    b->requested = true; b->piaDeviceId = id; b->requestedAt = b->now;
    b->log(b->user, "Game requested host connection; awaiting Pia/game acceptance");
}
static void _disconnect(struct GBASIORFUBackend *backend, unsigned mask) {
    (void)mask; IOSRelayStop(backend);
    struct IOSRelay *b = (struct IOSRelay *)backend;
    b->log(b->user, "Game disconnected; leave and rejoin LDN before another session");
}
static void _hostStart(struct GBASIORFUBackend *backend, uint16_t id) {
    struct IOSRelay *b = (struct IOSRelay *)backend;
    if(b->nativeHostEnabled){b->nativeHost=true;b->labHost=b->labMode=true;}
    if(b->labHost){if(!b->piaActive)LdnRandomBytes(b->labSSID,16);b->labDeviceId=id;b->labAdvertising=true;b->labNextSlot=0;}
    // The Switch-join path never creates a host.
}
static void _labBroadcast(struct GBASIORFUBackend *backend,const uint32_t words[6]){
    struct IOSRelay *b=(struct IOSRelay *)backend;memcpy(b->labWords,words,24);
}
static void _labHostStop(struct GBASIORFUBackend *backend){
    struct IOSRelay *b=(struct IOSRelay *)backend;if(b->labHost){b->labAdvertising=false;b->labNextSlot=255;}
}
static void _noopReply(struct GBASIORFUBackend *backend, uint16_t id, bool accept, unsigned slot) {
    struct IOSRelay *b=(struct IOSRelay *)backend;
    if(!b->labHost || !b->labConnectPending || id!=b->labClientId)return;
    b->labConnectPending=false;
    if(!accept || slot!=0){uint8_t d[4]={0x57,'D',0,0};_reliableQueue(b,d,4);return;}
    if(b->nativeHost){b->nativeAcceptPending=true;return;}
    uint8_t a[10]={0x57,'A',6,0,0,0,(uint8_t)id,(uint8_t)(id>>8),0,0};
    if(_reliableQueue(b,a,sizeof(a))){b->piaAccepted=true;b->labNextSlot=255;b->log(b->user,"Pia host accepted RFU child in slot 0");}
}
struct GBASIORFUBackend *IOSRelayCreate(IOSRelaySend send, IOSRelayCanSend canSend, IOSRelayLog log, void *user) {
    if (!send || !canSend || !log) return NULL;
    struct IOSRelay *b = calloc(1, sizeof(*b)); if (!b) return NULL;
    b->send = send; b->canSend=canSend; b->log = log; b->user = user;
    LdnTradeShimInit(&b->piaShim, _shimLog, b);
    b->d = (struct GBASIORFUBackend){.init=_init,.deinit=_deinit,.reset=_reset,
        .setBroadcast=_labBroadcast,.hostStart=_hostStart,.hostStop=_labHostStop,
        .connectReply=_noopReply,.searchStart=_searchStart,.searchStop=_searchStop,
        .connect=_connect,.disconnect=_disconnect,.sendData=_sendData};
    /* Tick is driven by the frontend even before the virtual RFU is powered on. */
    return &b->d;
}
static int _hex(uint8_t c) {
    if (c >= '0' && c <= '9') return c-'0';
    if (c >= 'a' && c <= 'f') return c-'a'+10;
    if (c >= 'A' && c <= 'F') return c-'A'+10;
    return -1;
}
bool IOSRelayConfigure(struct GBASIORFUBackend *backend, const uint8_t *p, size_t n, uint32_t now) {
    struct IOSRelay *b = (struct IOSRelay *)backend;
    if (!b || !p || n < 74 || p[0] != RL_CONNECTED || p[3] != 3 || p[22] != 32 ||
        lr_get64(p+12) != UINT64_C(0x01006fa0233f8000)) return false;
    uint8_t ssid[16];
    for (unsigned i=0; i<16; ++i) {
        int hi=_hex(p[23+2*i]), lo=_hex(p[24+2*i]);
        if (hi < 0 || lo < 0) return false;
        ssid[i] = (uint8_t)(hi*16+lo);
    }
    size_t at=23+32+16; uint16_t adLength=lr_get16(p+at); at+=2;
    struct LdnRfuBeacon beacon;
    if (adLength > 384 || at+adLength+1 > n || !LdnDecodeRfuBeacon(p+at, adLength, &beacon)) return false;
    at+=adLength; unsigned nodes=p[at++];
    if (nodes>8 || n != at+nodes*15) return false;
    const uint8_t *host=NULL, *us=NULL;
    for (unsigned i=0; i<nodes; ++i) {
        const uint8_t *node=p+at+i*15;
        if (!node[11]) continue;
        if (node[10]==0) host=node;
        if (!memcmp(node,p+4,4)) us=node;
    }
    if (!host || !us || host==us || !b->rfu) return false;
    IOSRelayStop(backend);
    b->labMode=false;b->labHost=false;b->nativeHost=false;
    b->beacon=beacon; b->now=b->started=b->lastReceive=now; b->lastBeacon=now-1000;
    memcpy(b->piaOurIp,p+4,4); memcpy(b->piaOurMac,us+4,6);
    memcpy(b->piaHostIp,host,4); memcpy(b->piaHostMac,host+4,6);
    LdnPiaCryptoInit(&b->piaCrypto,ssid);
    LdnPiaConnectInit(&b->piaConn,b->piaOurMac,b->piaHostMac,b->piaOurIp,"mGBA iOS");
    LdnPiaReliableInit(b->piaReliable,33,1000);
    memset(b->reliableUnsent,0,sizeof(b->reliableUnsent));
    b->piaHostVar=0x7620; b->lastState=0; b->piaTick=0;
    memset(&b->piaPktids,0,sizeof(b->piaPktids));
    /* Start above prior launches on the same SSID/IP, instead of reusing nonce 1. */
    struct timespec wall; clock_gettime(CLOCK_REALTIME,&wall);
    b->piaNonceCounter=((uint64_t)wall.tv_sec<<32)|(uint32_t)wall.tv_nsec;
    b->piaConnectId=(uint16_t)arc4random_uniform(65535)+1;
    b->piaTs=b->piaKSeq=0; b->received=b->decrypted=b->rejected=b->transmitted=0;
    memset(&b->sizes,0,sizeof(b->sizes));b->sizeLogAt=b->now;
    b->failed=false; b->piaActive=true;
    b->log(b->user,"Native metadata accepted; waiting for an authenticated Pia packet");
    return true;
}
void IOSRelayReceive(struct GBASIORFUBackend *backend, const uint8_t ip[4], const uint8_t *data, size_t length) {
    struct IOSRelay *b=(struct IOSRelay *)backend;
    if (!b || !b->piaActive || length>1400 || memcmp(ip,b->piaHostIp,4)) return;
    ++b->received;
    uint8_t plain[1400], decoded[8192]; size_t plainLength=0, decodedLength=sizeof(decoded);
    if (!LdnPiaDecrypt(&b->piaCrypto,data,length,ip,plain,&plainLength)) {
        if (++b->rejected<=3) b->log(b->user,"Pia authentication failed: SSID/key/source metadata needs checking");
        return;
    }
    if (!b->decrypted++) b->log(b->user,"First peer Pia packet authenticated and decrypted");
    b->sizes.rxWire+=length;
    unsigned sizeBin=0;while(sizeBin<5 && length>(size_t[]){64,96,128,192,256}[sizeBin])++sizeBin;
    ++b->sizes.rxBins[sizeBin];
    b->lastReceive=b->now;
    if (!LdnPiaDecompress(plain,plainLength,decoded,&decodedLength)) return;
    struct LdnPiaMessage messages[64]; size_t consumed;
    size_t count=LdnPiaParseMessages(decoded,decodedLength,messages,64,&consumed);
    if (count > 64) return;
    for (size_t i=0;i<count;++i) {
        struct LdnPiaMessage *m=&messages[i];
        if (m->proto != 3) relayTrace(b->rfu,"rx proto=%u length=%zu first=%02x",m->proto,m->payloadLength,m->payloadLength?m->payload[0]:0);
        if(b->labHost){
            unsigned previous=b->host.state;bool netAcked=b->host.netAcked;
            if(b->nativeHost && m->proto==13 && m->payloadLength>=30 && !m->payload[0]){
                struct LdnPiaHeader header;LdnPiaHeaderUnpack(data,&header);
                if(header.src!=((unsigned)m->payload[28]<<8|m->payload[29])){b->log(b->user,"Host rejected mismatched Pia source identity");continue;}
            }
            if(!PiaHostReceive(&b->host,m->proto,m->payload,m->payloadLength)){
                relayTrace(b->rfu,"host rejected proto=%u length=%zu subtype=%u",m->proto,m->payloadLength,m->payloadLength?m->payload[0]:255);continue;
            }
            b->piaHostVar=b->host.peerVar;
            /* Reliable sends use piaConn, while Session sends use PiaHost.
             * Keep both destinations synchronized with the authenticated join. */
            b->piaConn.hostVar=b->host.peerVar;b->piaConn.haveHostVar=true;
            if(!netAcked && b->host.netAcked)b->log(b->user,"Host Net probe acknowledged by peer");
            if(previous!=b->host.state)b->log(b->user,b->host.state==2?"Host Pia session connected":"Host Pia session finalizing");
            if(!b->nativeHost && m->proto==10 && b->host.state==2 && !b->piaOpenedStream){
                uint16_t seq;
                if(LdnPiaReliableOpen(b->piaReliable,kLdnPiaMetadataFrame,sizeof(kLdnPiaMetadataFrame),b->now,&seq)){
                    b->piaOpenedStream=true;b->reliableUnsent[seq%128]=true;
                }
            }
        }else LdnPiaConnectOnMessage(&b->piaConn,m->proto,m->payload,m->payloadLength);
        if (m->proto!=LDN_PIA_PROTO_RELIABLE || (!b->piaOpenedStream && !(b->nativeHost && b->host.state==2))) continue;
        struct LdnPiaReliableFrame frame;
        if (!LdnPiaParseReliableFrame(m->payload,m->payloadLength,&frame)) continue;
        relayTrace(b->rfu,"rx reliable flags=%02x seq=%04x ack=%04x length=%zu",frame.flagsA,frame.seq,frame.ack,frame.payloadLength);
        struct LdnPiaReliableEntry delivered[128];
        size_t nd=LdnPiaReliableReceive(b->piaReliable,&frame,b->now,delivered,128);
        for (size_t d=0;d<nd;++d) {
            if(b->nativeHost && !b->nativeConnectSeen && delivered[d].length==6 &&
               delivered[d].payload[0]==kGbaMarker && delivered[d].payload[1]==kGbaC){
                b->nativeConnectSeen=true;b->nativeConnectNext=(uint16_t)(delivered[d].seq+1);
            }
            if (!(delivered[d].flagsA & LDN_PIA_FLAGSA_INITIALIZED) ||
                (delivered[d].length && delivered[d].payload[0]==kGbaMarker))
                _gbaReceive(b,delivered[d].payload,delivered[d].length);
        }
    }
}
static void _labTick(struct IOSRelay *,uint32_t);
bool IOSRelayCanAdvanceFrame(struct GBASIORFUBackend *backend) {
    struct IOSRelay *b=(struct IOSRelay *)backend;
    /* Distinct game frames must not be dropped. Limit unsent work at its
     * producer instead of letting a 60Hz emulator fill the 64-slot FIFO when
     * reliable delivery is slower. Leave ample room for a single-frame burst. */
    if(b && b->piaActive && b->nativeHost && b->piaAccepted && b->piaOutCount>=4){
        ++b->nativeHeldFrames;return false;
    }
    return true;
}
void IOSRelayTick(struct GBASIORFUBackend *backend, uint32_t now) {
    struct IOSRelay *b=(struct IOSRelay *)backend;
    if (!b || !b->piaActive) return;
    b->now=now;
    if (b->failed || now-b->lastReceive>15000 || (b->requested && !b->notified && now-b->requestedAt>15000)) {
        b->log(b->user,"Relay session stopped: timeout or queue failure; leave and rejoin LDN");
        IOSRelayStop(backend); return;
    }
    if(b->labMode){_labTick(b,now);return;}
    if (b->searching && now-b->lastBeacon>=500) {
        uint32_t words[6]; LdnBeaconToBroadcastWords(&b->beacon,0x13820002,4,words);
        GBASIORFUBroadcastReceived(b->rfu,b->beacon.trainerId,0,words); b->lastBeacon=now;
    }
    if (b->piaConn.haveHostVar) b->piaHostVar=b->piaConn.hostVar;
    if (b->lastState!=b->piaConn.state) {
        b->lastState=b->piaConn.state;
        b->log(b->user,b->lastState==2 ? "Pia handshake connected" : "Pia session finalizing");
    }
    LdnPiaConnectTick(&b->piaConn,b->piaTick++ / 6); // Originate RTT at 1 Hz; still answer every host probe.
    if (b->requested && LdnPiaConnectIsConnected(&b->piaConn) && !b->piaOpenedStream) {
        uint16_t seq;
        if (LdnPiaReliableOpen(b->piaReliable,kLdnPiaMetadataFrame,sizeof(kLdnPiaMetadataFrame),now,&seq)) {
            b->piaOpenedStream=true;
            b->reliableUnsent[seq % LDN_PIA_RELIABLE_MAX_INFLIGHT]=true;
        }
    } else if (b->piaOpenedStream && !b->piaConnectQueued) _gbaSendConnect(b);
    _gbaFlushAcks(b);
    if (now-b->sizeLogAt>=5000) {
        b->sizeLogAt=now;char sizeText[640];
        snprintf(sizeText,sizeof(sizeText),
            "PIA sizes cumulative: tx=%u compressed=%llu raw=%llu packed=%llu wire=%llu rx_authenticated=%u rx_wire=%llu bins_le=64,96,128,192,256,larger tx_bins=%u,%u,%u,%u,%u,%u rx_bins=%u,%u,%u,%u,%u,%u",
            b->transmitted,(unsigned long long)b->sizes.compressed,(unsigned long long)b->sizes.raw,
            (unsigned long long)b->sizes.packed,(unsigned long long)b->sizes.wire,b->decrypted,(unsigned long long)b->sizes.rxWire,
            b->sizes.txBins[0],b->sizes.txBins[1],b->sizes.txBins[2],b->sizes.txBins[3],b->sizes.txBins[4],b->sizes.txBins[5],
            b->sizes.rxBins[0],b->sizes.rxBins[1],b->sizes.rxBins[2],b->sizes.rxBins[3],b->sizes.rxBins[4],b->sizes.rxBins[5]);
        b->log(b->user,sizeText);
    }
    if (b->piaOpenedStream && now-b->windowLogAt>=1000) {
        b->windowLogAt=now;char text[160];
        snprintf(text,sizeof(text),"Reliable window: occupied=%u/128 pending_K=%u child=%d",
                 (unsigned)(uint16_t)(b->piaReliable->outSeq-b->piaReliable->windowLo),b->pendingKCount,b->piaOutCount);
        b->log(b->user,text);
    }
    if (b->piaAccepted) {
        LdnTradeShimPoll(&b->piaShim,now);
        uint8_t child[LDN_TRADE_SHIM_CHILD_FRAME], parent[LDN_TRADE_SHIM_HOST_FRAME];
        size_t n=LdnTradeShimInject(&b->piaShim,now,child,sizeof(child));
        if (n) _gbaSendChildSlot(b,child,n);
        n=LdnTradeShimHostInject(&b->piaShim,now,parent,sizeof(parent));
        if (n) GBASIORFUDataReceived(b->rfu,0,parent,n);
        _gbaPumpChild(b);
    }
    _reliablePump(b);
    while (b->canSend(b->user)) {
        struct LdnPiaOutMessage out;
        if (!LdnPiaConnectDrain(&b->piaConn,&out,1)) break;
        if (!_piaSendMessage(b,&out)) break;
    }
}

#include "relay-lab.inc"

#include "native-host.inc"
