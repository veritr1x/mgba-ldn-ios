/* MPL-2.0. Wire layouts independently implemented from the protocol reference:
 * https://github.com/Decryptu/pokeldn/blob/main/pokeldn/ldn/pia_connect.py
 * Only IPv4, one guest, FRLG protocol versions. No migration or public hosting. */
#include "pia-host.h"
#include "ldn-pia-reliable.h"
#include <string.h>
#include <zlib.h>
static unsigned be16(const uint8_t *p){return (unsigned)p[0]*256+p[1];}
static void put16(uint8_t *p,unsigned v){p[0]=v>>8;p[1]=v;}
static bool emit(struct PiaHost *h,uint8_t proto,const uint8_t *p,size_t n,bool establishing){
    if(h->count==8 || n>256)return false;
    struct LdnPiaOutMessage *m=&h->out[h->count++];memset(m,0,sizeof(*m));
    m->proto=proto;m->dst=establishing?0:h->peerVar;m->src=h->var;
    if(h->native && !establishing && (proto==3 || (proto==13 && n && p[0]==5)))m->dst=1;
    m->establishing=establishing;m->footer=!establishing;m->compress=n>=62;
    memcpy(m->payload,p,n);m->length=n;return true;
}
void PiaHostInit(struct PiaHost *h,const uint8_t ssid[16]){
    memset(h,0,sizeof(*h));memcpy(h->ssid,ssid,16);
    memcpy(h->mac,(uint8_t[]){2,0,0,0,0,1},6);memcpy(h->peerMac,(uint8_t[]){2,0,0,0,0,2},6);
    memcpy(h->ip,(uint8_t[]){169,254,1,1},4);memcpy(h->peerIp,(uint8_t[]){169,254,1,2},4);
    h->maxStations=6;h->var=0x7620;h->peerVar=LDN_PIA_DEFAULT_OUR_VAR;
}
static size_t station(uint8_t *p,const uint8_t *mac,unsigned var,const uint8_t *ip,unsigned index,const uint8_t *token,const uint8_t *player,size_t playerSize){
    memset(p,0,57);memcpy(p,mac,6);put16(p+8,var);memcpy(p+10,ip,4);put16(p+14,12345);
    p[16]=index;put16(p+17,index);memcpy(p+21,token,32);p[53]=p[54]=1;
    memcpy(p+57,player,playerSize);return 57+playerSize;
}
bool PiaHostReceive(struct PiaHost *h,uint8_t proto,const uint8_t *p,size_t n){
    if(!p || !n)return false;
    if(proto==LDN_PIA_PROTO_SESSION && p[0]==0){
        static const uint8_t protocols[]={1,0,3,5,5,1,10,3,13,7,15,0};
        if(n<104 || p[1]!=6 || memcmp(p+2,protocols,12) || be16(p+14)!=0x58)return false;
        /* Fixed one-player join layout; validate before creating any response. */
        if(((!h->native || !h->learnPeerVar) && memcmp(p+20,h->peerMac,6)) || p[26] || p[27] || (!h->learnPeerVar && be16(p+28)!=h->peerVar) || p[30] || p[31] ||
           memcmp(p+64,h->mac,6) || p[70] || p[71] || be16(p+72)!=h->var || p[74]!=1 || p[75]!=1 ||
           p[76] || memcmp(p+77,h->peerIp,4) || be16(p+81)!=12345 || p[99] || p[100] || p[101] ||
           p[102]>40 || (p[103]!=1 && p[103]!=2) || n!=104u+p[102])return false;
        if(!be16(p+28))return false;
        if(h->state==2)return true; /* duplicate join cannot rewind an active stream */
        if(h->count>5)return false;
        if(h->native && h->learnPeerVar)memcpy(h->peerMac,p+20,6);
        h->peerVar=be16(p+28);h->learnPeerVar=false;
        uint8_t reply[37]={2,13,7,1};memcpy(reply+8,p+16,4);memcpy(reply+12,h->mac,6);put16(reply+20,h->var);
        memcpy(reply+22,h->peerMac,6);put16(reply+30,h->peerVar);reply[32]=1;put16(reply+33,1);
        uint8_t *u=h->update;memset(u,0,256);u[0]=5;u[3]=1;u[6]=3;memcpy(u+7,h->mac,6);put16(u+15,h->var);u[17]=2;put16(u+19,1);
        uint8_t player[24]={0},token[32]={0};player[7]=1;player[19]=3;player[20]=1;memcpy(player+21,"Mac",3);
        size_t used=27;used+=station(u+used,h->mac,h->var,h->ip,0,token,player,sizeof(player));
        used+=station(u+used,h->peerMac,h->peerVar,h->peerIp,1,p+32,p+83,n-83);h->updateSize=used;
        if(h->native){emit(h,13,u,used,false);emit(h,13,reply,sizeof(reply),false);}
        else {emit(h,13,reply,sizeof(reply),false);emit(h,13,u,used,false);}h->state=1;h->deadline=0;return true;
    }
    if(proto==13 && p[0]==6){
        if(n!=15 || memcmp(p+1,h->peerMac,6) || p[7] || p[8] || p[14]!=1 || !h->state)return false;
        h->state=2;h->deadline=0;return true;
    }
    if(proto==3 && n==21 && h->state==2){
        if(p[0]==0){uint8_t reply[21];memcpy(reply,p,21);reply[0]=1;return emit(h,3,reply,21,false);}return p[0]==1;
    }
    if(proto==1 && n==8 && p[0]==1 && p[1]==0x12 && !p[2] && !p[3] && !p[4] && !p[5] && !p[6] && p[7]==2)h->netAcked=true;
    return proto==1 || (proto==10 && h->state==2);
}
void PiaHostTick(struct PiaHost *h,uint32_t now){
    if(h->deadline && (int32_t)(now-h->deadline)<0)return;
    if(h->count>=6)return;
    if(h->state==0){
        uint8_t p[206]={1,0x11};unsigned stations=h->maxStations; if(stations<2 || stations>8)return;put16(p+2,stations*22);p[7]=2;put16(p+8,h->var);memcpy(p+10,h->mac,6);
        uint32_t crc=(uint32_t)crc32(0,h->ssid+1,15);for(unsigned i=0;i<4;i++)p[22+i]=crc>>(24-8*i);
        p[26]=1;put16(p+27,stations);
        for(unsigned i=0;i<stations;i++){size_t at=30+i*22;p[at+1]=i<2?i:255;if(i<2){memcpy(p+at+4,i?h->peerIp:h->ip,4);put16(p+at+20,12345);}}
        emit(h,1,p,30+22*stations,true);
    }else if(h->state==1)emit(h,13,h->update,h->updateSize,false);
    else{uint8_t rtt[21]={0};rtt[3]=5;for(unsigned i=0;i<4;i++)rtt[8+i]=now>>(8*i);put16(rtt+19,h->var);emit(h,3,rtt,21,false);}
    h->deadline=now+500;
}
bool PiaHostDrain(struct PiaHost *h,struct LdnPiaOutMessage *out){
    if(!h->count)return false;*out=h->out[0];--h->count;memmove(h->out,h->out+1,h->count*sizeof(*out));return true;
}
