/* MPL-2.0. Retail parent / Switch-revision child post-trade synchronization.
 * Switch REVISION >= 0xA adds CB2_SaveAndEndTrade cases 43/44 between
 * the second and third retail save barriers. Absorb that child-only barrier
 * and translate subsequent counters; never change block/Pokemon bytes.
 * Only enabled for native Switch hosting, not the retail/retail laboratory. */
#ifndef IOS_LDN_HOST_TRADE_SHIM_H
#define IOS_LDN_HOST_TRADE_SHIM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
struct HostTradeShim {
    uint16_t lastHostRound, blockCount, fake[32];
    unsigned fakeCount, absorbed, replies;
    bool haveHostRound, postTrade, haveChild;
    uint8_t lastChild[16], mappedChild[16], nextTag;
};
static inline uint16_t htsRead(const uint8_t *p){return p[0]|((uint16_t)p[1]<<8);}
static inline void htsWrite(uint8_t *p,uint16_t v){p[0]=v;p[1]=v>>8;}
static inline bool htsAtOrAfter(uint16_t a,uint16_t b){return (int16_t)(a-b)>=0;}
static inline uint16_t htsToSwitch(const struct HostTradeShim *s,uint16_t round){
    for(unsigned i=0;i<s->fakeCount;i++)if(htsAtOrAfter(round,s->fake[i]))++round;
    return round;
}
/* Called on original parent frames once, before entering the wire queue. */
static inline bool HostTradeParent(struct HostTradeShim *s,uint8_t *p,size_t n){
    if(n!=73 || memcmp(p,(uint8_t[]){0x46,0,5},3))return true;
    uint16_t cmd=htsRead(p+3),value=htsRead(p+5);
    if(cmd==0x6600){s->lastHostRound=value;s->haveHostRound=true;}
    if(cmd==0x8800)s->blockCount=value;
    if(cmd==0x8900 && s->blockCount==2 && value==0xdcba && !s->postTrade){
        if(!s->haveHostRound || s->fakeCount==32)return false;
        /* Last pre-trade barrier + first save + second save + Switch extra. */
        uint16_t extra=htsToSwitch(s,(uint16_t)(s->lastHostRound+3));
        s->fake[s->fakeCount++]=extra;s->postTrade=true;
    }
    if(cmd==0xa100 && value==1)s->postTrade=false;
    /* Both the parent command and the child's echo use retail counters. */
    for(unsigned row=0;row<2;row++){
        uint8_t *slot=p+3+14*row;
        if(htsRead(slot)==0x6600)htsWrite(slot+2,htsToSwitch(s,htsRead(slot+2)));
    }
    return true;
}
/* False consumes a Switch-only barrier. reply is then a complete wire-ready
 * parent frame and MUST bypass HostTradeParent. All other bytes stay intact,
 * except standby counters and the child command's 3-bit delivery tag. */
static inline bool HostTradeChild(struct HostTradeShim *s,uint8_t *p,size_t n,uint8_t reply[73]){
    if(n!=16 || p[0]!=0x0e || p[1]!=0x10 || !p[3])return true;
    uint16_t cmd=(p[2]&31)|((uint16_t)p[3]<<8),round=htsRead(p+4);
    if(cmd==0x6600){
        for(unsigned i=0;i<s->fakeCount;i++)if(round==s->fake[i]){
            memset(reply,0,73);reply[0]=0x46;reply[2]=5;
            htsWrite(reply+3,0x6600);htsWrite(reply+5,round);
            htsWrite(reply+17,0x6600);htsWrite(reply+19,round);
            ++s->replies;s->absorbed|=1u<<i;return false;
        }
    }
    /* Preserve consecutive duplicate frames as duplicates, while removing the
     * tag gaps left by the consumed commands. Keep original bytes for compare. */
    if(s->haveChild && !memcmp(s->lastChild,p,16)){memcpy(p,s->mappedChild,16);return true;}
    if(!s->haveChild)s->nextTag=p[2]>>5;
    memcpy(s->lastChild,p,16);s->haveChild=true;
    if(cmd==0x6600){
        unsigned offset=0;
        for(unsigned i=0;i<s->fakeCount;i++)if(htsAtOrAfter(round,s->fake[i]))++offset;
        htsWrite(p+4,(uint16_t)(round-offset));
    }
    p[2]=(p[2]&31)|(s->nextTag<<5);s->nextTag=(s->nextTag+1)&7;
    memcpy(s->mappedChild,p,16);return true;
}
#endif
