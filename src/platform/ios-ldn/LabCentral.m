/* MIT. Derived from ldn-relay's tested Mac BLE central. Main-queue owned. */
#import "LabCentral.h"
#import <CoreBluetooth/CoreBluetooth.h>
#include "relay_stream.h"
#include "relay_batch.h"
#include <time.h>
static uint64_t ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static NSString *const serviceID=@"7B61238D-028A-4F65-99D8-7C8F22A11D4E";
static NSString *const charID=@"7B61238D-028A-4F65-99D8-7C8F22A11D4F";
@interface LabCentral () <CBCentralManagerDelegate,CBPeripheralDelegate>{
 CBCentralManager *manager;CBPeripheral *phone;CBCharacteristic *characteristic;
 dispatch_source_t timer;LrStream stream;uint16_t limit;uint32_t nonce;unsigned stage;
 NSMutableArray<NSData *> *pending;NSData *advertisement;uint64_t began,lastAdvert,lastStats,offeredAt;
 BOOL sessionActive;id activity;
}
-(BOOL)receive:(const uint8_t *)p size:(size_t)n;
@end
static bool receiveMessage(void *ctx,const uint8_t *p,size_t n){return [(__bridge LabCentral *)ctx receive:p size:n];}
@implementation LabCentral
-(void)start {
 nonce=arc4random()|1;began=ms();pending=[NSMutableArray new];
 activity=[[NSProcessInfo processInfo]beginActivityWithOptions:NSActivityUserInitiated|NSActivityLatencyCritical reason:@"Emulator BLE trade host"];
 manager=[[CBCentralManager alloc]initWithDelegate:self queue:dispatch_get_main_queue()];
 timer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,dispatch_get_main_queue());
 dispatch_source_set_timer(timer,DISPATCH_TIME_NOW,2000000,200000);
 __weak LabCentral *weak=self;dispatch_source_set_event_handler(timer,^{[weak tick];});dispatch_resume(timer);
}
-(void)log:(NSString *)text {if(self.statusHandler)self.statusHandler(text);}
-(void)fail:(NSString *)text {
 [self log:text];stage=0;[pending removeAllObjects];sessionActive=NO;advertisement=nil;
 if(timer){dispatch_source_cancel(timer);timer=nil;}[manager stopScan];
 if(phone)[manager cancelPeripheralConnection:phone];phone=nil;
 if(self.resetHandler)self.resetHandler();
 if(activity){[[NSProcessInfo processInfo]endActivity:activity];activity=nil;}
}
-(NSUInteger)queuedMessages{return pending.count+stream.count;}
-(BOOL)enqueue:(NSData *)data {
 if(stage!=4 || !data.length || data.length>LR_MAX_MESSAGE || pending.count>=64)return NO;
 [pending addObject:data];return YES;
}
-(void)centralManagerDidUpdateState:(CBCentralManager *)m {
 [self log:[NSString stringWithFormat:@"Bluetooth state=%ld",(long)m.state]];
 if(m.state==CBManagerStatePoweredOn)[m scanForPeripheralsWithServices:@[[CBUUID UUIDWithString:serviceID]] options:nil];
 if(m.state==CBManagerStateUnauthorized || m.state==CBManagerStateUnsupported)[self fail:@"Bluetooth unavailable/permission denied"];
}
-(void)centralManager:(CBCentralManager *)m didDiscoverPeripheral:(CBPeripheral *)p advertisementData:(NSDictionary *)data RSSI:(NSNumber *)rssi {
 (void)data;(void)rssi;if(phone)return;phone=p;phone.delegate=self;[m stopScan];[self log:[NSString stringWithFormat:@"Connecting %@ %@",p.name,p.identifier]];[m connectPeripheral:p options:nil];
}
-(void)centralManager:(CBCentralManager *)m didConnectPeripheral:(CBPeripheral *)p {(void)m;[p discoverServices:@[[CBUUID UUIDWithString:serviceID]]];}
-(void)centralManager:(CBCentralManager *)m didFailToConnectPeripheral:(CBPeripheral *)p error:(NSError *)e {(void)m;(void)p;[self fail:e.localizedDescription?:@"Connection failed"];}
-(void)centralManager:(CBCentralManager *)m didDisconnectPeripheral:(CBPeripheral *)p error:(NSError *)e {(void)m;(void)p;[self fail:e.localizedDescription?:@"Disconnected"];}
-(void)peripheral:(CBPeripheral *)p didDiscoverServices:(NSError *)e {
 if(e){[self fail:e.localizedDescription];return;}for(CBService *s in p.services)[p discoverCharacteristics:@[[CBUUID UUIDWithString:charID]] forService:s];
}
-(void)peripheral:(CBPeripheral *)p didDiscoverCharacteristicsForService:(CBService *)s error:(NSError *)e {
 if(e){[self fail:e.localizedDescription];return;}for(CBCharacteristic *c in s.characteristics)if([c.UUID isEqual:[CBUUID UUIDWithString:charID]]){characteristic=c;[p setNotifyValue:YES forCharacteristic:c];}
}
-(void)peripheral:(CBPeripheral *)p didUpdateNotificationStateForCharacteristic:(CBCharacteristic *)c error:(NSError *)e {
 if(e || !c.isNotifying){[self fail:e.localizedDescription?:@"Notifications unavailable"];return;}
 limit=(uint16_t)MIN(500,[p maximumWriteValueLengthForType:CBCharacteristicWriteWithoutResponse]);
 if(limit<64){[self fail:@"Frame limit below 64"];return;}
 ls_init(&stream,limit,nonce);ls_set_window(&stream,6);stream.ack_delay_ms=5;uint8_t h[12]={'L','R','H','2'};lr_put32(h+4,nonce);lr_put16(h+8,limit);lr_put16(h+10,2);stage=1;
 [p writeValue:[NSData dataWithBytes:h length:12] forCharacteristic:c type:CBCharacteristicWriteWithResponse];
}
-(void)peripheral:(CBPeripheral *)p didWriteValueForCharacteristic:(CBCharacteristic *)c error:(NSError *)e {
 if(e){[self fail:e.localizedDescription];return;}if(stage==1){stage=2;[p readValueForCharacteristic:c];}
}
-(void)peripheral:(CBPeripheral *)p didUpdateValueForCharacteristic:(CBCharacteristic *)c error:(NSError *)e {
 if(e){[self fail:e.localizedDescription];return;}const uint8_t *b=c.value.bytes;size_t n=c.value.length;
 if(stage==2 && n==12 && !memcmp(b,"LRA2",4) && lr_get32(b+4)==nonce && lr_get16(b+8)==limit && lr_get16(b+10)==2){
 uint8_t h[12]={'L','R','N','1'};memcpy(h+4,b+4,8);stage=3;[p writeValue:[NSData dataWithBytes:h length:12] forCharacteristic:c type:CBCharacteristicWriteWithResponse];return;}
 if(stage==3 && n==12 && !memcmp(b,"LRNA",4) && lr_get32(b+4)==nonce){stage=4;[self log:@"Mac host BLE connected. Choose Become Leader in the Mac game."];return;}
 if(stage==4 && !ls_ingest(&stream,b,n,receiveMessage,(__bridge void *)self,ms()))[self fail:@"Malformed BLE stream frame"];
}

