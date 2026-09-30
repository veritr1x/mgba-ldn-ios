// MIT licence: relay/LICENSE.
#import <UIKit/UIKit.h>
@interface RelayController : UIViewController
@property(nonatomic, readonly) BOOL joined;
@property(nonatomic, readonly) BOOL udpReady;
@property(nonatomic, readonly) NSData *networkInfo;
- (uint16_t)sendDatagram:(NSData *)data slot:(uint8_t)slot address:(NSData *)address port:(uint16_t)port;
- (BOOL)canSendGameDatagram;
- (void)leave;
@end
