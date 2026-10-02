/* MPL-2.0. Exercise the real upstream cable/air translator with a controlled RFU peer. */
#include <mgba/internal/gba/sio/rfu-wrapper-air.h>
#include <mgba/internal/gba/sio/rfu.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct Fake {struct GBASIORFUBackend d;struct GBASIORFU *rfu;};
static unsigned inits,deinits,destroys,searches,connects,sends;
static bool failInit;
static struct GBASIORFUEvent events[16];static unsigned head,count;
static void push(struct GBASIORFUEvent event){assert(count<16);events[(head+count++)%16]=event;}
void GBASIOCableTrace(struct GBASIO *sio,const char *fmt,...){(void)sio;(void)fmt;}
void GBASIORFUCreate(struct GBASIORFU *rfu,struct GBASIORFUBackend *backend){memset(rfu,0,sizeof(*rfu));rfu->backend=backend;}
void GBASIORFUDestroy(struct GBASIORFU *rfu){(void)rfu;}
void GBASIORFUSetTraceFile(struct GBASIORFU *rfu,const char *path){(void)rfu;(void)path;}
bool GBASIORFUPopEvent(struct GBASIORFU *rfu,struct GBASIORFUEvent *event){(void)rfu;if(!count)return false;*event=events[head];head=(head+1)%16;--count;return true;}
struct GBASIORFUBackend *GBASIORFUBackendCreate(const char *name){(void)name;return NULL;}
void GBASIORFUBackendDestroy(struct GBASIORFUBackend *b){++destroys;free(b);}
static bool init(struct GBASIORFUBackend *b,struct GBASIORFU *r){((struct Fake *)b)->rfu=r;++inits;return !failInit;}
static void deinit(struct GBASIORFUBackend *b){(void)b;++deinits;}
static void search(struct GBASIORFUBackend *b){(void)b;++searches;}
static void stop(struct GBASIORFUBackend *b){(void)b;}
static void connectHost(struct GBASIORFUBackend *b,uint16_t id){(void)b;assert(id==0x1234);++connects;}
static void disconnectHost(struct GBASIORFUBackend *b,unsigned mask){(void)b;(void)mask;}
static void sendSlot(struct GBASIORFUBackend *b,const uint8_t *data,size_t size){(void)b;assert(data && size>0 && size<=16);++sends;}
static struct GBASIORFUBackend *make(void){struct Fake *b=calloc(1,sizeof(*b));assert(b);b->d=(struct GBASIORFUBackend){.init=init,.deinit=deinit,.searchStart=search,.searchStop=stop,.connect=connectHost,.disconnect=disconnectHost,.sendData=sendSlot};return &b->d;}
static void exercise(unsigned version){
    struct GBASIORFUWrapper wrapper={0};unsigned wasInit=inits,wasDeinit=deinits,wasDestroyed=destroys;
    struct GBASIORFUBackend *b=make();assert(GBASIORFUWrapperAttachAirBackend(&wrapper,b,"switch-relay",NULL));assert(inits==wasInit+1);
    assert(wrapper.air && wrapper.peer.frame && wrapper.peer.nextCommand && wrapper.peer.gameCommand);
    // The injected driver asks the game for its real LinkPlayer over the cable.
    uint16_t command[8];bool requested=false;for(int i=0;i<100;i++){memset(command,0,sizeof(command));if(wrapper.peer.nextCommand(wrapper.peer.context,command) && command[0]==0x2222){requested=true;break;}}assert(requested);
    uint8_t player[70]={0};player[16]=version;player[17]=0x40;player[20]=0x34;player[21]=0x12;player[24]=0xbc;memset(player+25,255,7);player[36]=0x33;player[37]=0x11;
    memset(command,0,sizeof(command));command[0]=0xbbbb;command[1]=60;wrapper.peer.gameCommand(wrapper.peer.context,command);
    for(int offset=0;offset<60;offset+=14){command[0]=0x8888;for(int j=0;j<7;j++)command[j+1]=player[offset+2*j]|(player[offset+2*j+1]<<8);wrapper.peer.gameCommand(wrapper.peer.context,command);}
    unsigned wasSearch=searches;wrapper.peer.frame(wrapper.peer.context);assert(searches==wasSearch+1);
    unsigned wasConnect=connects;
    struct GBASIORFUEvent event={.type=RFU_EVENT_BROADCAST,.deviceId=0x1234,.slot=255};event.words[3]=4;push(event);wrapper.peer.frame(wrapper.peer.context);assert(connects==wasConnect); // occupied
    event.slot=0;event.words[3]=1;push(event);wrapper.peer.frame(wrapper.peer.context);assert(connects==wasConnect); // not Trade Center
    event.words[3]=4;push(event);wrapper.peer.frame(wrapper.peer.context);assert(connects==wasConnect+1);
    unsigned wasSends=sends;event=(struct GBASIORFUEvent){.type=RFU_EVENT_CONNECT_RESULT,.accepted=true,.deviceId=0x1234,.slot=0};push(event);
    for(int i=0;i<20;i++)wrapper.peer.frame(wrapper.peer.context);assert(sends>wasSends); // actual RFU NI_START, not the cable stub
    wrapper.airDestroy(wrapper.air);wrapper.air=NULL;assert(deinits==wasDeinit+1 && destroys==wasDestroyed+1);
}
int main(void){
    exercise(1);exercise(2);
    struct GBASIORFUWrapper w={0};unsigned d=destroys,di=deinits;failInit=true;
    assert(!GBASIORFUWrapperAttachAirBackend(&w,make(),"switch-relay",NULL));assert(!w.air && destroys==d+1 && deinits==di+1);failInit=false;
    d=destroys;assert(!GBASIORFUWrapperAttachAirBackend(NULL,make(),"switch-relay",NULL));assert(destroys==d+1);
    assert(GBASIORFUWrapperAttachAirBackend(&w,make(),"switch-relay",NULL));void *original=w.air;d=destroys;
    assert(!GBASIORFUWrapperAttachAirBackend(&w,make(),"switch-relay",NULL));assert(w.air==original && destroys==d+1);w.airDestroy(w.air);
    puts("PASS injected cable backend: Ruby/Sapphire LinkPlayer, host filtering, RFU join/NI, init failure and ownership");
}
