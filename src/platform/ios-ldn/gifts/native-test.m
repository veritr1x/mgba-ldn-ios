// AGPL-3.0-or-later. JavaScriptCore + actual encrypted Pia backend, in-memory transport.
// RFU event fixture replaces only the emulator's SIO driver (no ROM needed).
#import "GiftEngine.h"
#import <JavaScriptCore/JavaScriptCore.h>
#include "relay-backend.h"
#include "relay/relay_protocol.h"
#include "ldn.h"
#include <assert.h>
static struct GBASIORFU *hostRfu;
static struct GBASIORFU child;
static struct GBASIORFUBackend *client;
static GiftEngine *engine;
static bool writable=true;
static unsigned accepted, frames, errors;
static uint16_t device;
static NSMutableArray *packets;
static JSValue *recipient;
static bool feedRecipient;
static uint8_t ips[2][4]={{169,254,1,1},{169,254,1,2}};
static struct GBASIORFUEvent *push(struct GBASIORFU *r,int type){unsigned next=(r->eventHead+1)%RFU_EVENT_QUEUE;assert(next!=r->eventTail);struct GBASIORFUEvent *e=&r->events[r->eventHead];memset(e,0,sizeof(*e));e->type=type;r->eventHead=next;return e;}
void GBASIORFUCreate(struct GBASIORFU *r,struct GBASIORFUBackend *b){memset(r,0,sizeof(*r));r->backend=b;hostRfu=r;}
void GBASIORFUDestroy(struct GBASIORFU *r){if(hostRfu==r)hostRfu=NULL;}
bool GBASIORFUPopEvent(struct GBASIORFU *r,struct GBASIORFUEvent *e){if(r->eventTail==r->eventHead)return false;*e=r->events[r->eventTail];r->eventTail=(r->eventTail+1)%RFU_EVENT_QUEUE;return true;}
void GBASIORFUConnectRequested(struct GBASIORFU *r,uint16_t id){push(r,RFU_EVENT_CONNECT_REQUEST)->deviceId=id;}
void GBASIORFUDisconnected(struct GBASIORFU *r,int slot){push(r,RFU_EVENT_DISCONNECTED)->slot=slot;}
void GBASIORFUDataReceived(struct GBASIORFU *r,unsigned slot,const uint8_t *p,size_t n){assert(n<=96);struct GBASIORFUEvent *e=push(r,RFU_EVENT_DATA);e->slot=slot;e->length=n;memcpy(e->data,p,n);}
void GBASIORFUConnectResult(struct GBASIORFU *r,bool ok,uint16_t id,unsigned slot){assert(r==&child);if(ok)accepted++;}
void GBASIORFUBroadcastReceived(struct GBASIORFU *r,uint16_t id,uint8_t slot,const uint32_t words[6]){device=id;}
static bool sendClient(void *ctx,const uint8_t ip[4],const uint8_t *p,size_t n){[packets addObject:@{@"host":@YES,@"data":[NSData dataWithBytes:p length:n]}];return true;}
static bool canSend(void *ctx){return writable;}
static void logClient(void *ctx,const char *s){if(strstr(s,"overflow") || strstr(s,"stopped")){puts(s);errors++;}}
static void step(uint32_t now){
 [engine tick:now];IOSRelayTick(client,now);
 while(packets.count){NSDictionary *p=packets[0];[packets removeObjectAtIndex:0];NSData *data=p[@"data"];
  if([p[@"host"] boolValue])[engine receive:data from:[NSData dataWithBytes:ips[1] length:4]];
  else IOSRelayReceive(client,ips[0],data.bytes,data.length);
 }
 struct GBASIORFUEvent e;while(GBASIORFUPopEvent(&child,&e))if(e.type==RFU_EVENT_DATA && e.length){
  if(e.length==3)frames++;
  if(feedRecipient){NSMutableArray *a=[NSMutableArray new];for(unsigned i=0;i<e.length;i++)[a addObject:@(e.data[i])];[recipient invokeMethod:@"receive" withArguments:@[a]];}
 }
 if(feedRecipient && accepted){JSValue *value=[recipient invokeMethod:@"take" withArguments:@[]];if(!value.isNull){NSArray *a=value.toArray;assert(a.count<=16);uint8_t p[16];for(unsigned i=0;i<a.count;i++)p[i]=[a[i] unsignedCharValue];client->sendData(client,p,a.count);}}
}
int main(int argc,const char **argv){@autoreleasepool{
 assert(argc==3);NSString *script=[NSString stringWithContentsOfFile:@(argv[1]) encoding:NSUTF8StringEncoding error:nil];
 engine=[[GiftEngine alloc] initWithScript:script];assert(engine && !engine.error);assert(engine.catalogue.count==3);assert([engine.catalogue[2][@"events"] count]==60);
 packets=[NSMutableArray new];engine.send=^BOOL(NSData *data,NSData *ip){assert(ip.length==4);[packets addObject:@{@"host":@NO,@"data":data}];return YES;};engine.canSend=^BOOL{return writable;};
 __block NSString *lastError=nil;engine.status=^(NSString *s,NSDictionary *d){if([s isEqual:@"error"])lastError=d[@"message"];};
 NSString *error=nil;assert(![engine importCard:[NSMutableData dataWithLength:1] name:@"bad.wc3" error:&error] && error.length);
 assert([engine start:@"frlg-altering-cave"]);NSData *ad=engine.advertisement;assert(ad.length==122);
 struct LdnRfuBeacon beacon;assert(LdnDecodeRfuBeacon(ad.bytes,ad.length,&beacon));device=beacon.trainerId;assert(device);
 uint8_t meta[226]={RL_HOSTED};meta[3]=3;memcpy(meta+4,ips[0],4);memset(meta+8,255,3);lr_put64(meta+12,UINT64_C(0x01006fa0233f8000));meta[22]=32;memcpy(meta+23,"000102030405060708090a0b0c0d0e0f",32);lr_put16(meta+71,122);memcpy(meta+73,ad.bytes,122);meta[195]=1;
 for(unsigned i=0;i<2;i++){uint8_t *node=meta+196+15*i;memcpy(node,ips[i],4);for(unsigned j=0;j<6;j++)node[4+j]=(i?0x70:0x10)+j*0x10;node[10]=i;node[11]=1;lr_put16(node+12,88);}
 for(unsigned n=0;n<211;n++)assert(![engine configure:[NSData dataWithBytes:meta length:n] now:1]);
 assert([engine configure:[NSData dataWithBytes:meta length:211] now:1]);[engine tick:20000];assert(!engine.error);
 meta[0]=RL_MEMBERS;meta[195]=2;assert([engine configure:[NSData dataWithBytes:meta length:226] now:20001]);
 client=IOSRelayCreate(sendClient,canSend,logClient,NULL);child.backend=client;assert(client->init(client,&child));
 meta[0]=RL_CONNECTED;memcpy(meta+4,ips[1],4);assert(IOSRelayConfigure(client,meta,226,20001));client->searchStart(client);
 for(unsigned t=20001;t<21000;t+=17)step(t);client->connect(client,device);
 for(unsigned t=21000;t<23000;t+=17)step(t);
 assert(accepted==1 && frames>0 && !errors && !lastError);
 JSContext *peerJS=[JSContext new];peerJS.exceptionHandler=^(JSContext *c,JSValue *e){fprintf(stderr,"Fake recipient: %s\n",e.toString.UTF8String);abort();};
 peerJS[@"atob"]=^NSString *(NSString *s){return [[NSString alloc] initWithData:[[NSData alloc] initWithBase64EncodedString:s options:0] encoding:NSISOLatin1StringEncoding];};
 [peerJS evaluateScript:script];[peerJS evaluateScript:[NSString stringWithContentsOfFile:@(argv[2]) encoding:NSUTF8StringEncoding error:nil]];recipient=peerJS[@"GiftPeer"];[recipient invokeMethod:@"start" withArguments:@[]];feedRecipient=true;
 writable=false;for(unsigned t=23000;t<23300;t+=17)step(t);writable=true;for(unsigned t=23300;t<25000;t+=17)step(t);assert(!errors && !lastError);
 __block BOOL complete=NO;
 engine.status=^(NSString *s,NSDictionary *d){if([s isEqual:@"result"] && [d[@"outcome"] isEqual:@"sent"])complete=YES;if([s isEqual:@"error"])lastError=d[@"message"];};
 for(unsigned t=25000;t<120000 && !complete;t+=17)step(t);
 NSDictionary *result=[[recipient invokeMethod:@"result" withArguments:@[]] toDictionary];
 assert(complete && [result[@"card"] count]==332 && [result[@"script"] count]>0 && [result[@"closed"] boolValue] && !lastError && !errors);
 puts("PASS full Wonder Card delivery through JavaScriptCore, RFU, encrypted Pia and simulated recipient, including save acknowledgement and closing");
 feedRecipient=false;
 // A malicious/invalid child length must be rejected before writing RFU1's 104-byte frame.
 [engine stop];assert([engine start:@"frlg-altering-cave"]);uint8_t invalid[96]={1};GBASIORFUDataReceived(hostRfu,0,invalid,96);[engine tick:25001];assert([lastError containsString:@"invalid RFU"]);
 [engine stop];assert(!engine.running && !engine.advertisement);
 assert([engine start:@"frlg-altering-cave"] && engine.advertisement.length==122);[engine stop];
 assert(![engine start:@"missing-card"] && engine.error.length);[engine stop];
 client->deinit(client);free(client);engine=nil;
 puts("PASS JavaScriptCore catalogue/import/start/restart, native metadata validation, encrypted Pia handshake, RFU request/accept/data, backpressure and oversized-frame rejection");
}return 0;}
