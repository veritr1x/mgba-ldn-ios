// AGPL-3.0-or-later. All calls are confined to the UI/emulation thread.
#import "GiftEngine.h"
#import <JavaScriptCore/JavaScriptCore.h>
#include "relay-backend.h"

@interface GiftEngine () {
    struct GBASIORFU _rfu;
    struct GBASIORFUBackend *_backend;
    uint16_t _hostID, _requestedID;
    BOOL _created, _ending;
}
@property(nonatomic,strong) JSContext *js;
@property(nonatomic,strong) JSValue *api;
@property(nonatomic,readwrite) NSArray *catalogue;
@property(nonatomic,readwrite) BOOL running;
@property(nonatomic,readwrite) NSString *error;
- (void)outgoing:(NSArray *)bytes;
@end
static bool giftSend(void *ctx,const uint8_t ip[4],const uint8_t *p,size_t n) {
    GiftEngine *g=(__bridge GiftEngine *)ctx;
    return g.send && g.send([NSData dataWithBytes:p length:n],[NSData dataWithBytes:ip length:4]);
}
static bool giftWritable(void *ctx) { GiftEngine *g=(__bridge GiftEngine *)ctx;return g.canSend && g.canSend(); }
static void giftLog(void *ctx,const char *text) {
    GiftEngine *g=(__bridge GiftEngine *)ctx;
    if(strstr(text,"Relay session stopped") || strstr(text,"overflow"))g.error=@"The gift connection stopped. Disconnect and try again.";
}
static NSArray *numbers(const uint8_t *p,size_t n) {
    NSMutableArray *out=[NSMutableArray arrayWithCapacity:n];for(size_t i=0;i<n;i++)[out addObject:@(p[i])];return out;
}
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
@implementation GiftEngine
- (instancetype)initWithScript:(NSString *)script {
    if(!(self=[super init]))return nil;
    self.js=[JSContext new];__weak GiftEngine *weak=self;
    self.js.exceptionHandler=^(JSContext *c,JSValue *e){weak.error=[e toString];};
    self.js[@"atob"]=^NSString *(NSString *s){NSData *d=[[NSData alloc] initWithBase64EncodedString:s options:0];return d?[[NSString alloc] initWithData:d encoding:NSISOLatin1StringEncoding]:@"";};
    self.js[@"giftSend"]=^(NSArray *a){[weak outgoing:a];};
    self.js[@"giftLog"]=^(NSString *s){};
    self.js[@"giftStatus"]=^(NSString *s,NSDictionary *d){GiftEngine *g=weak;if(g.status)g.status(s,d?:@{});};
    [self.js evaluateScript:script];self.api=self.js[@"Gifts"];
    self.catalogue=[[self.api invokeMethod:@"catalogue" withArguments:@[]] toArray];
    if(self.error || !self.catalogue.count)return nil;
    return self;
}
- (NSDictionary *)importCard:(NSData *)data name:(NSString *)name error:(NSString **)error {
    self.error=nil;
    JSValue *v=[self.api invokeMethod:@"importCard" withArguments:@[numbers(data.bytes,data.length),name]];
    if(self.error){if(error)*error=self.error;self.error=nil;return nil;}return [v toDictionary];
}
- (BOOL)start:(NSString *)identifier {
    [self stop];self.error=nil;_ending=NO;_hostID=_requestedID=0;
    _backend=IOSRelayCreate(giftSend,giftWritable,giftLog,(__bridge void *)self);
    if(!_backend){self.error=@"Could not create the gift host.";return NO;}
    GBASIORFUCreate(&_rfu,_backend);_created=YES;
    if(!_backend->init(_backend,&_rfu)){[self stop];self.error=@"Could not initialise the gift host.";return NO;}
    IOSRelayEnableNativeHost(_backend,true);IOSRelaySetGiftHost(_backend,true);
    self.running=YES;[self.api invokeMethod:@"start" withArguments:@[identifier]];
    if(self.error){[self stop];return NO;}return YES;
}
- (void)stop {
    self.running=NO;_ending=YES;
    [self.api invokeMethod:@"stop" withArguments:@[]];
    if(_backend){_backend->deinit(_backend);free(_backend);_backend=NULL;}
    if(_created){GBASIORFUDestroy(&_rfu);_created=NO;}
}
- (void)dealloc {
    // Do not invoke JS from dealloc: callbacks retain only a weak reference.
    if(_backend){_backend->deinit(_backend);free(_backend);}
    if(_created)GBASIORFUDestroy(&_rfu);
}
- (void)outgoing:(NSArray *)bytes {
    if(!self.running || _ending || !_backend || bytes.count>104 || bytes.count<12)return;
    uint8_t f[104]={0};for(NSUInteger i=0;i<bytes.count;i++)f[i]=[bytes[i] unsignedCharValue];
    if(memcmp(f,"RFU1",4))return;
    uint32_t type=be32(f+4),header=be32(f+8);
    if(type==0 && bytes.count==36){
        uint32_t words[6];for(unsigned i=0;i<6;i++)words[i]=be32(f+12+4*i);
        _backend->setBroadcast(_backend,words);
        if(!_hostID){_hostID=header&65535;_backend->hostStart(_backend,_hostID);}
    }else if((type==2 || type==3) && _requestedID){
        _backend->connectReply(_backend,_requestedID,type==2,0);_requestedID=0;
    }else if(type==5 && bytes.count==104 && (header&127)<=90){
        _backend->sendData(_backend,f+12,header&127);
    }else if(type==4){
        // GiftSession has already allowed the console five seconds to finish saving.
        _ending=YES;if(self.status)self.status(@"closed",@{});
    }
}
- (void)tick:(uint32_t)now {
    if(!self.running || !_backend)return;
    IOSRelayTick(_backend,now);
    struct GBASIORFUEvent event;
    while(GBASIORFUPopEvent(&_rfu,&event)){
        if(event.type==RFU_EVENT_CONNECT_REQUEST){
            _requestedID=event.deviceId;
            [self.api invokeMethod:@"receive" withArguments:@[@1,@(_hostID),@[]]];
        }else if(event.type==RFU_EVENT_DATA && event.length){
            if(event.length>RFU_CLIENT_TX_MAX){self.error=@"The console sent an invalid RFU child frame.";continue;}
            uint8_t f[104]={0};f[8]=event.length;memcpy(f+12,event.data,event.length);
            [self.api invokeMethod:@"receive" withArguments:@[@6,@0,numbers(f,sizeof(f))]];
        }else if(event.type==RFU_EVENT_DISCONNECTED){
            _ending=YES;
            [self.api invokeMethod:@"receive" withArguments:@[@4,@0,@[]]];
            if(self.status)self.status(@"closed",@{});
        }
    }
    if(!_ending && IOSRelayCanAdvanceFrame(_backend))[self.api invokeMethod:@"tick" withArguments:@[]];
    if(self.error && self.status){_ending=YES;self.status(@"error",@{@"message":self.error});self.error=nil;}
}
- (NSData *)advertisement {uint8_t ad[122];return _backend && IOSRelayNativeAdvertisement(_backend,ad)?[NSData dataWithBytes:ad length:sizeof(ad)]:nil;}
- (BOOL)configure:(NSData *)m now:(uint32_t)now {return _backend && IOSRelayConfigureNativeHost(_backend,m.bytes,m.length,now);}
- (void)receive:(NSData *)data from:(NSData *)ip {if(_backend && ip.length==4)IOSRelayReceive(_backend,ip.bytes,data.bytes,data.length);}
- (void)decide:(BOOL)sendAgain {[self.api invokeMethod:@"decide" withArguments:@[@(sendAgain)]];}
@end
