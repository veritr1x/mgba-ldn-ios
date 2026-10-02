/* MIT. Fake platform queue: tests recovery policy, not CoreBluetooth hardware. */
#include "relay_notify_gate.h"
#include "relay_stream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned delivered;
static bool receive(void *ctx,const uint8_t *p,size_t n){
    (void)ctx;assert(n==100 && p[0]==42);++delivered;return true;
}
int main(void){
    LrNotifyGate g={0};
    assert(lr_notify_attempt(&g,0,true));lr_notify_result(&g,false,0);
    for(unsigned t=5;t<500;t+=5)assert(!lr_notify_attempt(&g,t,true));
    assert(!lr_notify_attempt(&g,500,false)); /* No peer: no recovery probe. */
    assert(lr_notify_attempt(&g,500,true));lr_notify_result(&g,false,500);
    assert(!lr_notify_attempt(&g,999,true));
    lr_notify_ready(&g);assert(lr_notify_attempt(&g,999,true)); /* Callback wins. */

    static LrStream a,b;ls_init(&a,182,42);ls_init(&b,182,42);
    uint8_t payload[100]={42},frame[500],reverse[500];assert(ls_enqueue(&a,payload,sizeof(payload)));
    lr_notify_ready(&g);unsigned attempts=0;
    for(uint64_t t=0;t<=1000;t+=5){
        if(!lr_notify_attempt(&g,t,true))continue;
        size_t n=ls_prepare(&a,frame,sizeof(frame),t);assert(n>LS_HEADER);attempts++;
        bool accepted=t==1000;lr_notify_result(&g,accepted,t);
        if(!accepted){
            /* Real platform send rejection must leave the message uncommitted. */
            assert(a.next==1 && a.count==1 && a.flying==0 && a.tx_frames==0);
            continue;
        }
        assert(lr_get16(frame+4)==1);ls_commit(&a,frame,n,t);
        assert(ls_ingest(&b,frame,n,receive,NULL,t));
        size_t ack=ls_prepare(&b,reverse,sizeof(reverse),t);assert(ack);
        ls_commit(&b,reverse,ack,t);assert(ls_ingest(&a,reverse,ack,receive,NULL,t));
        /* An already-accepted radio notification may be repeated by the link:
         * the receiver must not deliver its payload twice. */
        assert(ls_ingest(&b,frame,n,receive,NULL,t+1));
    }
    assert(attempts==3 && delivered==1 && !a.count && !a.flying && a.next==2 && !g.blocked);
    /* Resetting a disconnected session cannot preserve an old blocked latch. */
    lr_notify_result(&g,false,1100);lr_notify_ready(&g);assert(lr_notify_attempt(&g,1101,false));
    puts("PASS notification readiness callback, missing-callback recovery, bounded probes, inactive-peer guard, rejected-write ownership and exactly-once stream delivery");
}
