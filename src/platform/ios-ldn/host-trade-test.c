/* MPL-2.0. Regression for the captured 0.4.7 post-save phase mismatch. */
#include "host-trade-shim.h"
#include <assert.h>
#include <stdio.h>
static void parent(uint8_t p[73],uint16_t cmd,uint16_t value){
    memset(p,0,73);p[0]=0x46;p[2]=5;htsWrite(p+3,cmd);htsWrite(p+5,value);
}
static void child(uint8_t p[16],uint16_t cmd,uint16_t value,unsigned tag){
    memset(p,0,16);p[0]=14;p[1]=16;htsWrite(p+2,cmd);p[2]|=(tag&7)<<5;htsWrite(p+4,value);
}
/* Five retail barriers vs six Switch barriers, from trade_scene.c + trade.c.
 * With the original passthrough the first A100 is sent at Switch phase 5
 * (not party-ready), matching the captured ABCD/stale-buffer block. */
static bool saveAndReenter(struct HostTradeShim *s,bool translate,uint16_t retailStart,uint16_t switchStart){
    uint8_t p[73],c[16],reply[73];unsigned switchPhase=0,retailPhase=0,tag=0,forwarded=0;
    parent(p,0x6600,retailStart-1);if(translate)assert(HostTradeParent(s,p,73));
    parent(p,0x8800,2);if(translate)assert(HostTradeParent(s,p,73));
    parent(p,0x8900,0xdcba);if(translate){assert(HostTradeParent(s,p,73));unsigned n=s->fakeCount;assert(HostTradeParent(s,p,73));assert(s->fakeCount==n);}
    while(retailPhase<5){
        uint16_t round=switchStart+switchPhase;
        child(c,0x6600,round,tag++);
        if(translate && !HostTradeChild(s,c,16,reply)){
            assert(switchPhase==2 && htsRead(reply+3)==0x6600 && htsRead(reply+5)==round);
            assert(htsRead(reply+17)==0x6600 && htsRead(reply+19)==round);
            /* A repeated extra round gets the same wire answer and is never
             * delivered to the retail ROM. It must not add another offset. */
            unsigned n=s->fakeCount;child(c,0x6600,round,tag++);
            assert(!HostTradeChild(s,c,16,reply));assert(s->fakeCount==n);
            ++switchPhase;continue;
        }
        assert(htsRead(c+4)==(uint16_t)(retailStart+retailPhase));
        if(translate && forwarded)assert((c[2]>>5)==(forwarded&7));
        ++forwarded;
        parent(p,0x6600,retailStart+retailPhase);
        htsWrite(p+17,0x6600);htsWrite(p+19,retailStart+retailPhase);
        if(translate)assert(HostTradeParent(s,p,73));
        assert(htsRead(p+5)==round && htsRead(p+19)==round);
        ++retailPhase;++switchPhase;
    }
    parent(p,0xa100,1);if(translate)assert(HostTradeParent(s,p,73));
    assert(htsRead(p+3)==0xa100 && htsRead(p+5)==1);
    return switchPhase==6;
}
int main(void){
    struct HostTradeShim old={0},s={0};
    assert(!saveAndReenter(&old,false,5,5));
    assert(saveAndReenter(&s,true,5,5));assert(s.fakeCount==1 && s.fake[0]==7 && s.absorbed==1);
    /* A second trade retains the counter offset. Reset only the test's tag
     * origin; a real child's raw tags continue across the whole session. */
    s.haveChild=false;assert(saveAndReenter(&s,true,10,11));assert(s.fakeCount==2 && s.fake[1]==13);
    puts("PASS old path requests party one phase early; translated path waits for all six Switch barriers, including repeated and second-trade rounds");
    uint8_t p[73],before[73],c[16],original[16],reply[73];
    for(unsigned frag=0;frag<19;frag++){
        parent(p,0x8900|frag,0);for(unsigned i=5;i<73;i++)p[i]=(uint8_t)(i*29+frag);
        memcpy(before,p,73);assert(HostTradeParent(&s,p,73));assert(!memcmp(p,before,73));
        child(c,0x8900|frag,0,frag);for(unsigned i=4;i<16;i++)c[i]=(uint8_t)(i*17+frag);
        memcpy(original,c,16);assert(HostTradeChild(&s,c,16,reply));
        assert((c[2]&31)==(original[2]&31) && !memcmp(c+3,original+3,13));
        memcpy(before,c,16);assert(HostTradeChild(&s,original,16,reply));assert(!memcmp(original,before,16));
    }
    puts("PASS Pokemon/mail fragments unchanged; duplicate child commands keep identical mapped tags");
    struct HostTradeShim wrap={0};assert(saveAndReenter(&wrap,true,65532,65532));
    memset(&s,0,sizeof(s));parent(p,0x6600,4);assert(HostTradeParent(&s,p,73));assert(htsRead(p+5)==4);
    parent(p,0x8800,2);assert(HostTradeParent(&s,p,73));s.fakeCount=32;
    parent(p,0x8900,0xdcba);assert(!HostTradeParent(&s,p,73));
    puts("PASS counter wrap, reset, and bounded-session fail-closed behavior");
}