-(BOOL)receive:(const uint8_t *)p size:(size_t)n {
 if(n>=3 && p[0]==RL_BATCH)return lr_batch_receive(p,n,receiveMessage,(__bridge void *)self);
 if(n<3)return NO;
 if(p[0]==0x91 && n==3 && advertisement && !sessionActive){
    sessionActive=YES;NSMutableData *m=[NSMutableData dataWithBytes:(uint8_t[]){0x90,0,0} length:3];[m appendData:advertisement];if(self.messageHandler)self.messageHandler(m);return YES;
 }
 if(p[0]==RL_SEND){
    if(!sessionActive || n<10 || p[3] || memcmp(p+4,(uint8_t[]){169,254,1,1},4) || p[8]!=0x30 || p[9]!=0x39)return NO;
    NSMutableData *d=[NSMutableData dataWithBytes:p length:n];uint8_t *b=d.mutableBytes;b[0]=RL_UDP;b[7]=2;
    if(self.messageHandler)self.messageHandler(d);return YES;
 }
 if(p[0]==RL_LEAVE && n==3){
    uint8_t left[3]={RL_LEFT,p[1],p[2]};if(![self enqueue:[NSData dataWithBytes:left length:3]])return NO;
    sessionActive=NO;advertisement=nil;if(self.resetHandler)self.resetHandler();return YES;
 }
 [self log:[NSString stringWithFormat:@"Lab rejected unsupported command %u",p[0]]];return YES;
}
-(void)tick {
 uint64_t now=ms();if(stage!=4){if(now-began>90000)[self fail:@"BLE connection timed out. Reopen the host app to retry."];return;}
 if(advertisement && !sessionActive && now-offeredAt>10000){[self fail:@"No lab acknowledgement. Open mGBA LDN 0.3.4 on the iPhone, then reopen this host app."];return;}
 if(now-lastAdvert>=500){
    lastAdvert=now;NSData *current=self.advertisementProvider?self.advertisementProvider():nil;
    if(current.length==47 && ![current isEqualToData:advertisement]){
        BOOL newSession=!advertisement || memcmp((const uint8_t *)current.bytes+4,(const uint8_t *)advertisement.bytes+4,16);
        if(newSession && advertisement){
            if(![self enqueue:[NSData dataWithBytes:(uint8_t[]){RL_LEFT,0,0} length:3]])return;
            sessionActive=NO;if(self.resetHandler)self.resetHandler();
        }
        uint8_t opcode=newSession?0x90:0x92;
        NSMutableData *m=[NSMutableData dataWithBytes:(uint8_t[]){opcode,0,0} length:3];[m appendData:current];
        if([self enqueue:m]){advertisement=current;if(newSession)offeredAt=now;}
    }
 }
 /* Batch short records; send a larger datagram alone for stream fragmentation. */
 while(pending.count && stream.count<2){
    LrBatch b;lr_batch_init(&b,limit-LS_HEADER);unsigned count=0;
    for(NSData *d in pending){if(!lr_batch_add(&b,d.bytes,d.length))break;count++;}
    BOOL ok=count?ls_enqueue(&stream,b.bytes,b.size):ls_enqueue(&stream,pending[0].bytes,pending[0].length);
    if(!ok)break;[pending removeObjectsInRange:NSMakeRange(0,count?:1)];
 }
 for(unsigned i=0;i<LS_WINDOW+1 && phone.canSendWriteWithoutResponse;i++){
    uint8_t frame[LR_MAX_FRAME];size_t n=ls_prepare(&stream,frame,sizeof(frame),now);if(!n)break;
    [phone writeValue:[NSData dataWithBytes:frame length:n] forCharacteristic:characteristic type:CBCharacteristicWriteWithoutResponse];ls_commit(&stream,frame,n,now);
 }
 if(now-lastStats>=5000){lastStats=now;[self log:[NSString stringWithFormat:@"Lab BLE queue=%lu flight=%u retries=%llu gaps=%llu",(unsigned long)self.queuedMessages,stream.flying,(unsigned long long)stream.retries,(unsigned long long)stream.gaps]];}
}
@end
