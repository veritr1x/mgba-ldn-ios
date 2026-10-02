/* MPL-2.0. Retail-ROM host/child laboratory on the existing Pia wire transport.
 * The Switch-specific post-trade shim and child tag rewriting are bypassed only
 * in this explicit mode; both emulated games own their original RFU slots. */
void IOSRelaySetLabHost(struct GBASIORFUBackend *backend,bool enabled){
    struct IOSRelay *b=(struct IOSRelay *)backend;if(!b)return;
    b->labHost=enabled;b->labMode=enabled;LdnRandomBytes(b->labSSID,16);
}
bool IOSRelayLabAdvertisement(struct GBASIORFUBackend *backend,uint8_t out[47]){
    struct IOSRelay *b=(struct IOSRelay *)backend;
    if(!b || !b->labHost || (!b->labAdvertising && !b->piaAccepted))return false;
    memcpy(out,"MGL1",4);memcpy(out+4,b->labSSID,16);lr_put16(out+20,b->labDeviceId);
    out[22]=b->labNextSlot;for(unsigned i=0;i<6;i++)lr_put32(out+23+i*4,b->labWords[i]);return true;
}
void IOSRelayUpdateLabAdvertisement(struct GBASIORFUBackend *backend,const uint8_t *p,size_t n){
    struct IOSRelay *b=(struct IOSRelay *)backend;if(!b || !b->labMode || n!=47 || memcmp(p,"MGL1",4) || memcmp(p+4,b->labSSID,16))return;
    b->labDeviceId=lr_get16(p+20);b->labNextSlot=p[22];
    for(unsigned i=0;i<6;i++)b->labWords[i]=lr_get32(p+23+i*4);
}
bool IOSRelayConfigureLab(struct GBASIORFUBackend *backend,const uint8_t *p,size_t n,bool host,uint32_t now){
    struct IOSRelay *b=(struct IOSRelay *)backend;if(!b || !b->rfu || !p || n!=47 || memcmp(p,"MGL1",4) || !lr_get16(p+20))return false;
    IOSRelayStop(backend);b->labMode=true;b->labHost=host;memcpy(b->labSSID,p+4,16);
    IOSRelayUpdateLabAdvertisement(backend,p,n);
    b->now=b->started=b->lastReceive=now;b->lastBeacon=now-1000;b->failed=false;b->piaActive=true;
    b->beacon.trainerId=b->labDeviceId;
    PiaHostInit(&b->host,b->labSSID);
    memcpy(b->piaOurIp,host?b->host.ip:b->host.peerIp,4);memcpy(b->piaHostIp,host?b->host.peerIp:b->host.ip,4);
    memcpy(b->piaOurMac,host?b->host.mac:b->host.peerMac,6);memcpy(b->piaHostMac,host?b->host.peerMac:b->host.mac,6);
    LdnPiaCryptoInit(&b->piaCrypto,b->labSSID);
    LdnPiaConnectInit(&b->piaConn,b->piaOurMac,b->piaHostMac,b->piaOurIp,"iPhone");
    if(host){b->piaConn.ourVar=b->host.var;b->piaConn.hostVar=b->host.peerVar;b->piaConn.haveHostVar=true;}
    b->piaHostVar=host?b->host.peerVar:b->host.var;
    LdnPiaReliableInit(b->piaReliable,33,1000);memset(b->reliableUnsent,0,sizeof(b->reliableUnsent));memset(&b->piaPktids,0,sizeof(b->piaPktids));
    LdnRandomBytes((uint8_t *)&b->piaNonceCounter,8);b->piaNonceCounter>>=1;
    b->piaConnectId=(uint16_t)arc4random_uniform(65535)+1;b->piaTs=b->piaKSeq=b->piaTick=0;
    if(!b->nativeHost)b->log(b->user,host?"LAB: Mac Pia/RFU host active; retail-ROM pair, not a Switch host":"LAB: joining emulator host; Switch trade shim disabled");return true;
}
static void _labHostReceive(struct IOSRelay *b,const uint8_t *p,size_t n){
    while(n>=4 && p[0]==0x57){size_t length=lr_get16(p+2);if(length+4>n)return;
        const uint8_t *body=p+4;
        if(p[1]=='C' && length==2 && !b->piaAccepted && !b->labConnectPending){
            b->labClientId=lr_get16(body);if(!b->labClientId)return;b->labConnectPending=true;
            GBASIORFUConnectRequested(b->rfu,b->labClientId);
        }else if(p[1]=='T' && b->piaAccepted && length>=8){
            size_t bytes=body[5];if(body[4] || body[6] || body[7] || bytes>RFU_PACKET_MAX || bytes+8>length)return;
            if(bytes){
                uint8_t mapped[RFU_PACKET_MAX],reply[73];memcpy(mapped,body+8,bytes);
                if(b->nativeHost && !HostTradeChild(&b->hostTrade,mapped,bytes,reply)){
                    _queueLabData(b,reply,sizeof(reply));
                    char text[128];snprintf(text,sizeof(text),"Native trade: answered Switch-only save barrier %u (reply %u)",
                        htsRead(reply+5),b->hostTrade.replies);b->log(b->user,text);
                }else GBASIORFUDataReceived(b->rfu,0,mapped,bytes);
            }
        }else if(p[1]=='K' && length==12 && b->piaAccepted){
            GBASIORFUDataReceived(b->rfu,0,body,0); /* Adapter liveness without a queued RFU payload. */
        }else if(p[1]=='D'){GBASIORFUDisconnected(b->rfu,0);b->failed=true;}
        p+=4+length;n-=4+length;
    }
}
static void _labPumpSlot(struct IOSRelay *b){
    /* Native RFU reception has a small queue. Bound the whole unacknowledged
     * host window so a BLE delay cannot release dozens of polls in a burst. */
    unsigned limit=b->nativeHost?6:64;
    if(!b->piaAccepted || (uint16_t)(b->piaReliable->outSeq-b->piaReliable->windowLo)>=limit)return;
    if(!b->labHost && (b->pendingKCount || b->piaCredits<=0))return;
    if(!b->labHost && !b->piaOutCount)return;
    unsigned at=b->piaOutHead;size_t length=b->piaOutCount?b->piaOut[at].length:0;
    if(b->labHost){
        uint8_t frame[4+8+RFU_PACKET_MAX]={0x57,'T'};size_t padded=(length+3)&~(size_t)3;
        lr_put16(frame+2,8+padded);lr_put32(frame+4,++b->piaTs);frame[8]=(b->nativeHost && !length)?1:length;
        if(length)memcpy(frame+12,b->piaOut[at].data,length);
        if(!_reliableQueue(b,frame,12+padded))return;
    }else{_gbaSendSlot(b,b->piaOut[at].data,length);--b->piaCredits;}
    if(b->piaOutCount){b->piaOutHead=(b->piaOutHead+1)%64;--b->piaOutCount;}
}
static void _labTick(struct IOSRelay *b,uint32_t now){
    if(b->labHost){
        PiaHostTick(&b->host,now);
        while(b->canSend(b->user)){struct LdnPiaOutMessage out;if(!PiaHostDrain(&b->host,&out))break;if(!_piaSendMessage(b,&out))break;}
        if(b->nativeHost && !b->piaOpenedStream){
            /* A native leader ACKs the child's C before opening its own stream
             * with A (not with the joiner's game-metadata INIT). */
            _reliablePump(b);
            if(b->nativeAcceptPending && b->nativeConnectAcked && b->canSend(b->user)){
                uint8_t a[10]={0x57,'A',6,0};uint16_t seq;
                lr_put16(a+4,b->labDeviceId);lr_put16(a+6,b->labClientId);
                if(LdnPiaReliableOpen(b->piaReliable,a,sizeof(a),now,&seq)){
                    b->piaOpenedStream=b->piaAccepted=true;b->nativeAcceptPending=false;
                    b->reliableUnsent[seq%128]=true;b->labNextSlot=255;
                    b->log(b->user,"Native host opened RFU acceptance after acknowledging child connect");
                }
            }
        }
    }else{
        if(b->searching && now-b->lastBeacon>=500){GBASIORFUBroadcastReceived(b->rfu,b->labDeviceId,b->labNextSlot,b->labWords);b->lastBeacon=now;}
        if(b->piaConn.haveHostVar)b->piaHostVar=b->piaConn.hostVar;
        LdnPiaConnectTick(&b->piaConn,b->piaTick++/6);
        if(b->requested && LdnPiaConnectIsConnected(&b->piaConn) && !b->piaOpenedStream){
            uint16_t seq;if(LdnPiaReliableOpen(b->piaReliable,kLdnPiaMetadataFrame,sizeof(kLdnPiaMetadataFrame),now,&seq)){
                b->piaOpenedStream=true;b->reliableUnsent[seq%128]=true;
            }
        }else if(b->piaOpenedStream && !b->piaConnectQueued)_gbaSendConnect(b);
        _gbaFlushAcks(b);
        while(b->canSend(b->user)){struct LdnPiaOutMessage out;if(!LdnPiaConnectDrain(&b->piaConn,&out,1))break;if(!_piaSendMessage(b,&out))break;}
    }
    _labPumpSlot(b);_reliablePump(b);
    if(b->nativeHost && b->piaAccepted && now-b->windowLogAt>=1000){
        b->windowLogAt=now;char text[180];
        snprintf(text,sizeof(text),"Native host RFU: queued=%d peak=%u coalesced=%u reliable=%u/6 held_frames=%u",
            b->piaOutCount,b->nativeRfuQueuePeak,b->nativeRfuCoalesced,
            (unsigned)(uint16_t)(b->piaReliable->outSeq-b->piaReliable->windowLo),b->nativeHeldFrames);
        b->log(b->user,text);
    }
}
