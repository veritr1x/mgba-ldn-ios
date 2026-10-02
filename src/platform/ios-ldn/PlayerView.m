/* MPL-2.0. Port of the layout, effects and customization in android-ldn/GameView.java. */
#import "PlayerView.h"
#import "PlayerMath.h"
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <ImageIO/ImageIO.h>
static CGRect rect(MPRect r){return CGRectMake(r.x,r.y,r.w,r.h);}
static UIColor *hexColor(NSString *s){unsigned n=0xFFFFFF;[[NSScanner scannerWithString:s?:@"FFFFFF"] scanHexInt:&n];return [UIColor colorWithRed:((n>>16)&255)/255.0 green:((n>>8)&255)/255.0 blue:(n&255)/255.0 alpha:1];}
@interface PlayerButtonElement : UIAccessibilityElement
@property(nonatomic,copy) BOOL (^activate)(void);
@end
@implementation PlayerButtonElement
- (BOOL)accessibilityActivate {return self.activate?self.activate():NO;}
@end
@interface PlayerView ()
@property(nonatomic,strong) UIImageView *screen,*overlay;
@property(nonatomic,strong) NSMutableDictionary *settings;
@property(nonatomic,strong) NSURL *directory;
@property(nonatomic,strong) NSMutableSet<UITouch *> *touches;
@property(nonatomic) uint32_t touchKeys;
@property(nonatomic) MPLayout layout;
@property(nonatomic) CGSize previousSize;
@property(nonatomic) BOOL effectsDirty;
@property(nonatomic,strong) NSDictionary<NSString *,UIImage *> *backgrounds;
@end
@interface DisplaySettingsController : UITableViewController <UIDocumentPickerDelegate>
@property(nonatomic,weak) PlayerView *player;
@property(nonatomic,copy) NSArray *sections;
@property(nonatomic,copy) NSString *imageKey;
@end
@implementation PlayerView
- (instancetype)initWithDirectory:(NSURL *)directory {
    if(!(self=[super initWithFrame:CGRectZero]))return nil;
    _directory=directory;_settings=[@{@"wireless":@YES,@"adapter":@3,@"fps":@YES,@"counter":@NO,@"relayStatus":@YES,@"controls":@YES,@"opacity":@35,@"colorMode":@0,@"saturation":@100,@"blend":@NO,@"pixel":@0,@"horizontal":@NO,@"vertical":@NO,@"horizontalStrength":@35,@"verticalStrength":@35,@"mirror":@NO,@"portraitColor":@"000000",@"leftColor":@"000000",@"rightColor":@"000000",@"dpad":@"FFFFFF",@"a":@"FFFFFF",@"b":@"FFFFFF",@"l":@"FFFFFF",@"r":@"FFFFFF",@"start":@"FFFFFF",@"select":@"FFFFFF"} mutableCopy];
    NSDictionary *saved=[NSDictionary dictionaryWithContentsOfURL:[directory URLByAppendingPathComponent:@"display.plist"]];
    // Ignore unknown or wrongly typed preferences rather than crashing on an edited file.
    for(NSString *key in _settings.allKeys){id value=saved[key];if([value isKindOfClass:[_settings[key] isKindOfClass:NSString.class]?NSString.class:NSNumber.class])_settings[key]=value;}
    if(![saved[@"adapter"] isKindOfClass:NSNumber.class] && ![_settings[@"wireless"] boolValue])_settings[@"adapter"]=@0;
    _touches=[NSMutableSet new];self.multipleTouchEnabled=YES;self.backgroundColor=UIColor.blackColor;
    _screen=[UIImageView new];_screen.contentMode=UIViewContentModeScaleToFill;_screen.backgroundColor=UIColor.blackColor;
    _screen.layer.magnificationFilter=kCAFilterNearest;_screen.layer.minificationFilter=kCAFilterNearest;_screen.accessibilityLabel=@"Game screen";
    _overlay=[UIImageView new];[self addSubview:_screen];[self addSubview:_overlay];
    [self applySettings];return self;
}
- (void)saveSettings {[_settings writeToURL:[_directory URLByAppendingPathComponent:@"display.plist"] atomically:YES];[self applySettings];}
- (void)applySettings {
    if(![_settings[@"controls"] boolValue])[self clearTouches];
    NSMutableDictionary *images=[NSMutableDictionary new];for(NSString *key in @[@"portrait",@"left",@"right"]){UIImage *im=[UIImage imageWithContentsOfFile:[[_directory URLByAppendingPathComponent:[key stringByAppendingString:@"-background.png"]] path]];if(im)images[key]=im;}_backgrounds=images;
    self.effectsDirty=YES;[self setNeedsLayout];[self setNeedsDisplay];[self.subviews.lastObject setNeedsDisplay];
}
- (void)layoutSubviews {
    [super layoutSubviews];BOOL changed=!CGSizeEqualToSize(self.bounds.size,self.previousSize);if(changed)[self clearTouches];
    _layout=MPComputeLayout(self.bounds.size.width,self.bounds.size.height);_screen.frame=rect(_layout.game);_overlay.frame=_screen.frame;
    if(changed || self.effectsDirty){[self rebuildOverlay];[self rebuildAccessibility];self.effectsDirty=NO;self.previousSize=self.bounds.size;}
    [self setNeedsDisplay];[self.subviews.lastObject setNeedsDisplay];
}
- (void)rebuildAccessibility {
    NSMutableArray *elements=[NSMutableArray new];
    if([_settings[@"controls"] boolValue]){
        NSArray *names=@[@"A",@"B",@"L",@"R",@"Select",@"Start",@"Up",@"Down",@"Left",@"Right"];unsigned bits[]={0,1,9,8,2,3,6,7,5,4};
        for(int i=0;i<10;i++){PlayerButtonElement *e=[[PlayerButtonElement alloc] initWithAccessibilityContainer:self];e.accessibilityLabel=names[i];e.accessibilityTraits=UIAccessibilityTraitButton;
            CGRect r;if(i<6)r=rect(_layout.buttons[i]);else {CGRect d=rect(_layout.dpad);CGFloat a=d.size.width/3;r=CGRectMake(i==8?d.origin.x:i==9?CGRectGetMaxX(d)-a:CGRectGetMidX(d)-a/2,i==6?d.origin.y:i==7?CGRectGetMaxY(d)-a:CGRectGetMidY(d)-a/2,a,a);}
            e.accessibilityFrameInContainerSpace=r;unsigned bit=bits[i];__weak PlayerView *weak=self;e.activate=^BOOL{if(weak.keysChanged){weak.keysChanged(1u<<bit);weak.keysChanged(0);return YES;}return NO;};[elements addObject:e];
        }
    }self.accessibilityElements=elements;
}
- (void)rebuildOverlay {
    int mode=[_settings[@"pixel"] intValue];BOOL horizontal=[_settings[@"horizontal"] boolValue],vertical=[_settings[@"vertical"] boolValue];
    if((!mode && !horizontal && !vertical) || _screen.bounds.size.width<=0){_overlay.image=nil;return;}
    UIGraphicsImageRenderer *renderer=[[UIGraphicsImageRenderer alloc] initWithSize:_screen.bounds.size];
    _overlay.image=[renderer imageWithActions:^(UIGraphicsImageRendererContext *r){
        CGContextRef c=r.CGContext;double pw=self.screen.bounds.size.width/240,ph=self.screen.bounds.size.height/160;
        for(int y=0;y<160;y++)for(int x=0;x<240;x++){
            CGRect cell=CGRectMake(x*pw,y*ph,pw,ph);
            if(mode==1){[[UIColor colorWithWhite:0 alpha:.5] setFill];UIRectFill(CGRectMake(CGRectGetMaxX(cell)-MAX(.25,pw*.18),cell.origin.y,MAX(.25,pw*.18),ph));UIRectFill(CGRectMake(cell.origin.x,CGRectGetMaxY(cell)-MAX(.25,ph*.18),pw,MAX(.25,ph*.18)));}
            else if(mode==2){CGContextSaveGState(c);CGContextClipToRect(c,cell);UIBezierPath *p=[UIBezierPath bezierPathWithRect:cell];[p appendPath:[UIBezierPath bezierPathWithRoundedRect:CGRectInset(cell,pw/16,ph/16) cornerRadius:MIN(pw,ph)*5/16]];p.usesEvenOddFillRule=YES;[[UIColor colorWithWhite:0 alpha:.55] setFill];[p fill];CGContextRestoreGState(c);}
            else if(mode==3){for(int k=0;k<3;k++){[[UIColor colorWithRed:k==0?0:120/255.0 green:k==1?0:120/255.0 blue:k==2?0:120/255.0 alpha:.4] setFill];UIRectFill(CGRectMake(cell.origin.x+k*pw/3,cell.origin.y,pw/3,ph));}}
        }
        if(horizontal){[[UIColor colorWithWhite:0 alpha:MIN(1,MAX(0,[self.settings[@"horizontalStrength"] doubleValue]/100))] setFill];for(int y=0;y<160;y++)UIRectFill(CGRectMake(0,(y+.55)*ph,self.screen.bounds.size.width,ph*.45));}
        if(vertical){[[UIColor colorWithWhite:0 alpha:MIN(1,MAX(0,[self.settings[@"verticalStrength"] doubleValue]/100))] setFill];for(int x=0;x<240;x++)UIRectFill(CGRectMake((x+.55)*pw,0,pw*.45,self.screen.bounds.size.height));}
    }];
}
- (void)drawBackground:(NSString *)key inRect:(CGRect)area mirror:(BOOL)mirror {
    if(area.size.width<=0 || area.size.height<=0)return;
    [hexColor(_settings[[key stringByAppendingString:@"Color"]]) setFill];UIRectFill(area);
    UIImage *image=_backgrounds[mirror?@"left":key];if(!image)return;
    CGContextRef c=UIGraphicsGetCurrentContext();CGContextSaveGState(c);CGContextClipToRect(c,area);
    if(mirror){CGContextTranslateCTM(c,CGRectGetMidX(area)*2,0);CGContextScaleCTM(c,-1,1);}
    double scale=MAX(area.size.width/image.size.width,area.size.height/image.size.height);CGSize sz=CGSizeMake(image.size.width*scale,image.size.height*scale);
    [image drawInRect:CGRectMake(CGRectGetMidX(area)-sz.width/2,CGRectGetMidY(area)-sz.height/2,sz.width,sz.height)];CGContextRestoreGState(c);
}
- (void)drawRect:(CGRect)dirty {
    CGRect game=rect(_layout.game);double w=self.bounds.size.width,h=self.bounds.size.height;
    if(h>=w)[self drawBackground:@"portrait" inRect:CGRectMake(0,CGRectGetMaxY(game),w,h-CGRectGetMaxY(game)) mirror:NO];
    else {[self drawBackground:@"left" inRect:CGRectMake(0,0,game.origin.x,h) mirror:NO];[self drawBackground:@"right" inRect:CGRectMake(CGRectGetMaxX(game),0,w-CGRectGetMaxX(game),h) mirror:[_settings[@"mirror"] boolValue]];}
    // Controls must be above the game image; drawn by a transparent overlay below.
}
- (void)drawControls {
    if(![_settings[@"controls"] boolValue])return;
    double alpha=MAX(0,MIN(1,[_settings[@"opacity"] doubleValue]/100));
    UIColor *base=hexColor(_settings[@"dpad"]);CGRect dp=rect(_layout.dpad);double arm=dp.size.width*.17;
    [[base colorWithAlphaComponent:alpha] setFill];
    [[UIBezierPath bezierPathWithRoundedRect:CGRectMake(CGRectGetMidX(dp)-arm,dp.origin.y,arm*2,dp.size.height) cornerRadius:arm/2] fill];
    [[UIBezierPath bezierPathWithRoundedRect:CGRectMake(dp.origin.x,CGRectGetMidY(dp)-arm,dp.size.width,arm*2) cornerRadius:arm/2] fill];
    [[base colorWithAlphaComponent:MIN(1,alpha+.35)] setFill];
    if(_touchKeys&(1<<6))UIRectFill(CGRectMake(CGRectGetMidX(dp)-arm,dp.origin.y,arm*2,dp.size.height/2-arm));
    if(_touchKeys&(1<<7))UIRectFill(CGRectMake(CGRectGetMidX(dp)-arm,CGRectGetMidY(dp)+arm,arm*2,dp.size.height/2-arm));
    if(_touchKeys&(1<<5))UIRectFill(CGRectMake(dp.origin.x,CGRectGetMidY(dp)-arm,dp.size.width/2-arm,arm*2));
    if(_touchKeys&(1<<4))UIRectFill(CGRectMake(CGRectGetMidX(dp)+arm,CGRectGetMidY(dp)-arm,dp.size.width/2-arm,arm*2));
    NSArray *names=@[@"A",@"B",@"L",@"R",@"SELECT",@"START"],*keys=@[@"a",@"b",@"l",@"r",@"select",@"start"];unsigned bits[]={0,1,9,8,2,3};
    for(int i=0;i<6;i++){CGRect r=rect(_layout.buttons[i]);UIColor *color=hexColor(_settings[keys[i]]);[[color colorWithAlphaComponent:(_touchKeys&(1<<bits[i]))?MIN(1,alpha+.35):alpha] setFill];
        [(i<2?[UIBezierPath bezierPathWithOvalInRect:r]:[UIBezierPath bezierPathWithRoundedRect:r cornerRadius:r.size.height/2]) fill];
        CGFloat red,green,blue;[color getRed:&red green:&green blue:&blue alpha:NULL];UIColor *ink=(red*.299+green*.587+blue*.114)>.59?UIColor.blackColor:UIColor.whiteColor;
        NSDictionary *attrs=@{NSFontAttributeName:[UIFont boldSystemFontOfSize:MAX(10,MIN(r.size.height*.48,MIN(self.bounds.size.width,self.bounds.size.height)*.045))],NSForegroundColorAttributeName:[ink colorWithAlphaComponent:MAX(.5,alpha)]};CGSize sz=[names[i] sizeWithAttributes:attrs];[names[i] drawAtPoint:CGPointMake(CGRectGetMidX(r)-sz.width/2,CGRectGetMidY(r)-sz.height/2) withAttributes:attrs];
    }
}
- (void)updateTouches {uint32_t keys=0;for(UITouch *t in _touches){CGPoint p=[t locationInView:self];keys|=MPKeysAt(_layout,p.x,p.y);}if(![_settings[@"controls"] boolValue])keys=0;if(keys!=_touchKeys){_touchKeys=keys;if(_keysChanged)_keysChanged(keys);[self.subviews.lastObject setNeedsDisplay];}}
- (void)touchesBegan:(NSSet *)touches withEvent:(UIEvent *)event {[_touches unionSet:touches];[self updateTouches];}
- (void)touchesMoved:(NSSet *)touches withEvent:(UIEvent *)event {[self updateTouches];}
- (void)touchesEnded:(NSSet *)touches withEvent:(UIEvent *)event {[_touches minusSet:touches];[self updateTouches];}
- (void)touchesCancelled:(NSSet *)touches withEvent:(UIEvent *)event {[self touchesEnded:touches withEvent:event];}
- (void)clearTouches {[_touches removeAllObjects];[self updateTouches];}
- (UIViewController *)settingsController {DisplaySettingsController *c=[[DisplaySettingsController alloc] initWithStyle:UITableViewStyleInsetGrouped];c.player=self;return c;}
@end
// Keep the original GPU-backed image view, with touch controls composited above it.
@interface PlayerControlsOverlay : UIView
@property(nonatomic,weak) PlayerView *player;
@end
@implementation PlayerControlsOverlay
- (void)drawRect:(CGRect)r {[self.player drawControls];}
@end
@implementation PlayerView (Overlay)
- (void)didMoveToWindow {if(![self.subviews.lastObject isKindOfClass:PlayerControlsOverlay.class]){PlayerControlsOverlay *v=[[PlayerControlsOverlay alloc] initWithFrame:self.bounds];v.player=self;v.userInteractionEnabled=NO;v.backgroundColor=UIColor.clearColor;v.autoresizingMask=UIViewAutoresizingFlexibleWidth|UIViewAutoresizingFlexibleHeight;[self addSubview:v];}}
@end
@implementation DisplaySettingsController
- (void)viewDidLoad {
    [super viewDidLoad];self.title=@"Display settings";
    self.navigationItem.rightBarButtonItem=[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone target:self action:@selector(done)];
    _sections=@[
        @[@[@"fps",@"FPS counter",@0],@[@"counter",@"Frame counter",@0],@[@"relayStatus",@"Relay status",@0]],
        @[@[@"controls",@"On-screen buttons",@0],@[@"opacity",@"Button opacity",@1],@[@"all",@"All button colors",@3],@[@"dpad",@"D-pad color",@3],@[@"a",@"A color",@3],@[@"b",@"B color",@3],@[@"l",@"L color",@3],@[@"r",@"R color",@3],@[@"start",@"Start color",@3],@[@"select",@"Select color",@3]],
        @[@[@"colorMode",@"Color mode",@2],@[@"saturation",@"Saturation",@1],@[@"blend",@"Frame blending",@0],@[@"pixel",@"Pixel effect",@2],@[@"horizontal",@"Horizontal scanlines",@0],@[@"horizontalStrength",@"Horizontal strength",@1],@[@"vertical",@"Vertical scanlines",@0],@[@"verticalStrength",@"Vertical strength",@1]],
        @[@[@"portraitColor",@"Portrait panel color",@3],@[@"portrait",@"Portrait picture",@4],@[@"leftColor",@"Left panel color",@3],@[@"rightColor",@"Right panel color",@3],@[@"mirror",@"Mirror left picture on right",@0],@[@"left",@"Left picture",@4],@[@"right",@"Right picture",@4]]];
}
- (void)done {[self dismissViewControllerAnimated:YES completion:nil];}
- (NSInteger)numberOfSectionsInTableView:(UITableView *)t{return _sections.count;}
- (NSInteger)tableView:(UITableView *)t numberOfRowsInSection:(NSInteger)s{return [_sections[s] count];}
- (NSString *)tableView:(UITableView *)t titleForHeaderInSection:(NSInteger)s{return @[@"On screen",@"Controls",@"Picture",@"Backgrounds"][s];}
- (NSArray *)choices:(NSString *)key {return [key isEqual:@"pixel"]?@[@"Off",@"Pixel grid",@"Round pixels",@"RGB subpixels"]:@[@"Original",@"Muted",@"Vivid",@"Black and white",@"DMG"];}
- (UITableViewCell *)tableView:(UITableView *)t cellForRowAtIndexPath:(NSIndexPath *)p {
    NSArray *row=_sections[p.section][p.row];NSString *key=row[0];int kind=[row[2] intValue];
    UITableViewCell *cell=[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1 reuseIdentifier:nil];cell.textLabel.text=row[1];
    if(kind==0){__weak DisplaySettingsController *weak=self;UISwitch *s=[UISwitch new];s.on=[self.player.settings[key] boolValue];s.accessibilityLabel=row[1];[s addAction:[UIAction actionWithHandler:^(UIAction *a){DisplaySettingsController *owner=weak;owner.player.settings[key]=@(((UISwitch *)a.sender).on);[owner.player saveSettings];}] forControlEvents:UIControlEventValueChanged];cell.accessoryView=s;cell.selectionStyle=UITableViewCellSelectionStyleNone;}
    else if(kind==1){cell.detailTextLabel.text=[NSString stringWithFormat:@"%@%%",self.player.settings[key]];cell.accessoryType=UITableViewCellAccessoryDisclosureIndicator;}
    else if(kind==2){NSArray *options=[self choices:key];NSInteger n=[self.player.settings[key] integerValue];cell.detailTextLabel.text=options[MAX(0,MIN(n,(NSInteger)options.count-1))];cell.accessoryType=UITableViewCellAccessoryDisclosureIndicator;}
    else {cell.detailTextLabel.text=kind==3?self.player.settings[key]:self.player.backgrounds[key]?@"Custom":@"None";cell.accessoryType=UITableViewCellAccessoryDisclosureIndicator;}
    return cell;
}
- (void)presentMenu:(UIAlertController *)menu cell:(UITableViewCell *)cell {menu.popoverPresentationController.sourceView=cell;menu.popoverPresentationController.sourceRect=cell.bounds;[self presentViewController:menu animated:YES completion:nil];}
- (void)setColor:(NSString *)color key:(NSString *)key {if([key isEqual:@"all"])for(NSString *k in @[@"dpad",@"a",@"b",@"l",@"r",@"start",@"select"])self.player.settings[k]=color;else self.player.settings[key]=color;[self.player saveSettings];[self.tableView reloadData];}
- (void)customColor:(NSString *)key {
    UIAlertController *a=[UIAlertController alertControllerWithTitle:@"Custom color" message:@"Enter six hexadecimal digits, such as 6A4FC9." preferredStyle:UIAlertControllerStyleAlert];
    [a addTextFieldWithConfigurationHandler:^(UITextField *f){f.text=self.player.settings[key]?:@"FFFFFF";f.autocapitalizationType=UITextAutocapitalizationTypeAllCharacters;f.autocorrectionType=UITextAutocorrectionTypeNo;}];
    [a addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [a addAction:[UIAlertAction actionWithTitle:@"Apply" style:UIAlertActionStyleDefault handler:^(UIAlertAction *x){NSString *s=a.textFields.firstObject.text.uppercaseString;NSCharacterSet *bad=[[NSCharacterSet characterSetWithCharactersInString:@"0123456789ABCDEF"] invertedSet];if(s.length==6 && [s rangeOfCharacterFromSet:bad].location==NSNotFound)[self setColor:s key:key];else [self showError:@"Use exactly six hexadecimal digits (0–9 and A–F)."];}]];[self presentViewController:a animated:YES completion:nil];
}
- (void)showError:(NSString *)message {UIAlertController *a=[UIAlertController alertControllerWithTitle:@"Display settings" message:message preferredStyle:UIAlertControllerStyleAlert];[a addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];[self presentViewController:a animated:YES completion:nil];}
- (void)tableView:(UITableView *)t didSelectRowAtIndexPath:(NSIndexPath *)p {
    [t deselectRowAtIndexPath:p animated:YES];NSArray *row=_sections[p.section][p.row];NSString *key=row[0];int kind=[row[2] intValue];if(!kind)return;
    if(kind==1){UIAlertController *a=[UIAlertController alertControllerWithTitle:row[1] message:@"Percentage (0–100)" preferredStyle:UIAlertControllerStyleAlert];[a addTextFieldWithConfigurationHandler:^(UITextField *f){f.text=[self.player.settings[key] stringValue];f.keyboardType=UIKeyboardTypeNumberPad;}];[a addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];[a addAction:[UIAlertAction actionWithTitle:@"Apply" style:UIAlertActionStyleDefault handler:^(UIAlertAction *x){NSString *s=a.textFields.firstObject.text;NSScanner *scanner=[NSScanner scannerWithString:s];NSInteger n;if(![scanner scanInteger:&n] || !scanner.isAtEnd || n<0 || n>100){[self showError:@"Enter a number from 0 to 100."];return;}self.player.settings[key]=@(n);[self.player saveSettings];[t reloadData];}]];[self presentViewController:a animated:YES completion:nil];return;}
    UIAlertController *menu=[UIAlertController alertControllerWithTitle:row[1] message:nil preferredStyle:UIAlertControllerStyleActionSheet];
    if(kind==2){NSArray *choices=[self choices:key];for(NSUInteger i=0;i<choices.count;i++)[menu addAction:[UIAlertAction actionWithTitle:choices[i] style:UIAlertActionStyleDefault handler:^(UIAlertAction *a){self.player.settings[key]=@(i);[self.player saveSettings];[t reloadData];}]];}
    if(kind==3){NSArray *names=@[@"White",@"Black",@"GBA Indigo",@"GBC Teal",@"GBC Berry",@"GBC Dandelion",@"GBC Kiwi",@"GBC Grape",@"Miku"],*colors=@[@"FFFFFF",@"000000",@"6A4FC9",@"2BB5B0",@"C2417D",@"F2C230",@"9BD34B",@"8A4FB3",@"00B2A9"];for(NSUInteger i=0;i<names.count;i++)[menu addAction:[UIAlertAction actionWithTitle:names[i] style:UIAlertActionStyleDefault handler:^(UIAlertAction *a){[self setColor:colors[i] key:key];}]];[menu addAction:[UIAlertAction actionWithTitle:@"Custom…" style:UIAlertActionStyleDefault handler:^(UIAlertAction *a){[self customColor:key];}]];}
    if(kind==4){[menu addAction:[UIAlertAction actionWithTitle:@"Import picture…" style:UIAlertActionStyleDefault handler:^(UIAlertAction *a){self.imageKey=key;UIDocumentPickerViewController *picker=[[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[UTTypeImage] asCopy:YES];picker.delegate=self;[self presentViewController:picker animated:YES completion:nil];}]];[menu addAction:[UIAlertAction actionWithTitle:@"Remove picture" style:UIAlertActionStyleDestructive handler:^(UIAlertAction *a){[NSFileManager.defaultManager removeItemAtURL:[self.player.directory URLByAppendingPathComponent:[key stringByAppendingString:@"-background.png"]] error:nil];[self.player applySettings];[t reloadData];}]];}
    [menu addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];[self presentMenu:menu cell:[t cellForRowAtIndexPath:p]];
}
- (void)documentPicker:(UIDocumentPickerViewController *)c didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    NSURL *url=urls.firstObject;if(!url)return;BOOL access=[url startAccessingSecurityScopedResource];
    CGImageSourceRef source=CGImageSourceCreateWithURL((__bridge CFURLRef)url,NULL);
    CGImageRef thumb=source?CGImageSourceCreateThumbnailAtIndex(source,0,(__bridge CFDictionaryRef)@{(id)kCGImageSourceCreateThumbnailFromImageAlways:@YES,(id)kCGImageSourceCreateThumbnailWithTransform:@YES,(id)kCGImageSourceThumbnailMaxPixelSize:@2200}):NULL;
    NSError *error=nil;BOOL ok=thumb && [UIImagePNGRepresentation([UIImage imageWithCGImage:thumb]) writeToURL:[self.player.directory URLByAppendingPathComponent:[self.imageKey stringByAppendingString:@"-background.png"]] options:NSDataWritingAtomic error:&error];
    if(thumb)CGImageRelease(thumb);if(source)CFRelease(source);if(access)[url stopAccessingSecurityScopedResource];
    if(!ok)[self showError:error.localizedDescription?:@"Could not read this picture. Choose a PNG or JPEG image."];else {[self.player applySettings];[self.tableView reloadData];}
}
@end
