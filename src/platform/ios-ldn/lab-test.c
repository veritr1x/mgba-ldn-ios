/* MPL-2.0. Encrypted host/joiner integration without Bluetooth or a ROM. */
#include "relay-backend.h"
#include "pia-host.h"
#include "ldn-pia.h"
#include "ldn-pia-reliable.h"
#include "native-host.h"
#include "relay/relay_protocol.h"
#include "lab-wire-fixtures.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct Peer {struct GBASIORFU rfu;struct GBASIORFUBackend *backend;unsigned sent,data,accepted,beacons,failed;uint8_t last[96];size_t length;};
static struct Peer peers[2];
static bool recordHostSlots;
static struct {uint8_t bytes[96];size_t length;} receivedSlots[8192];
static unsigned receivedCount;
static bool strictNative;static unsigned checkedProbes, broadcastProbes, nativeInit;
static bool nativeConnectAckSent;
static uint8_t peerIps[2][4]={{169,254,1,1},{169,254,1,2}};
struct Packet{uint8_t ip[4],data[1400];size_t n;unsigned to;};
static struct Packet packets[1024];static unsigned head,count;static bool writable=true;
static bool sendPacket(void *ctx,const uint8_t ip[4],const uint8_t *p,size_t n){
    struct Peer *a=ctx;unsigned to=a==peers?1:0;
    if(strictNative && to==1){
        struct LdnPiaHeader header;LdnPiaHeaderUnpack(p,&header);
        uint8_t ssid[16];for(unsigned i=0;i<16;i++)ssid[i]=i;
        struct LdnPiaCrypto crypto;LdnPiaCryptoInit(&crypto,ssid);
        uint8_t plain[1400],decoded[8192];size_t plainSize=0,decodedSize=sizeof(decoded),consumed=0;
        assert(LdnPiaDecrypt(&crypto,p,n,peerIps[0],plain,&plainSize));assert(LdnPiaDecompress(plain,plainSize,decoded,&decodedSize));
        struct LdnPiaMessage messages[8];size_t num=LdnPiaParseMessages(decoded,decodedSize,messages,8,&consumed);assert(num<=8);
        for(size_t i=0;i<num;i++)if(messages[i].proto==10){
            /* The permissive emulated peer does not filter header destinations;
             * retail does. The failed hardware capture sent these to zero. */
            assert(header.dst==LDN_PIA_DEFAULT_OUR_VAR && header.src==0x7620);
            struct LdnPiaReliableFrame f;assert(LdnPiaParseReliableFrame(messages[i].payload,messages[i].payloadLength,&f));
            if(!(f.flagsA&LDN_PIA_FLAGSA_APP_DATA)){
                uint16_t next;uint8_t mask[16];assert(LdnPiaParseBulkAck(f.payload,f.payloadLength,&next,mask));
                if((uint16_t)(next-0xfff2)<0x8000)nativeConnectAckSent=true;
            }else if(f.flagsA&LDN_PIA_FLAGSA_INITIALIZED){
                assert(nativeConnectAckSent && f.seq==0xfff0 && f.payloadLength==10);
                assert(!memcmp(f.payload,(uint8_t[]){0x57,'A',6,0,0x34,0x12},6));++nativeInit;
            }
        }
        for(size_t i=0;i<num;i++)if(messages[i].proto==1 && messages[i].payloadLength>1 && messages[i].payload[1]==0x11){
            const uint8_t *net=messages[i].payload;
            assert(header.dst==0 && header.src==0x7620 && header.pktid==0 && (header.flags&2) && !header.footer);
            assert(messages[i].payloadLength==162 && net[2]==0 && net[3]==132 && net[27]==0 && net[28]==6);
            assert(!memcmp(net+10,(uint8_t[]){0x30,0x50,0x60,0x40,0x20,0x10,0,0},8));
            assert(!memcmp(net+18,(uint8_t[]){0,0,0,0,0xf5,0xa6,0xaa,0x3a},8));
            assert(!memcmp(net+34,peerIps[0],4) && !memcmp(net+56,peerIps[1],4));checkedProbes++;
            /* Native capture has four unused 22-byte stations with rank 0xff. */
            for(unsigned j=2;j<6;j++)for(unsigned k=0;k<22;k++)assert(net[30+22*j+k]==(k==1?255:0));
            if(ip[3]==255)++broadcastProbes;
        }
    }
assert(!memcmp(ip,peerIps[to],4) || (strictNative && to==1 && !memcmp(ip,peerIps[0],3) && ip[3]==255));assert(count<1024 && n<=1400);
    struct Packet *q=&packets[(head+count++)%1024];q->to=to;q->n=n;memcpy(q->data,p,n);memcpy(q->ip,peerIps[1-to],4);a->sent++;return true;
}
static bool canSend(void *ctx){(void)ctx;return writable;}
static void logMessage(void *ctx,const char *s){struct Peer *p=ctx;if(strstr(s,"overflow")||strstr(s,"failed")||strstr(s,"stopped")){p->failed++;puts(s);}}
void GBASIORFUConnectRequested(struct GBASIORFU *r,uint16_t id){r->backend->connectReply(r->backend,id,true,0);}
void GBASIORFUConnectResult(struct GBASIORFU *r,bool ok,uint16_t id,unsigned slot){(void)id;assert(slot==0);if(ok)((struct Peer *)r)->accepted++;}
void GBASIORFUDisconnected(struct GBASIORFU *r,int slot){(void)r;(void)slot;}
void GBASIORFUDataReceived(struct GBASIORFU *r,unsigned slot,const uint8_t *p,size_t n){struct Peer *peer=(struct Peer *)r;assert(slot==0 && n<=96);if(!n)return;
    if(recordHostSlots && peer==&peers[1] && !(n==1 && !p[0])){
        assert(receivedCount<8192);memcpy(receivedSlots[receivedCount].bytes,p,n);receivedSlots[receivedCount++].length=n;
    }
    peer->data++;memcpy(peer->last,p,n);peer->length=n;
}
void GBASIORFUBroadcastReceived(struct GBASIORFU *r,uint16_t id,uint8_t slot,const uint32_t words[6]){assert(id==0x1234 && slot<=255 && words[0]==0x13820002);((struct Peer *)r)->beacons++;}
static void step(unsigned now){
    IOSRelayTick(peers[0].backend,now);IOSRelayTick(peers[1].backend,now);
    while(count){struct Packet q=packets[head];head=(head+1)%1024;count--;IOSRelayReceive(peers[q.to].backend,q.ip,q.data,q.n);}
}
static void wireTest(void){
    struct PiaHost h;uint8_t ssid[16];for(unsigned i=0;i<16;i++)ssid[i]=i;
    PiaHostInit(&h,ssid);struct LdnPiaOutMessage m;
    PiaHostTick(&h,1);assert(PiaHostDrain(&h,&m));assert(m.length==sizeof(wire_net) && !memcmp(m.payload,wire_net,m.length));
    for(size_t n=0;n<sizeof(wire_join);n++)assert(!PiaHostReceive(&h,13,wire_join,n));
    assert(h.state==0 && !h.count);
    assert(PiaHostReceive(&h,13,wire_join,sizeof(wire_join)));assert(h.state==1);
    assert(PiaHostDrain(&h,&m));assert(m.length==sizeof(wire_response) && !memcmp(m.payload,wire_response,m.length));
    assert(PiaHostDrain(&h,&m));assert(m.length==sizeof(wire_update) && !memcmp(m.payload,wire_update,m.length));
    /* Lose the first update; the host retries rather than declaring connected. */
    PiaHostTick(&h,600);assert(PiaHostDrain(&h,&m));assert(m.length==sizeof(wire_update) && !memcmp(m.payload,wire_update,m.length));assert(h.state==1);
    uint8_t fin[15]={6,2,0,0,0,0,2,0,0,0,0,0,0,0,1};
    fin[1]^=1;assert(!PiaHostReceive(&h,13,fin,15));fin[1]^=1;
    assert(PiaHostReceive(&h,13,fin,15));assert(h.state==2);
    assert(PiaHostReceive(&h,13,wire_join,sizeof(wire_join)));assert(h.state==2 && !h.count);
    puts("PASS independent golden Net/Session bytes, truncated joins, retry, wrong identity and no state rewind");
}
static void nativeAdvertisementTest(void){
    uint32_t words[6]={0x12a20002,0x22111234,0x04034433,0x00002584,0xbebdbcbb,0xffffffff};
    uint8_t ad[122],rec[24];assert(IOSNativeAdvertisement(words,0xabcd,true,ad));
    assert(ad[1]==92 && ad[2]==22 && ad[4]==88 && ad[22]==2 && !memcmp(ad+28,"iPhone",6));
    for(unsigned g=0;g<6;g++){uint64_t value=0;for(int i=4;i>=0;i--){unsigned c=ad[92+5*g+i];assert(c>=0x23 && c<=0x78 && c!=0x5c);value=value*85+(c<0x5c?c-0x23:c-0x24);}assert(value<=UINT32_MAX);lr_put32(rec+g*4,value);}
    assert(lr_get16(rec)==0x1234 && lr_get16(rec+10)==0xabcd);
    assert(!memcmp(rec+2,(uint8_t[]){0xbb,0xbc,0xbd,0xbe,255,255,255,255},8));
    assert(!memcmp(rec+12,(uint8_t[]){0x11,0x22,0x33,0x44},4));
    assert(lr_get16(rec+16)==0xd404 && rec[18]==4 && rec[19]==0x25 && lr_get16(rec+22)==3);
    words[0]=1;assert(!IOSNativeAdvertisement(words,1,false,ad));
    puts("PASS native RFU advertisement: live trainer/name/session/activity/language/version/partner/trade fields");
}
static void nativeIdentityTest(void){
    struct PiaHost h;uint8_t ssid[16]={0},join[sizeof(wire_join)];PiaHostInit(&h,ssid);
    h.learnPeerVar=true;memcpy(h.ip,(uint8_t[]){10,9,8,7},4);memcpy(h.peerIp,(uint8_t[]){10,9,8,8},4);
    memcpy(h.mac,(uint8_t[]){6,5,4,3,2,1},6);memcpy(h.peerMac,(uint8_t[]){6,5,4,3,2,2},6);
    memcpy(join,wire_join,sizeof(join));memcpy(join+20,h.peerMac,6);memcpy(join+64,h.mac,6);memcpy(join+77,h.peerIp,4);join[28]=0x51;join[29]=0x72;
    join[77]^=1;assert(!PiaHostReceive(&h,13,join,sizeof(join)));assert(h.learnPeerVar);join[77]^=1;
    assert(PiaHostReceive(&h,13,join,sizeof(join)));assert(h.peerVar==0x5172 && !h.learnPeerVar);
    join[29]^=1;assert(!PiaHostReceive(&h,13,join,sizeof(join)));assert(h.peerVar==0x5172);
    puts("PASS native host real IP/MAC binding and authenticated peer-variable learning/locking");
}
static void nativePairTest(void){
    memset(peers,0,sizeof(peers));head=count=0;strictNative=true;
    memcpy(peerIps,(uint8_t[][4]){{169,254,31,41},{169,254,31,42}},sizeof(peerIps));
    for(unsigned i=0;i<2;i++){peers[i].backend=IOSRelayCreate(sendPacket,canSend,logMessage,&peers[i]);assert(peers[i].backend);peers[i].rfu.backend=peers[i].backend;assert(peers[i].backend->init(peers[i].backend,&peers[i].rfu));}
    struct GBASIORFUBackend *h=peers[0].backend,*c=peers[1].backend;
    IOSRelayEnableNativeHost(h,true);
    uint32_t words[6]={0x13820002,0x1234,0,4,0xffffffff,0xffffffff};h->setBroadcast(h,words);h->hostStart(h,0x1234);
    uint8_t meta[226]={RL_HOSTED};meta[3]=3;memcpy(meta+4,peerIps[0],4);memset(meta+8,255,3);lr_put64(meta+12,UINT64_C(0x01006fa0233f8000));meta[22]=32;
    memcpy(meta+23,"000102030405060708090a0b0c0d0e0f",32);lr_put16(meta+71,122);assert(IOSRelayNativeAdvertisement(h,meta+73));meta[195]=1;
    for(unsigned i=0;i<2;i++){uint8_t *node=meta+196+15*i;memcpy(node,peerIps[i],4);for(unsigned j=0;j<6;j++)node[4+j]=(i?0x70:0x10)+j*0x10;node[10]=i;node[11]=1;lr_put16(node+12,88);}
    for(size_t n=0;n<211;n++)assert(!IOSRelayConfigureNativeHost(h,meta,n,1));
    assert(IOSRelayConfigureNativeHost(h,meta,211,1));
    IOSRelayTick(h,20000);assert(!peers[0].failed); /* Waiting for a guest has no timeout. */
    meta[0]=RL_MEMBERS;meta[195]=2;assert(IOSRelayConfigureNativeHost(h,meta,sizeof(meta),20001));
    meta[0]=RL_CONNECTED;memcpy(meta+4,peerIps[1],4);assert(IOSRelayConfigure(c,meta,sizeof(meta),20001));
    c->searchStart(c);for(unsigned t=20001;t<21000;t+=17)step(t);c->connect(c,0x1234);
    for(unsigned t=21000;t<23000;t+=17)step(t);
    assert(peers[1].accepted==1 && checkedProbes>0 && broadcastProbes>0 && nativeInit==1 && !peers[0].failed && !peers[1].failed);
    puts("PASS native reliable destination, C acknowledged before A INIT, advertised host ID and one stream open");
    puts("PASS strict encrypted native Net probe: source ID, 15-byte SSID CRC, permuted constant ID, six-station native table, broadcast delivery and source-IP authentication");
    meta[0]=RL_MEMBERS;memcpy(meta+4,peerIps[0],4);assert(IOSRelayConfigureNativeHost(h,meta,sizeof(meta),23000));
    for(unsigned t=23000;t<24000;t+=17)step(t);assert(peers[1].accepted==1);
    /* BLE accepts writes but the guest stops returning ACKs. A host must stop
     * fresh polls at six outstanding frames, including across sequence wrap.
     * Keep the stall below the 500ms minimum retry timer. */
    unsigned queuedBefore=count;
    for(unsigned t=24000;t<24400;t+=17)IOSRelayTick(h,t);
    unsigned freshPolls=0;
    for(unsigned j=queuedBefore;j<count;j++){
        struct Packet *q=&packets[(head+j)%1024];
        struct LdnPiaCrypto crypto;uint8_t ssid[16],plain[1400],decoded[8192];
        for(unsigned k=0;k<16;k++)ssid[k]=k;LdnPiaCryptoInit(&crypto,ssid);
        size_t size=0,decodedSize=sizeof(decoded),consumed=0;
        assert(LdnPiaDecrypt(&crypto,q->data,q->n,peerIps[0],plain,&size));
        assert(LdnPiaDecompress(plain,size,decoded,&decodedSize));
        struct LdnPiaMessage messages[32];size_t num=LdnPiaParseMessages(decoded,decodedSize,messages,32,&consumed);assert(num<=32);
        for(size_t k=0;k<num;k++)if(messages[k].proto==10){
            struct LdnPiaReliableFrame f;assert(LdnPiaParseReliableFrame(messages[k].payload,messages[k].payloadLength,&f));
            if(f.payloadLength>=2 && f.payload[0]==0x57 && f.payload[1]=='T')++freshPolls;
        }
    }
    assert(freshPolls>0 && freshPolls<=6);
    for(unsigned t=24408;t<25000;t+=17)step(t);
    assert(!peers[0].failed && !peers[1].failed);
    puts("PASS native host stops at six outstanding polls without ACKs and resumes after the stalled peer drains");
    /* Reproduce the hardware producer outrunning a six-frame reliable window.
     * Only adjacent, still-pending copies may merge: A,B,A must stay A,B,A. */
    recordHostSlots=true;receivedCount=0;
    uint8_t a[73]={0x46,0,5},b[73]={0x46,0,5};b[3]=0x88;
    for(unsigned i=0;i<200;i++)h->sendData(h,a,sizeof(a));
    h->sendData(h,b,sizeof(b));h->sendData(h,a,sizeof(a));
    assert(!peers[0].failed);
    for(unsigned t=25000;t<27000;t+=17)step(t);
    assert(receivedCount==3);
    assert(!memcmp(receivedSlots[0].bytes,a,73) && !memcmp(receivedSlots[1].bytes,b,73) && !memcmp(receivedSlots[2].bytes,a,73));
    /* A repeat after draining is fresh traffic, not globally deduplicated. */
    h->sendData(h,a,sizeof(a));
    for(unsigned t=27000;t<27500;t+=17)step(t);
    assert(receivedCount==4 && receivedSlots[3].length==73);
    /* Fill the FIFO with distinct records. A duplicate final entry must merge
     * before the capacity check. Repeat to cross the ring boundary. */
    for(unsigned round=0;round<2;round++){
        receivedCount=0;
        for(unsigned i=0;i<64;i++){uint8_t slot[4]={0x00,0x68,0x04,i};h->sendData(h,slot,sizeof(slot));}
        const uint8_t tail[4]={0x00,0x68,0x04,63};
        for(unsigned i=0;i<200;i++)h->sendData(h,tail,sizeof(tail));
        assert(!peers[0].failed);
        for(unsigned t=27500+round*2000;t<29500+round*2000;t+=17)step(t);
        assert(receivedCount==64);
        for(unsigned i=0;i<64;i++)assert(receivedSlots[i].length==4 && receivedSlots[i].bytes[3]==i);
    }
    /* Sixty seconds with a producer at ~60Hz and delayed delivery/ACKs every
     * 102ms. Every produced payload changes, so duplicate merging cannot help.
     * Service the protocol even when the emulated frame must be held. */
    receivedCount=0;unsigned produced=0,held=0;
    for(unsigned i=0,t=31500;t<91500;t+=17,i++){
        IOSRelayTick(h,t);
        if(IOSRelayCanAdvanceFrame(h)){
            uint8_t slot[4]={0x46,0x05};lr_put16(slot+2,produced++);
            h->sendData(h,slot,sizeof(slot));
        }else ++held;
        if(i%6==5){
            IOSRelayTick(c,t);
            while(count){struct Packet q=packets[head];head=(head+1)%1024;count--;IOSRelayReceive(peers[q.to].backend,q.ip,q.data,q.n);}
        }
        assert(!peers[0].failed && !peers[1].failed);
    }
    for(unsigned t=91500;t<92500;t+=17)step(t);
    assert(held>0 && produced>1000 && receivedCount==produced);
    for(unsigned i=0;i<produced;i++)assert(receivedSlots[i].length==4 && lr_get16(receivedSlots[i].bytes+2)==i);
    assert(IOSRelayCanAdvanceFrame(h));recordHostSlots=false;
    printf("PASS 60s changing-frame backpressure: %u delivered in order, %u held emulator frames, transport continues and producer resumes\n",produced,held);
    assert(!peers[0].failed && !peers[1].failed);
    puts("PASS native pending duplicates coalesce; A-B-A, fresh repeats, all 64 distinct frames and ring wrap preserved");
    /* Exercise the shim inside the real encrypted backend, not only helpers.
     * Extra child barrier bypasses the ROM and returns a wire-ready echo. */
    uint8_t hp[73]={0x46,0,5},cp[16]={14,16,0,0x66};
    lr_put16(hp+3,0x6600);lr_put16(hp+5,4);h->sendData(h,hp,73);
    lr_put16(hp+3,0x8800);lr_put16(hp+5,2);h->sendData(h,hp,73);
    lr_put16(hp+3,0x8900);lr_put16(hp+5,0xdcba);h->sendData(h,hp,73);
    for(unsigned t=92500;t<93000;t+=17)step(t);
    unsigned hostBefore=peers[0].data;receivedCount=0;recordHostSlots=true;
    lr_put16(cp+4,7);c->sendData(c,cp,16);
    for(unsigned t=93000;t<93500;t+=17)step(t);
    assert(peers[0].data==hostBefore && receivedCount==1);
    assert(receivedSlots[0].length==73 && lr_get16(receivedSlots[0].bytes+5)==7 && lr_get16(receivedSlots[0].bytes+19)==7);
    cp[2]=32;lr_put16(cp+4,8);c->sendData(c,cp,16);
    for(unsigned t=93500;t<94000;t+=17)step(t);
    assert(peers[0].data==hostBefore+1 && lr_get16(peers[0].last+4)==7);
    receivedCount=0;lr_put16(hp+3,0x6600);lr_put16(hp+5,7);
    lr_put16(hp+17,0x6600);lr_put16(hp+19,7);h->sendData(h,hp,73);
    for(unsigned t=94000;t<94500;t+=17)step(t);
    assert(receivedCount==1 && lr_get16(receivedSlots[0].bytes+5)==8 && lr_get16(receivedSlots[0].bytes+19)==8);
    recordHostSlots=false;assert(!peers[0].failed && !peers[1].failed);
    puts("PASS encrypted native save-barrier consumption, direct wire reply and bidirectional counter translation");
    meta[195]=1;assert(IOSRelayConfigureNativeHost(h,meta,211,94500));
    assert(IOSRelayCanAdvanceFrame(h));
    puts("PASS native room wait, membership, real addresses, encrypted host/client handshake and RFU acceptance; duplicate member update preserves session");
    for(unsigned i=0;i<2;i++){peers[i].backend->deinit(peers[i].backend);free(peers[i].backend);}
}
int main(void){wireTest();nativeAdvertisementTest();nativeIdentityTest();
    for(unsigned i=0;i<2;i++){peers[i].backend=IOSRelayCreate(sendPacket,canSend,logMessage,&peers[i]);assert(peers[i].backend);peers[i].rfu.backend=peers[i].backend;assert(peers[i].backend->init(peers[i].backend,&peers[i].rfu));}
    IOSRelaySetLabHost(peers[0].backend,true);uint32_t words[6]={0x13820002,1,2,3,4,5};
    peers[0].backend->setBroadcast(peers[0].backend,words);peers[0].backend->hostStart(peers[0].backend,0x1234);
    uint8_t meta[47];assert(IOSRelayLabAdvertisement(peers[0].backend,meta));
    assert(IOSRelayConfigureLab(peers[0].backend,meta,47,true,1));assert(IOSRelayConfigureLab(peers[1].backend,meta,47,false,1));
    peers[1].backend->searchStart(peers[1].backend);
    for(unsigned t=1;t<1000;t+=17)step(t);
    assert(peers[1].beacons>0);peers[1].backend->connect(peers[1].backend,0x1234);
    for(unsigned t=1000;t<2000;t+=17)step(t);
    assert(peers[1].accepted==1);puts("PASS encrypted Net/Session/RTT/reliable handshake and RFU C/A acceptance");
    const uint8_t hostSlot[]={1,2,3,4,5,6,7},childSlot[]={8,9,10,11,12};
    peers[0].backend->sendData(peers[0].backend,hostSlot,sizeof(hostSlot));peers[1].backend->sendData(peers[1].backend,childSlot,sizeof(childSlot));
    for(unsigned t=2000;t<3000;t+=17)step(t);
    assert(peers[1].length==sizeof(hostSlot) && !memcmp(peers[1].last,hostSlot,sizeof(hostSlot)));
    assert(peers[0].length==sizeof(childSlot) && !memcmp(peers[0].last,childSlot,sizeof(childSlot)));
    puts("PASS exact bidirectional RFU slot payloads through authenticated encrypted Pia");
    writable=false;for(unsigned t=3000;t<3500;t+=17)step(t);writable=true;
    for(unsigned t=3500;t<5000;t+=17)step(t);
    assert(!peers[0].failed && !peers[1].failed);puts("PASS recovery after 500ms transport backpressure");
    for(unsigned i=0;i<2;i++){peers[i].backend->deinit(peers[i].backend);free(peers[i].backend);}nativePairTest();return 0;
}
