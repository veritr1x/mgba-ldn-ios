/* MPL-2.0. Synthetic protocol tests; contains no ROM, save, or device identifiers. */
#include "relay-backend.h"
#include "ldn.h"
#include "ldn-pia.h"
#include "ldn-pia-reliable.h"
#include "relay/relay_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned sent, accepted, disconnected, beacons;
static bool writable=true;
static unsigned queueFailures;
static uint8_t packets[64][1400];static size_t sizes[64];
static bool send(void *ctx,const uint8_t ip[4],const uint8_t *p,size_t n) {
    (void)ctx;assert(ip[3]==1);assert(n<=1400);assert(sent<64);
    memcpy(packets[sent],p,n);sizes[sent++]=n;return true;
}
static bool canSend(void *ctx) { (void)ctx;return writable; }
static void logMessage(void *ctx,const char *s) { (void)ctx;if(strstr(s,"full") || strstr(s,"overflow"))queueFailures++;if(strncmp(s,"TRACE ",6))puts(s); }
void GBASIORFUConnectResult(struct GBASIORFU *r,bool ok,uint16_t id,unsigned slot) { (void)r;(void)id;(void)slot;if(ok)++accepted; }
void GBASIORFUDisconnected(struct GBASIORFU *r,int slot) { (void)r;(void)slot;++disconnected; }
void GBASIORFUDataReceived(struct GBASIORFU *r,unsigned slot,const uint8_t *p,size_t n) { (void)r;(void)slot;(void)p;(void)n; }
void GBASIORFUBroadcastReceived(struct GBASIORFU *r,uint16_t id,uint8_t next,const uint32_t words[6]) {
    (void)r;(void)next;assert(id==0x1234);assert(words[0]==0x13820002);++beacons;
}
void GBASIORFUConnectRequested(struct GBASIORFU *r,uint16_t id) { (void)r;(void)id;assert(!"Switch join backend must not host"); }
static void cryptoTest(void) {
    const uint8_t cipherExpected[16]={0x03,0x88,0xda,0xce,0x60,0xb6,0xa3,0x92,0xf3,0x28,0xc2,0xb9,0x71,0xb2,0xfe,0x78};
    const uint8_t tagExpected[16]={0xab,0x6e,0x47,0xd4,0x2c,0xec,0x13,0xbd,0xf5,0x3a,0x67,0xb2,0x12,0x57,0xbd,0xdf};
    uint8_t key[16]={0},nonce[12]={0},plain[16]={0},cipher[16],tag[16],out[16];
    assert(LdnAesGcmEncryptTag(key,nonce,NULL,0,plain,16,cipher,tag,16));
    assert(!memcmp(cipher,cipherExpected,16));assert(!memcmp(tag,tagExpected,16));
    assert(LdnAesGcmDecryptTag(key,nonce,NULL,0,tag,8,cipher,16,out));assert(!memcmp(out,plain,16));
    tag[0]^=1;memset(out,0xa5,16);
    assert(!LdnAesGcmDecryptTag(key,nonce,NULL,0,tag,8,cipher,16,out));assert(out[0]==0xa5);
    assert(!LdnAesGcmEncryptTag(key,nonce,NULL,0,plain,16,cipher,tag,0));
    puts("PASS NIST AES-GCM vector, 8-byte tag, tampering, invalid tag length");
}
static size_t metadata(uint8_t *p) {
    memset(p,0,512);p[0]=RL_CONNECTED;p[3]=3;p[4]=169;p[5]=254;p[6]=1;p[7]=2;
    memset(p+8,255,3);lr_put64(p+12,UINT64_C(0x01006fa0233f8000));p[22]=32;
    memcpy(p+23,"000102030405060708090a0b0c0d0e0f",32);
    size_t at=71;lr_put16(p+at,122);at+=2;
    uint8_t record[24]={0x34,0x12,0xbc,0xbb,0xff,0xff,0xff,0xff,0xff,0xff,1};
    for(unsigned g=0;g<6;++g) {
        uint32_t word=lr_get32(record+g*4);
        for(unsigned i=0;i<5;++i) { unsigned digit=word%85;word/=85;uint8_t c=digit+0x23;if(c>=0x5c)++c;p[at+92+g*5+i]=c; }
    }
    at+=122;p[at++]=2;
    for(unsigned i=0;i<2;++i) {
        p[at]=169;p[at+1]=254;p[at+2]=1;p[at+3]=i+1;
        memset(p+at+4,i+1,6);p[at+10]=i;p[at+11]=1;at+=15;
    }
    return at;
}
static struct LdnPiaCrypto crypto;
static void host(struct GBASIORFUBackend *b,uint8_t proto,const uint8_t *p,size_t n) {
    static uint64_t nonce=0;uint8_t tiled[1024],datagram[1400];size_t size;
    size_t length=LdnPiaBuildMessage(proto,p,n,false,0,tiled);
    struct LdnPiaHeader h={.enc=0x90,.src=0x7620,.dst=0xc493,.pktid=1};
    ++nonce;for(unsigned i=0;i<8;++i)h.nonce8[i]=(uint8_t)(nonce>>(56-8*i));
    uint8_t ip[4]={169,254,1,1};assert(LdnPiaEncrypt(&crypto,tiled,length,ip,&h,datagram,&size));
    IOSRelayReceive(b,ip,datagram,size);
}
static void receiveWindowTest(void) {
    struct LdnPiaReliable r;struct LdnPiaReliableEntry out[128];uint8_t p=42;
    LdnPiaReliableInit(&r,33,1000);r.peerOpened=true;r.recvNext=100;
    struct LdnPiaReliableFrame f={.flagsA=7,.seq=101,.payload=&p,.payloadLength=1};
    assert(!LdnPiaReliableReceive(&r,&f,0,out,128));
    assert(LdnPiaReliablePoll(&r,40,out,128)==1);
    uint16_t ack;uint8_t mask[16];assert(LdnPiaParseBulkAck(out[0].payload,out[0].length,&ack,mask));assert(ack==100 && (mask[0]&1));
    f.seq=229;assert(!LdnPiaReliableReceive(&r,&f,41,out,128));assert(r.recvBuf[101%128].seq==101);
    f.seq=228;assert(!LdnPiaReliableReceive(&r,&f,42,out,128));assert(!r.recvBuf[100%128].used);
    f.seq=100;assert(LdnPiaReliableReceive(&r,&f,43,out,128)==2);assert(out[0].seq==100 && out[1].seq==101);
    LdnPiaReliableInit(&r,33,1000);r.peerOpened=true;r.recvNext=65535;
    f.seq=0;assert(!LdnPiaReliableReceive(&r,&f,0,out,128));f.seq=65535;
    assert(!LdnPiaReliableReceive(&r,&f,1,out,0));assert(r.recvNext==65535);
    assert(LdnPiaReliableReceive(&r,&f,2,out,128)==2 && r.recvNext==1);
    puts("PASS receive-window collision, upper bound, prior ACK preservation, output backpressure, sequence wrap");
}
int main(void) {
    cryptoTest();receiveWindowTest();
    uint8_t p[512],ssid[16];size_t n=metadata(p);for(unsigned i=0;i<16;++i)ssid[i]=i;
    LdnPiaCryptoInit(&crypto,ssid);
    struct GBASIORFU rfu={0};struct GBASIORFUBackend *b=IOSRelayCreate(send,canSend,logMessage,NULL);assert(b);rfu.backend=b;assert(b->init(b,&rfu));
    for(size_t len=0;len<n;++len)assert(!IOSRelayConfigure(b,p,len,1000));
    p[23]='z';assert(!IOSRelayConfigure(b,p,n,1000));p[23]='0';
    assert(IOSRelayConfigure(b,p,n,1000));b->searchStart(b);IOSRelayTick(b,1001);assert(beacons==1);
    b->connect(b,0x1234);assert(!accepted);
    uint8_t net[16]={1,0x11,0,0,0,0,0,2,0x76,0x20,1,1,1,1,1,1};
    host(b,1,net,16);IOSRelayTick(b,1017);assert(sent==2);assert(!accepted);
    uint8_t session[1]={5};host(b,13,session,1);IOSRelayTick(b,1034);assert(!accepted);
    uint8_t rtt[21]={0};host(b,3,rtt,21);IOSRelayTick(b,1051);IOSRelayTick(b,1068);assert(!accepted);
    uint16_t connectId=0;uint8_t ip[4]={169,254,1,2};
    for(unsigned i=0;i<sent;++i) {
        uint8_t plain[1400],decoded[8192];size_t pl,dl=sizeof(decoded),used;
        assert(LdnPiaDecrypt(&crypto,packets[i],sizes[i],ip,plain,&pl));assert(LdnPiaDecompress(plain,pl,decoded,&dl));
        struct LdnPiaMessage m[8];size_t count=LdnPiaParseMessages(decoded,dl,m,8,&used);assert(count<=8);
        for(size_t j=0;j<count;++j) if(m[j].proto==10) {
            struct LdnPiaReliableFrame frame;assert(LdnPiaParseReliableFrame(m[j].payload,m[j].payloadLength,&frame));
            if(frame.payloadLength==6 && frame.payload[1]=='C') connectId=lr_get16(frame.payload+4);
        }
    }
    assert(connectId);
    uint8_t accept[10]={0x57,'A',6,0,1,0};lr_put16(accept+6,connectId);
    uint8_t reliable[32];size_t length=LdnPiaBuildReliableFrame(0xfff0,0xfff0,0x0f,accept,10,reliable);
    host(b,10,reliable,length);assert(accepted==1);
    host(b,10,reliable,length);assert(accepted==1); // Reliable duplicate must not re-deliver.
    // Backpressure must retain fresh K/T messages and coalesce them with the ACK.
    writable=false;unsigned before=sent;
    uint8_t game[16]={0x57,'T',12,0,1,0,0,0,1};
    length=LdnPiaBuildReliableFrame(0xfff1,0xfff0,7,game,sizeof(game),reliable);
    host(b,10,reliable,length);IOSRelayTick(b,1100);IOSRelayTick(b,1600);assert(sent==before);
    writable=true;IOSRelayTick(b,1617);
    bool sawAck=false,sawK=false;unsigned tiles=0;
    for(unsigned i=before;i<sent;++i) {
        uint8_t plain[1400],decoded[8192];size_t pl,dl=sizeof(decoded),used;
        assert(sizes[i]+34<=500); // Relay command + codec overhead also fits.
        assert(LdnPiaDecrypt(&crypto,packets[i],sizes[i],ip,plain,&pl));
        assert(LdnPiaDecompress(plain,pl,decoded,&dl));
        struct LdnPiaMessage m[32];size_t count=LdnPiaParseMessages(decoded,dl,m,32,&used);assert(count<=32);
        for(size_t j=0;j<count;++j) if(m[j].proto==10) {
            struct LdnPiaReliableFrame f;assert(LdnPiaParseReliableFrame(m[j].payload,m[j].payloadLength,&f));++tiles;
            if(!f.flagsA) {sawAck=true;assert(m[j].haveMsgFlags && m[j].msgFlags==0x40);}
            else {assert(m[j].haveMsgFlags && !m[j].msgFlags);if(f.payloadLength==16 && f.payload[1]=='K')sawK=true;}
        }
    }
    assert(sawAck && sawK && tiles>=2);
    // With no incoming ACK, an ordinary frame must not retry every emulator tick.
    before=sent;IOSRelayTick(b,1634);IOSRelayTick(b,1651);assert(sent==before);
    puts("PASS BLE backpressure, batched ACK/game frames, tile flags, single BLE frame budget, retry pacing");
    // Full reliable send window must defer K acknowledgements, not kill the session.
    writable=false;
    for(unsigned i=0;i<160;i++){
        lr_put32(game+4,i+2);
        length=LdnPiaBuildReliableFrame((uint16_t)(0xfff2+i),0xfff0,7,game,sizeof(game),reliable);
        host(b,10,reliable,length);
    }
    IOSRelayTick(b,1700);assert(!disconnected && !queueFailures);
    // A cumulative ACK releases slots and lets the pending K FIFO drain.
    uint8_t ackPayload[20],mask[16]={0};LdnPiaBuildBulkAck(0x0070,mask,ackPayload);
    length=LdnPiaBuildReliableFrame(0xfff0,0xfff0,0,ackPayload,20,reliable);host(b,10,reliable,length);
    IOSRelayTick(b,1717);assert(!disconnected && !queueFailures);
    writable=true;IOSRelayTick(b,1734);assert(!disconnected && !queueFailures);
    puts("PASS K acknowledgements defer at full reliable window and resume after ACK without disconnect");
    IOSRelayTick(b,20000);assert(disconnected==1);
    b->deinit(b);assert(b->init(b,&rfu));assert(IOSRelayConfigure(b,p,n,21000));b->deinit(b);free(b);
    puts("PASS metadata bounds, Pia handshake, encrypted replies, host acceptance gate, duplicate suppression, timeout");
}
