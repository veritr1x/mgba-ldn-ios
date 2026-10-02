// AGPL-3.0-or-later. Wonder Card distribution UI.
#import <UIKit/UIKit.h>
@class RelayController;
@interface GiftController : UITableViewController
@property(nonatomic,strong) RelayController *relay;
@property(nonatomic,copy) void (^finished)(void);
@end
