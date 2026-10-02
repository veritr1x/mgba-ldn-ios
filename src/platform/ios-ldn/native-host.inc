#include "native-host.h"
void IOSRelayEnableNativeHost(struct GBASIORFUBackend *backend,bool enabled){
    struct IOSRelay *b=(struct IOSRelay *)backend;if(b)b->nativeHostEnabled=enabled;
}
bool IOSRelayNativeAdvertisement(struct GBASIORFUBackend *backend,uint8_t out[122]){
    struct IOSRelay *b=(struct IOSRelay *)backend;
    return b && b->nativeHost && (b->labAdvertising || b->piaAccepted) && IOSNativeAdvertisement(b->labWords,b->labDeviceId,b->piaAccepted,out);
}
bool IOSRelayConfigureNativeHost(struct GBASIORFUBackend *backend,const uint8_t *p,size_t n,uint32_t now){
    struct IOSRelay *b=(struct IOSRelay *)backend;
    if(!b || !b->rfu || !b->nativeHost || !p || n<74 || (p[0]!=RL_HOSTED && p[0]!=RL_MEMBERS) ||
       p[3]!=3 || p[22]!=32 || lr_get64(p+12)!=UINT64_C(0x01006fa0233f8000))return false;
    uint8_t ssid[16];for(unsigned i=0;i<16;i++){int hi=_hex(p[23+2*i]),lo=_hex(p[24+2*i]);if(hi<0 || lo<0)return false;ssid[i]=hi*16+lo;}
    size_t at=73;unsigned ad=lr_get16(p+71);if(ad>384 || at+ad+1>n)return false;
    at+=ad;unsigned count=p[at++];if(!count || count>2 || n!=at+15*count)return false;
    const uint8_t *us=NULL,*peer=NULL;
    for(unsigned i=0;i<count;i++){const uint8_t *node=p+at+15*i;
        if(!node[11])continue;
        if(!node[10] && !memcmp(node,p+4,4))us=node;else if(node[10]==1)peer=node;else return false;
    }
    if(!us)return false;
    if(!peer){if(b->piaActive)IOSRelayStop(backend);b->log(b->user,"Native host room ready; waiting for a Switch guest");return true;}
    if(b->piaActive && !memcmp(b->piaHostIp,peer,4) && !memcmp(b->piaHostMac,peer+4,6) && !memcmp(b->labSSID,ssid,16))return true;
    uint8_t metadata[47];if(!IOSRelayLabAdvertisement(backend,metadata))return false;memcpy(metadata+4,ssid,16);
    if(!IOSRelayConfigureLab(backend,metadata,sizeof(metadata),true,now))return false;
    memcpy(b->host.ip,us,4);memcpy(b->host.mac,us+4,6);memcpy(b->host.peerIp,peer,4);memcpy(b->host.peerMac,peer+4,6);
    /* Pia constant IDs reorder MAC bytes; they are not raw LDN MACs.
       The guest's own Pia ID is learned from its authenticated Session join. */
    const unsigned order[6]={2,4,5,3,1,0};
    for(unsigned i=0;i<6;i++)b->host.mac[i]=us[4+order[i]];
    /* Retail FRLG advertises six Pia station slots even for a two-player trade. */
    b->host.native=true;b->host.maxStations=6;b->host.learnPeerVar=true;
    for(unsigned i=0;i<4;i++)b->piaBroadcastIp[i]=us[i]|(uint8_t)~p[8+i];
    memcpy(b->piaOurIp,us,4);memcpy(b->piaOurMac,us+4,6);memcpy(b->piaHostIp,peer,4);memcpy(b->piaHostMac,peer+4,6);
    LdnPiaConnectInit(&b->piaConn,b->piaOurMac,b->piaHostMac,b->piaOurIp,"iPhone");b->piaConn.ourVar=b->host.var;
    b->log(b->user,"Native host peer metadata accepted; starting encrypted Pia handshake");return true;
}
