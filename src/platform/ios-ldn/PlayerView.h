/* MPL-2.0. Apple frontend for the Android LDN player controls and display options. */
#import <UIKit/UIKit.h>
@interface PlayerView : UIView
@property(nonatomic,strong,readonly) UIImageView *screen;
@property(nonatomic,strong,readonly) NSMutableDictionary *settings;
@property(nonatomic,copy) void (^keysChanged)(uint32_t keys);
@property(nonatomic,readonly) uint32_t touchKeys;
- (instancetype)initWithDirectory:(NSURL *)directory;
- (void)clearTouches;
- (void)applySettings;
- (void)saveSettings;
- (UIViewController *)settingsController;
@end
