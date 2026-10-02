// AGPL-3.0-or-later. GB-Link gift modules hosted by the Apple Pia adapter.
#import <Foundation/Foundation.h>
@interface GiftEngine : NSObject
@property(nonatomic,copy) BOOL (^send)(NSData *, NSData *);
@property(nonatomic,copy) BOOL (^canSend)(void);
@property(nonatomic,copy) void (^status)(NSString *, NSDictionary *);
@property(nonatomic,readonly) NSArray *catalogue;
@property(nonatomic,readonly) BOOL running;
@property(nonatomic,readonly) NSString *error;
- (instancetype)initWithScript:(NSString *)script;
- (NSDictionary *)importCard:(NSData *)data name:(NSString *)name error:(NSString **)error;
- (BOOL)start:(NSString *)identifier;
- (void)stop;
- (void)tick:(uint32_t)now;
- (NSData *)advertisement;
- (BOOL)configure:(NSData *)metadata now:(uint32_t)now;
- (void)receive:(NSData *)data from:(NSData *)ip;
- (void)decide:(BOOL)sendAgain;
@end
