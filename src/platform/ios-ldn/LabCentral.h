/* MIT. BLE central for the explicit Mac emulator host laboratory. */
#import <Foundation/Foundation.h>
@interface LabCentral : NSObject
@property(nonatomic,copy) void (^messageHandler)(NSData *);
@property(nonatomic,copy) void (^statusHandler)(NSString *);
@property(nonatomic,copy) void (^resetHandler)(void);
@property(nonatomic,copy) NSData *(^advertisementProvider)(void);
@property(nonatomic,readonly) NSUInteger queuedMessages;
- (void)start;
- (BOOL)enqueue:(NSData *)data;
@end
