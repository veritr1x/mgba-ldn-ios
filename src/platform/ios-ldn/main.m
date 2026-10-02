#import "LRLog.h"
/* Copyright (c) 2026 mGBA LDN iOS contributors. MPL-2.0. */
#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <CommonCrypto/CommonDigest.h>
#import "RelayController.h"
#import "GameFiles.h"
#import "PlayerView.h"
#import "PlayerMath.h"
#import "GameProfile.h"
#include <mgba/internal/gba/sio/rfu-wrapper-air.h>
#import <GameController/GameController.h>
#include "relay-backend.h"
#import "GiftController.h"
#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba/gba/interface.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/vfs.h>
#include <stdatomic.h>
#include <time.h>

static uint32_t monotonicMs(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (uint32_t)((uint64_t)t.tv_sec*1000+t.tv_nsec/1000000); }
struct AudioStream { struct mAVStream d; unsigned rate; };
static void audioRate(struct mAVStream *s, unsigned rate) { if(rate) ((struct AudioStream *)s)->rate=rate; }

@interface GameController : UIViewController <UIDocumentPickerDelegate> {
    struct mCore *_core;
    struct GBASIORFU _rfu;
    struct GBASIORFUWrapper _wrapper;
    BOOL _wrapperAttached;
    enum IOSPokemonGame _gameProfile;
    enum IOSAdapterChoice _adapter;
    struct GBASIORFUBackend *_backend;
    struct AudioStream _stream;
    mColor _pixels[240*160];
    uint32_t _keys, _pendingKeys;
    uint8_t _displayPixels[240*160*4], _previousPixels[240*160*4];
    BOOL _previousValid;
    double _fpsTime;unsigned _fpsFrames;
    BOOL _attached, _paused, _importingSave, _saveLoaded;
    double _accumulator, _lastTime, _phase, _sumL, _sumR;
    unsigned _samples, _frames;
    float _ring[32768*2];
    _Atomic(uint32_t) _readAudio, _writeAudio;
}
@property(nonatomic,strong) RelayController *relay;
@property(nonatomic,strong) GiftController *gifts;
@property(nonatomic,strong) UIImageView *screen;
@property(nonatomic,strong) PlayerView *player;
@property(nonatomic,strong) UIButton *menuButton;
@property(nonatomic,strong) UILabel *counters;
@property(nonatomic,strong) NSMutableArray<NSString *> *recentEvents;
@property(nonatomic,strong) UILabel *status;
@property(nonatomic,strong) UILabel *titleLabel;
@property(nonatomic,strong) UIButton *pauseButton;
@property(nonatomic,strong) CADisplayLink *display;
@property(nonatomic,strong) AVAudioEngine *audio;
@property(nonatomic,strong) AVAudioSourceNode *source;
@property(nonatomic,strong) NSURL *romURL;
@property(nonatomic,strong) NSURL *saveURL;
@property(nonatomic,strong) NSURL *logURL;
@property(nonatomic,strong) LRLog *logger;
@property(nonatomic,strong) LRLog *captureLogger;
@property(nonatomic,strong) NSURL *captureURL;
@property(nonatomic) uint64_t captureSequence;
@property(nonatomic) BOOL captureSession;
- (void)capture:(NSDictionary *)event;
- (void)record:(NSString *)text;
@end
static bool canSendPia(void *ctx) { return [((__bridge GameController *)ctx).relay canSendGameDatagram]; }
static bool sendPia(void *ctx, const uint8_t ip[4], const uint8_t *p, size_t n) {
    GameController *g=(__bridge GameController *)ctx;
    NSData *data=[NSData dataWithBytes:p length:n],*address=[NSData dataWithBytes:ip length:4];
    BOOL accepted=[g.relay sendDatagram:data slot:0 address:address port:12345]!=0;
    [g capture:@{@"event":@"datagram",@"direction":@"tx",@"accepted":@(accepted),@"ip_b64":[address base64EncodedStringWithOptions:0],@"port":@12345,@"payload_b64":[data base64EncodedStringWithOptions:0],@"length":@(n)}];
    return accepted;
}
static void logPia(void *ctx, const char *text) { [(__bridge GameController *)ctx record:[NSString stringWithUTF8String:text]]; }

@implementation GameController
- (NSURL *)documents {
    NSArray *args=NSProcessInfo.processInfo.arguments;NSUInteger index=[args indexOfObject:@"--data-dir"];
    if(index!=NSNotFound && index+1<args.count){
        NSURL *dir=[NSURL fileURLWithPath:args[index+1] isDirectory:YES];
        [NSFileManager.defaultManager createDirectoryAtURL:dir withIntermediateDirectories:YES attributes:nil error:nil];return dir;
    }
#if TARGET_OS_MACCATALYST
    NSURL *dir=[[NSFileManager.defaultManager URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask].firstObject URLByAppendingPathComponent:[[NSBundle.mainBundle objectForInfoDictionaryKey:@"MGBALabHost"] boolValue]?@"mGBA LDN Host Lab":@"mGBA LDN"];
    [NSFileManager.defaultManager createDirectoryAtURL:dir withIntermediateDirectories:YES attributes:nil error:nil]; return dir;
#else
    return [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
#endif
}
- (void)capture:(NSDictionary *)event {
    if(!self.captureLogger)return;
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    NSMutableDictionary *row=[event mutableCopy];row[@"seq"]=@(self.captureSequence++);
    row[@"monotonic_ns"]=@((uint64_t)t.tv_sec*1000000000+(uint64_t)t.tv_nsec);row[@"emulated_frame"]=@(_frames);
    NSData *json=[NSJSONSerialization dataWithJSONObject:row options:0 error:nil];
    [self.captureLogger append:json ? [[[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding] stringByAppendingString:@"\n"] : @"CAPTURE ERROR\n"];
}
- (void)record:(NSString *)text {
    if ([text hasPrefix:@"TRACE "] && ![NSProcessInfo.processInfo.arguments containsObject:@"--trace-pia"]) return;
    if (![text hasPrefix:@"TRACE "]) {self.status.text=text;[self capture:@{@"event":@"game_event",@"text":text}];}
    NSString *line=[NSString stringWithFormat:@"%@ %@\n",NSDate.date,text];
    if(![text hasPrefix:@"TRACE "]){[self.recentEvents addObject:line];if(self.recentEvents.count>200)[self.recentEvents removeObjectAtIndex:0];}
    if([[NSBundle.mainBundle objectForInfoDictionaryKey:@"MGBADiagnostics"] boolValue]) {
        if(!self.logger)self.logger=[[LRLog alloc] initWithURL:self.logURL];
        [self.logger append:line];
    }
    if (![text hasPrefix:@"TRACE "]) NSLog(@"mGBA LDN: %@",text);
}
- (UIButton *)actionButton:(NSString *)title selector:(SEL)action {
    UIButton *b=[UIButton buttonWithType:UIButtonTypeSystem];
    b.configuration=[UIButtonConfiguration borderedButtonConfiguration];
    [b setTitle:title forState:UIControlStateNormal];
    [b addTarget:self action:action forControlEvents:UIControlEventTouchUpInside]; return b;
}
- (void)viewDidLoad {
    [super viewDidLoad]; self.view.backgroundColor=UIColor.systemBackgroundColor;
    self.logURL=[[self documents] URLByAppendingPathComponent:@"game.log"];
    if([NSProcessInfo.processInfo.arguments containsObject:@"--capture-trade"] ||
       [[NSBundle.mainBundle objectForInfoDictionaryKey:@"LDNRelayCaptureTrade"] boolValue]){
        self.captureURL=[[self documents] URLByAppendingPathComponent:[NSString stringWithFormat:@"trade-capture-%@.jsonl",NSUUID.UUID.UUIDString]];
        self.captureLogger=[[LRLog alloc] initWithURL:self.captureURL];
        [self capture:@{@"event":@"capture_start",@"schema":@1,@"clock_scope":@"App callback boundaries, not radio timestamps",@"transport":[NSBundle.mainBundle objectForInfoDictionaryKey:@"LDNRelayTransport"]?:@"ble"}];
        NSLog(@"Trade capture: %@",self.captureURL.path);
    }
    self.recentEvents=[NSMutableArray new];
    self.player=[[PlayerView alloc] initWithDirectory:[self documents]];self.player.translatesAutoresizingMaskIntoConstraints=NO;
    self.screen=self.player.screen;self.titleLabel=[UILabel new];self.titleLabel.text=@"mGBA LDN";
    __weak GameController *inputOwner=self;
    self.player.keysChanged=^(uint32_t keys){GameController *g=inputOwner;if(g)g->_pendingKeys|=keys;};
    [self.view addSubview:self.player];UILayoutGuide *safe=self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[[self.player.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor],[self.player.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor],[self.player.topAnchor constraintEqualToAnchor:safe.topAnchor],[self.player.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor]]];
    self.view.backgroundColor=UIColor.blackColor;
    self.menuButton=[UIButton buttonWithType:UIButtonTypeSystem];self.menuButton.translatesAutoresizingMaskIntoConstraints=NO;
    self.menuButton.configuration=[UIButtonConfiguration tintedButtonConfiguration];self.menuButton.configuration.baseBackgroundColor=UIColor.blackColor;self.menuButton.tintColor=UIColor.whiteColor;
    [self.menuButton setImage:[UIImage systemImageNamed:@"line.3.horizontal"] forState:UIControlStateNormal];self.menuButton.accessibilityLabel=@"Game menu";
    self.menuButton.showsMenuAsPrimaryAction=YES;[self.view addSubview:self.menuButton];
    self.menuButton.menu=[UIMenu menuWithChildren:@[[UIDeferredMenuElement elementWithUncachedProvider:^(void (^completion)(NSArray<UIMenuElement *> *)){
        GameController *g=inputOwner;[g clearInput];completion([g menuActions]);
    }]]];
    self.status=[UILabel new];self.status.translatesAutoresizingMaskIntoConstraints=NO;self.status.numberOfLines=2;self.status.font=[UIFont systemFontOfSize:12];self.status.textColor=UIColor.whiteColor;self.status.backgroundColor=[UIColor colorWithWhite:0 alpha:.6];
    self.counters=[UILabel new];self.counters.translatesAutoresizingMaskIntoConstraints=NO;self.counters.font=[UIFont monospacedDigitSystemFontOfSize:12 weight:UIFontWeightRegular];self.counters.textColor=UIColor.whiteColor;self.counters.backgroundColor=[UIColor colorWithWhite:0 alpha:.6];
    [self.view addSubview:self.status];[self.view addSubview:self.counters];
    [NSLayoutConstraint activateConstraints:@[[self.menuButton.topAnchor constraintEqualToAnchor:safe.topAnchor constant:6],[self.menuButton.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-6],[self.menuButton.widthAnchor constraintEqualToConstant:48],[self.menuButton.heightAnchor constraintEqualToConstant:40],[self.counters.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:6],[self.counters.topAnchor constraintEqualToAnchor:safe.topAnchor constant:6],[self.counters.trailingAnchor constraintLessThanOrEqualToAnchor:self.menuButton.leadingAnchor constant:-6],[self.status.leadingAnchor constraintEqualToAnchor:self.counters.leadingAnchor],[self.status.topAnchor constraintEqualToAnchor:self.counters.bottomAnchor constant:2],[self.status.trailingAnchor constraintLessThanOrEqualToAnchor:self.menuButton.leadingAnchor constant:-6]]];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(bound:) name:@"LDNRelayBound" object:self.relay];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(lost:) name:@"LDNRelayLost" object:self.relay];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(datagram:) name:@"LDNRelayDatagram" object:self.relay];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(inactive:) name:UIApplicationWillResignActiveNotification object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(active:) name:UIApplicationDidBecomeActiveNotification object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(hostMembers:) name:@"LDNHostMembers" object:self.relay];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(labAdvertisement:) name:@"LDNLabAdvertisement" object:self.relay];
    __weak GameController *weakGame=self;
    self.relay.labAdvertisementProvider=^NSData *{
        GameController *g=weakGame;if(!g || !g->_backend)return nil;
        uint8_t m[IOS_LAB_METADATA_SIZE];return IOSRelayLabAdvertisement(g->_backend,m)?[NSData dataWithBytes:m length:sizeof(m)]:nil;
    };
    self.relay.nativeAdvertisementProvider=^NSData *{GameController *g=weakGame;if(!g || !g->_backend || g->_paused || g->_adapter==IOS_ADAPTER_OFF)return nil;uint8_t ad[122];return IOSRelayNativeAdvertisement(g->_backend,ad)?[NSData dataWithBytes:ad length:122]:nil;};
    [self startAudio];
    self.display=[CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
    self.display.preferredFrameRateRange=CAFrameRateRangeMake(60,60,60);
    [self.display addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    UIApplication.sharedApplication.idleTimerDisabled=YES;
    [self record:@"Open a .gba file to play. Use the game's save menu to keep your progress."];
    NSArray *args=NSProcessInfo.processInfo.arguments;
    NSUInteger romArg=[args indexOfObject:@"--rom"];
    if(romArg!=NSNotFound && romArg+1<args.count) { [self importROMURL:[NSURL fileURLWithPath:args[romArg+1]]];return; }
    NSString *last=[args containsObject:@"--data-dir"]?nil:[NSUserDefaults.standardUserDefaults stringForKey:@"lastROM"];
    if(last) [self loadROM:[[self documents] URLByAppendingPathComponent:last]];
    else {
        NSURL *incoming=[[self documents] URLByAppendingPathComponent:@"Import.gba"];
        if([NSFileManager.defaultManager fileExistsAtPath:incoming.path]) [self importROMURL:incoming];
    }
}
- (BOOL)canBecomeFirstResponder { return YES; }
- (void)viewDidAppear:(BOOL)animated { [super viewDidAppear:animated];[self becomeFirstResponder]; }
- (int)bitForPress:(UIPress *)press {
    switch(press.key.keyCode) {
        case UIKeyboardHIDUsageKeyboardZ:return 0;case UIKeyboardHIDUsageKeyboardX:return 1;
        case UIKeyboardHIDUsageKeyboardReturnOrEnter:return 3;case UIKeyboardHIDUsageKeyboardRightShift:return 2;
        case UIKeyboardHIDUsageKeyboardRightArrow:return 4;case UIKeyboardHIDUsageKeyboardLeftArrow:return 5;
        case UIKeyboardHIDUsageKeyboardUpArrow:return 6;case UIKeyboardHIDUsageKeyboardDownArrow:return 7;
        case UIKeyboardHIDUsageKeyboardS:return 8;case UIKeyboardHIDUsageKeyboardA:return 9;default:return -1;
    }
}
- (void)pressesBegan:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
    for(UIPress *p in presses) { int bit=[self bitForPress:p];if(bit>=0){_keys|=1u<<bit;_pendingKeys|=1u<<bit;}else [super pressesBegan:[NSSet setWithObject:p] withEvent:event]; }
}
- (void)pressesEnded:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
    for(UIPress *p in presses) { int bit=[self bitForPress:p];if(bit>=0)_keys&=~(1u<<bit);else [super pressesEnded:[NSSet setWithObject:p] withEvent:event]; }
}
- (void)pressesCancelled:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event { _keys=0;_pendingKeys=0; }
- (void)clearInput {_keys=0;_pendingKeys=0;[self.player clearTouches];}
- (uint32_t)controllerKeys {
    if(self.presentedViewController || !self.view.window.isKeyWindow)return 0;
    uint32_t keys=0;for(GCController *controller in GCController.controllers){GCExtendedGamepad *pad=controller.extendedGamepad;if(!pad)continue;
        // Physical east = GBA A, south = GBA B, matching the Android layout.
        if(pad.buttonB.isPressed)keys|=1<<0;if(pad.buttonA.isPressed)keys|=1<<1;
        if(pad.buttonOptions.isPressed)keys|=1<<2;if(pad.buttonMenu.isPressed)keys|=1<<3;
        if(pad.dpad.right.isPressed || pad.leftThumbstick.xAxis.value>.5)keys|=1<<4;
        if(pad.dpad.left.isPressed || pad.leftThumbstick.xAxis.value<-.5)keys|=1<<5;
        if(pad.dpad.up.isPressed || pad.leftThumbstick.yAxis.value>.5)keys|=1<<6;
        if(pad.dpad.down.isPressed || pad.leftThumbstick.yAxis.value<-.5)keys|=1<<7;
        if(pad.rightShoulder.isPressed)keys|=1<<8;if(pad.leftShoulder.isPressed)keys|=1<<9;
    }return keys;
}
- (NSArray<UIMenuElement *> *)menuActions {
    NSArray *titles=@[@"Open game",@"Import save",@"Export save",_paused?@"Resume":@"Pause",@"Reset game",@"Switch multiplayer",@"Wonder Card gifts",@"Wireless adapter",@"Display settings",@"Export diagnostic log",@"About"];
    NSArray *selectors=@[@"openGame",@"importSave",@"exportSave",@"togglePause",@"resetGame",@"showMultiplayer",@"showGifts",@"showWirelessAdapter",@"showDisplaySettings",@"exportDiagnostics",@"showAbout"];
    NSMutableArray *actions=[NSMutableArray new];for(NSUInteger i=0;i<titles.count;i++){NSString *sel=selectors[i];UIAction *action=[UIAction actionWithTitle:titles[i] image:nil identifier:nil handler:^(UIAction *a){[UIApplication.sharedApplication sendAction:NSSelectorFromString(sel) to:self from:nil forEvent:nil];}];if(!_core && i>=1 && i<=4)action.attributes=UIMenuElementAttributesDisabled;[actions addObject:action];}return actions;
}
- (void)showGifts {
    if(self.relay.sessionActive){[self record:@"Disconnect or cancel multiplayer before opening Wonder Cards."];return;}
    if([[NSBundle.mainBundle objectForInfoDictionaryKey:@"MGBALabHost"] boolValue]){[self record:@"Use the standard app for Wonder Cards, not the emulator host lab."];return;}
    if(_core && !_paused)[self togglePause];
    [self clearInput];[self.relay loadViewIfNeeded];
    NSData *(^provider)(void)=[self.relay.nativeAdvertisementProvider copy];
    NSString *instructions=self.relay.gameInstructions;BOOL joinOnly=self.relay.joinOnly;
    GiftController *gifts=[[GiftController alloc] initWithStyle:UITableViewStyleInsetGrouped];gifts.relay=self.relay;self.gifts=gifts;
    __weak GameController *weak=self;
    gifts.finished=^{GameController *g=weak;g.relay.nativeAdvertisementProvider=provider;g.relay.gameInstructions=instructions;g.relay.joinOnly=joinOnly;[g dismissViewControllerAnimated:YES completion:^{g.gifts=nil;}];};
    UINavigationController *nav=[[UINavigationController alloc] initWithRootViewController:gifts];nav.modalPresentationStyle=UIModalPresentationFullScreen;[self presentViewController:nav animated:YES completion:nil];
}
- (void)resetGame {
    if(self.relay.joined){[self record:@"Disconnect multiplayer before resetting the game."];return;}
    UIAlertController *a=[UIAlertController alertControllerWithTitle:@"Reset game?" message:@"Progress since your last in-game save will be lost." preferredStyle:UIAlertControllerStyleAlert];
    [a addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [a addAction:[UIAlertAction actionWithTitle:@"Reset" style:UIAlertActionStyleDestructive handler:^(UIAlertAction *action){if(self.romURL)[self loadROM:self.romURL];}]];[self presentViewController:a animated:YES completion:nil];
}
- (void)showWirelessAdapter {
    if(self.relay.joined){[self record:@"Disconnect multiplayer before changing the wireless adapter."];return;}
    UIAlertController *a=[UIAlertController alertControllerWithTitle:@"Wireless adapter" message:@"Automatic uses wireless for FireRed/LeafGreen and Emerald, or the cable wrapper for Ruby/Sapphire. Changing adapters restarts the game from its last in-game save." preferredStyle:UIAlertControllerStyleActionSheet];
    NSArray *labels=@[@"Off",@"Wireless → Switch relay",@"Cable wrapper → Switch relay",@"Automatic (recommended)"];
    for(NSNumber *choice in @[@3,@1,@2,@0]){[a addAction:[UIAlertAction actionWithTitle:labels[choice.unsignedIntegerValue] style:UIAlertActionStyleDefault handler:^(UIAlertAction *action){self.player.settings[@"adapter"]=choice;[self.player saveSettings];if(self.romURL)[self loadROM:self.romURL];}]];}
    [a addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];a.popoverPresentationController.sourceView=self.menuButton;a.popoverPresentationController.sourceRect=self.menuButton.bounds;[self presentViewController:a animated:YES completion:nil];
}
- (void)showMultiplayer {
    if(_adapter==IOS_ADAPTER_OFF){[self record:@"Choose Wireless adapter → Automatic in the game menu first."];return;}
    [self clearInput];self.relay.title=@"Switch multiplayer";self.relay.navigationItem.rightBarButtonItem=[[UIBarButtonItem alloc] initWithTitle:@"Play" style:UIBarButtonItemStyleDone target:self action:@selector(closePanel)];
    UINavigationController *nav=[[UINavigationController alloc] initWithRootViewController:self.relay];nav.modalPresentationStyle=UIModalPresentationFullScreen;[self presentViewController:nav animated:YES completion:nil];
}
- (void)closePanel {[self dismissViewControllerAnimated:YES completion:^{[self becomeFirstResponder];}];}
- (void)showDisplaySettings {[self clearInput];UINavigationController *nav=[[UINavigationController alloc] initWithRootViewController:[self.player settingsController]];[self presentViewController:nav animated:YES completion:nil];}
- (void)exportDiagnostics {
    NSString *report=[NSString stringWithFormat:@"mGBA LDN %@\nRecent game events (no ROM, save or packet capture).\n%@",[NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"],[self.recentEvents componentsJoinedByString:@""]];
    report=[report stringByReplacingOccurrencesOfString:[self documents].path withString:@"<app data>"];
    NSURL *url=[[NSURL fileURLWithPath:NSTemporaryDirectory()] URLByAppendingPathComponent:@"mGBA-LDN-diagnostics.txt"];NSError *error=nil;
    if(![report writeToURL:url atomically:YES encoding:NSUTF8StringEncoding error:&error]){[self record:error.localizedDescription];return;}
    [self presentViewController:[[UIDocumentPickerViewController alloc] initForExportingURLs:@[url] asCopy:YES] animated:YES completion:nil];
}
- (void)showAbout {
    UIAlertController *a=[UIAlertController alertControllerWithTitle:@"mGBA LDN" message:@"Apple frontend by veritr1x. Wonder Card catalogue and gift protocol from the open-source GB-Link project, including Project Wonder by Goppier and the original card authors. No endorsement is implied.\n\nBased on mGBA and Gr3nSkyDragon's mGBA LDN, with controls and display options adapted from its Android frontend.\n\nSwitch multiplayer uses LDN Relay with approval on the modified Switch. FireRed / LeafGreen, Emerald, and Ruby/Sapphire via the cable wrapper. One guest; new game paths need console testing.\n\nKeyboard: arrows, Z = A, X = B, Return = Start, right Shift = Select, A = L, S = R.\nGamepad: east = A, south = B, shoulders = L/R, Menu = Start, Options = Select.\n\nSource and licenses are included in the download. No games are bundled." preferredStyle:UIAlertControllerStyleAlert];[a addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];[self presentViewController:a animated:YES completion:nil];
}
- (void)startAudio {
    NSError *error=nil;
    [AVAudioSession.sharedInstance setCategory:AVAudioSessionCategoryPlayback mode:AVAudioSessionModeDefault options:0 error:&error];
    [AVAudioSession.sharedInstance setActive:YES error:&error];
    self.audio=[AVAudioEngine new];
    AVAudioFormat *format=[[AVAudioFormat alloc] initStandardFormatWithSampleRate:32768 channels:2];
    __weak GameController *weak=self;
    self.source=[[AVAudioSourceNode alloc] initWithFormat:format renderBlock:^OSStatus(BOOL *silence,const AudioTimeStamp *stamp,AVAudioFrameCount count,AudioBufferList *out) {
        GameController *g=weak; if(!g) return noErr;
        uint32_t r=atomic_load(&g->_readAudio),w=atomic_load(&g->_writeAudio);
        *silence=r==w;
        for(unsigned i=0;i<count;++i) {
            BOOL have=r!=w;
            for(unsigned ch=0;ch<out->mNumberBuffers;++ch) ((float *)out->mBuffers[ch].mData)[i]=have?g->_ring[(r&32767)*2+(ch&1)]:0;
            if(have) ++r;
        }
        atomic_store(&g->_readAudio,r); return noErr;
    }];
    [self.audio attachNode:self.source];[self.audio connect:self.source to:self.audio.mainMixerNode format:format];
    [self.audio startAndReturnError:&error];
    if(error) [self record:[@"Audio: " stringByAppendingString:error.localizedDescription]];
}
- (void)drainAudio {
    int16_t source[16384*2];
    struct mAudioBuffer *buf=_core->getAudioBuffer(_core);
    size_t n=mAudioBufferRead(buf,source,MIN(mAudioBufferAvailable(buf),16384));
    uint32_t w=atomic_load(&_writeAudio),r=atomic_load(&_readAudio);
    double ratio=32768.0/MAX(_stream.rate,32768u);
    for(size_t i=0;i<n;++i) {
        _sumL+=source[i*2];_sumR+=source[i*2+1];++_samples;_phase+=ratio;
        if(_phase>=1.0) {
            _phase-=1.0;
            if(w-r<4096) { _ring[(w&32767)*2]=_sumL/(_samples*32768.0);_ring[(w&32767)*2+1]=_sumR/(_samples*32768.0);++w; }
            _sumL=_sumR=0;_samples=0;
        }
    }
    atomic_store(&_writeAudio,w);
}
- (BOOL)save {
    if(!_core || !_saveLoaded || !self.saveURL) return YES;
    void *bytes=NULL;size_t n=_core->savedataClone(_core,&bytes);
    if(n && bytes) {
        NSError *error=nil;
        BOOL ok=[[NSData dataWithBytesNoCopy:bytes length:n freeWhenDone:YES] writeToURL:self.saveURL options:NSDataWritingAtomic error:&error];
        if(!ok) [self record:[@"Save failed: " stringByAppendingString:error.localizedDescription?:@"Could not write the save."]];
        return ok;
    } else free(bytes);
    return YES;
}
- (BOOL)unload {
    if(![self save])return NO;
    if(_core) {
        if(_wrapperAttached){_core->setPeripheral(_core,mPERIPH_GBA_LINK_PORT,NULL);GBASIORFUWrapperDestroy(&_wrapper);_backend=NULL;_wrapperAttached=NO;}
        else if(_attached) { _core->setPeripheral(_core,mPERIPH_GBA_LINK_PORT,NULL);GBASIORFUDestroy(&_rfu);GBASIORFUBackendDestroy(_backend);_backend=NULL;_attached=NO; }
        _core->unloadROM(_core);mCoreConfigDeinit(&_core->config);_core->deinit(_core);_core=NULL;
    }
    _saveLoaded=NO;self.romURL=nil;self.saveURL=nil;
    return YES;
}
- (BOOL)loadROM:(NSURL *)url {
    if(self.relay.joined) { [self record:@"Leave the relay session before changing games or saves."];return NO; }
    if(![self unload])return NO;
    _core=mCoreFind(url.fileSystemRepresentation);
    if(!_core || _core->platform(_core)!=mPLATFORM_GBA) { if(_core) free(_core);_core=NULL;[self record:@"This file is not a supported GBA ROM."];return NO; }
    mCoreInitConfig(_core,NULL);
    if(!_core->init(_core)) { mCoreConfigDeinit(&_core->config);free(_core);_core=NULL;[self record:@"Could not start the emulator."];return NO; }
    mCoreConfigSetDefaultIntValue(&_core->config,"volume",0x100);
    mCoreConfigSetDefaultIntValue(&_core->config,"mute",0);
    mCoreLoadConfig(_core);
    _core->setVideoBuffer(_core,_pixels,240); _core->setAudioBufferSize(_core,16384);
    memset(&_stream,0,sizeof(_stream));_stream.d.audioRateChanged=audioRate;
    _core->setAVStream(_core,&_stream.d);
    if(!mCoreLoadFile(_core,url.fileSystemRepresentation)) { [self unload];[self record:@"Could not load ROM."];return NO; }
    self.romURL=url;self.saveURL=[[url URLByDeletingLastPathComponent] URLByAppendingPathComponent:@"game.sav"];
    NSError *saveError=nil;
    NSData *save=[NSData dataWithContentsOfURL:self.saveURL options:0 error:&saveError];
    if(!save && [NSFileManager.defaultManager fileExistsAtPath:self.saveURL.path]) {
        [self unload];[self record:[@"Could not read the save: " stringByAppendingString:saveError.localizedDescription]];return NO;
    }
    if(save.length) {
        NSString *name=[NSString stringWithFormat:@"backup-%@.sav",NSUUID.UUID.UUIDString];
        if(![save writeToURL:[[url URLByDeletingLastPathComponent] URLByAppendingPathComponent:name] options:NSDataWritingAtomic error:&saveError]) {
            [self unload];[self record:@"Could not back up the save. Free some storage and try again."];return NO;
        }
    }
    /* Keep the core's save in memory; atomically persist snapshots in our own sandbox. */
    struct VFile *vf=VFileMemChunk(save.bytes,save.length);
    if(!vf || !_core->loadSave(_core,vf)) { if(vf) vf->close(vf);[self unload];[self record:@"Could not load the save. The saved file was left unchanged."];return NO; }
    _saveLoaded=YES;
    _core->reset(_core);
    struct mGameInfo info;_core->getGameInfo(_core,&info);_gameProfile=IOSGameProfile(info.code);
    _adapter=IOSResolveAdapter([self.player.settings[@"adapter"] intValue],_gameProfile);
    if(_adapter!=IOS_ADAPTER_OFF){
        _backend=IOSRelayCreate(sendPia,canSendPia,logPia,(__bridge void *)self);
        if(!_backend) { [self unload];[self record:@"Could not create the relay adapter."];return NO; }
        IOSRelayEnableNativeHost(_backend,!IOSAdapterJoinOnly(_adapter,_gameProfile) && ![[NSBundle.mainBundle objectForInfoDictionaryKey:@"MGBALabHost"] boolValue]);
        IOSRelaySetLabHost(_backend,!IOSAdapterJoinOnly(_adapter,_gameProfile) && [[NSBundle.mainBundle objectForInfoDictionaryKey:@"MGBALabHost"] boolValue]);
        NSURL *trace=self.captureURL?[[self.captureURL URLByDeletingPathExtension] URLByAppendingPathExtension:@"rfu.log"]:[[self documents] URLByAppendingPathComponent:@"rfu.log"];
        const char *tracePath=[NSProcessInfo.processInfo.arguments containsObject:@"--trace-rfu"]?trace.fileSystemRepresentation:NULL;
        if(_adapter==IOS_ADAPTER_CABLE){
            GBASIORFUWrapperCreate(&_wrapper,"switch-relay");
            // The air translator owns this backend on both success and failure.
            if(!GBASIORFUWrapperAttachAirBackend(&_wrapper,_backend,"switch-relay",tracePath)){
                _backend=NULL;GBASIORFUWrapperDestroy(&_wrapper);[self unload];[self record:@"Could not attach the cable-to-wireless translator."];return NO;
            }
            _wrapperAttached=YES;_core->setPeripheral(_core,mPERIPH_GBA_LINK_PORT,&_wrapper.d);
        }else{
            GBASIORFUCreate(&_rfu,_backend);if(tracePath)GBASIORFUSetTraceFile(&_rfu,tracePath);
            _attached=YES;_core->setPeripheral(_core,mPERIPH_GBA_LINK_PORT,&_rfu.d);
        }
    }
    self.relay.joinOnly=IOSAdapterJoinOnly(_adapter,_gameProfile);
    NSString *setup=@"Open LDN Relay through Album on the modified Switch, press A to connect, then approve this companion. Keep this app visible and unlocked.";
    if(_adapter==IOS_ADAPTER_CABLE)self.relay.gameInstructions=[setup stringByAppendingString:@"\n\nRuby / Sapphire: FireRed or LeafGreen on the stock Switch must host Direct Corner → Become Leader. Tap Find game here, join its room, then tap Play and speak to the middle Cable Club Trade Center attendant in Ruby/Sapphire. The wrapper joins the wireless host for you.\n\nFinish the host's Sevii Islands trading quest first. Cable-wrapper trading is new on Apple and needs console verification."];
    else if(_gameProfile==IOS_GAME_EMERALD)self.relay.gameInstructions=[setup stringByAppendingString:@"\n\nEmerald: use the Wireless Club Trade Center and Join Group while FireRed/LeafGreen hosts Direct Corner on the stock console. The FRLG save must have completed the Sevii Islands trading quest. Normal game trading restrictions still apply.\n\nEmerald uses the upstream RFU driver; this Apple path needs console verification."];
    else self.relay.gameInstructions=[setup stringByAppendingString:@"\n\nFireRed / LeafGreen: open Direct Corner in both games. Choose Become Leader on the host and Join Group on the guest. Tap Find game to join, or Host game to lead from this app."];
    _stream.rate=_core->audioSampleRate(_core);_phase=_sumL=_sumR=0;_samples=0;
    _frames=0;[self clearInput];_previousValid=NO;_fpsTime=0;_fpsFrames=0;_lastTime=0;_accumulator=0;_paused=NO;
    [self.pauseButton setTitle:@"Pause" forState:UIControlStateNormal];
    NSString *relative=[url.path substringFromIndex:[self documents].path.length+1];
    if(![NSProcessInfo.processInfo.arguments containsObject:@"--data-dir"])[NSUserDefaults.standardUserDefaults setObject:relative forKey:@"lastROM"];
    self.titleLabel.text=url.lastPathComponent.stringByDeletingPathExtension;
    [self record:_adapter==IOS_ADAPTER_CABLE?@"Ruby/Sapphire cable wrapper · Switch hosts":_gameProfile==IOS_GAME_EMERALD?@"Emerald wireless adapter":@"Ready"];
    return YES;
}
- (void)tick:(CADisplayLink *)display {
    self.status.hidden=![self.player.settings[@"relayStatus"] boolValue];
    if(!_core || _paused) { _lastTime=0;self.counters.text=_paused?@"Paused":@"Open game from the menu ☰";return; }
    if(!_fpsTime)_fpsTime=display.timestamp;
    if(display.timestamp-_fpsTime>=.5){NSMutableArray *parts=[NSMutableArray new];if([self.player.settings[@"fps"] boolValue])[parts addObject:[NSString stringWithFormat:@"%.1f FPS",(_frames-_fpsFrames)/(display.timestamp-_fpsTime)]];if([self.player.settings[@"counter"] boolValue])[parts addObject:[NSString stringWithFormat:@"Frame %u",_frames]];self.counters.text=[parts componentsJoinedByString:@" · "];_fpsTime=display.timestamp;_fpsFrames=_frames;}

    if(!_lastTime) _lastTime=display.timestamp;
    _accumulator+=MIN(display.timestamp-_lastTime,0.05);_lastTime=display.timestamp;
    const double frameTime=280896.0/16777216.0;
    BOOL rendered=NO;
    while(_accumulator>=frameTime) {
        _accumulator-=frameTime;
        IOSRelayTick(_backend,monotonicMs());
        if(!IOSRelayCanAdvanceFrame(_backend)) {
            /* Continue protocol service next display tick. Do not accumulate
             * catch-up frames while the peer is applying backpressure. */
            _accumulator=0;break;
        }
        uint32_t input=_keys|self.player.touchKeys|_pendingKeys|[self controllerKeys];_pendingKeys=0;
        _core->setKeys(_core,input);_core->runFrame(_core);
        BOOL blend=[self.player.settings[@"blend"] boolValue];
        MPDisplayFrame((const uint8_t *)_pixels,_displayPixels,_previousPixels,240*160,blend,_previousValid,[self.player.settings[@"colorMode"] intValue],[self.player.settings[@"saturation"] doubleValue]/100);_previousValid=blend;
        [self drainAudio];++_frames;rendered=YES;
        if(_frames%600==0) [self save];
    }
    if(!rendered) return;
    NSData *pixels=[NSData dataWithBytes:_displayPixels length:sizeof(_displayPixels)];
    CGDataProviderRef provider=CGDataProviderCreateWithCFData((__bridge CFDataRef)pixels);
    CGColorSpaceRef color=CGColorSpaceCreateDeviceRGB();
    CGImageRef cg=CGImageCreate(240,160,8,32,240*4,color,kCGBitmapByteOrder32Big|kCGImageAlphaNoneSkipLast,provider,NULL,false,kCGRenderingIntentDefault);
    UIImage *image=[UIImage imageWithCGImage:cg];self.screen.image=image;
    CGImageRelease(cg);CGColorSpaceRelease(color);CGDataProviderRelease(provider);
    if([NSProcessInfo.processInfo.arguments containsObject:@"--capture-screen"] && (_frames==180 || _frames==600)) {
        [UIImagePNGRepresentation(image) writeToURL:[[self documents] URLByAppendingPathComponent:@"frame.png"] atomically:YES];
        UIGraphicsImageRenderer *renderer=[[UIGraphicsImageRenderer alloc] initWithBounds:self.view.bounds];
        UIImage *ui=[renderer imageWithActions:^(UIGraphicsImageRendererContext *ctx){ [self.view drawViewHierarchyInRect:self.view.bounds afterScreenUpdates:NO]; }];
        [UIImagePNGRepresentation(ui) writeToURL:[[self documents] URLByAppendingPathComponent:@"screen.png"] atomically:YES];
        [self record:[NSString stringWithFormat:@"Rendered %u GBA frames; diagnostic snapshot saved.",_frames]];
    }
}
- (void)togglePause {
    if(!_core) return;
    _paused=!_paused;[self clearInput];_lastTime=0;_fpsTime=0;_fpsFrames=_frames;
    if(_paused) { [self save]; if(self.relay.joined) [self.relay leave];IOSRelayStop(_backend);[self.audio pause]; }
    else { [self.audio startAndReturnError:nil]; }
    [self.pauseButton setTitle:_paused?@"Resume":@"Pause" forState:UIControlStateNormal];
}
- (void)inactive:(NSNotification *)note {
    #if !TARGET_OS_MACCATALYST
    if(_core && !_paused) [self togglePause];
#else
    [self save]; [self clearInput];
#endif
}
- (void)active:(NSNotification *)note { /* User resumes explicitly; never silently resume a stale multiplayer session. */ }
- (void)bound:(NSNotification *)note {
    if(self.gifts)return;
    NSData *metadata=note.userInfo[@"metadata"];
    if(self.captureLogger){
        if(self.captureSession){[self capture:@{@"event":@"session_end",@"trade_success":@"unverified"}];self.captureSession=NO;}
        NSData *rom=[NSData dataWithContentsOfURL:self.romURL],*save=[NSData dataWithContentsOfURL:self.saveURL];uint8_t digest[CC_SHA256_DIGEST_LENGTH];CC_SHA256(rom.bytes,(CC_LONG)rom.length,digest);NSMutableString *hash=[NSMutableString new];for(unsigned i=0;i<sizeof(digest);i++)[hash appendFormat:@"%02x",digest[i]];
        [self capture:@{@"event":@"session_start",@"metadata_b64":[metadata base64EncodedStringWithOptions:0]?:@"",@"rom_sha256":hash,@"save_b64":[save base64EncodedStringWithOptions:0]?:@"",@"save_scope":@"Last persisted save; not an emulator savestate"}];self.captureSession=YES;
    }
    if(!_core || _paused || _adapter==IOS_ADAPTER_OFF || !( [note.userInfo[@"lab"] boolValue]?IOSRelayConfigureLab(_backend,metadata.bytes,metadata.length,[note.userInfo[@"host"] boolValue],monotonicMs()):[note.userInfo[@"nativeHost"] boolValue]?IOSRelayConfigureNativeHost(_backend,metadata.bytes,metadata.length,monotonicMs()):IOSRelayConfigure(_backend,metadata.bytes,metadata.length,monotonicMs()))) {
        [self record:@"Load and resume the Pokémon ROM before joining. The stock Switch room must be FireRed / LeafGreen."];
        [self.relay leave];
    }
}
- (void)hostMembers:(NSNotification *)note {if(self.gifts)return;NSData *m=note.userInfo[@"metadata"];if(_backend && !IOSRelayConfigureNativeHost(_backend,m.bytes,m.length,monotonicMs())){[self record:@"Native host peer metadata rejected; leaving room."];[self.relay leave];}}
- (void)labAdvertisement:(NSNotification *)note {NSData *m=note.userInfo[@"metadata"];if(_backend)IOSRelayUpdateLabAdvertisement(_backend,m.bytes,m.length);}
- (void)lost:(NSNotification *)note { if(self.gifts)return;IOSRelayStop(_backend);if(self.captureSession){[self capture:@{@"event":@"session_end",@"trade_success":@"unverified"}];self.captureSession=NO;[self.captureLogger flushSynchronously];} }
- (void)datagram:(NSNotification *)note {
    if(self.gifts)return;
    if(!_backend || _paused || [note.userInfo[@"slot"] unsignedIntValue]!=0 || [note.userInfo[@"port"] unsignedIntValue]!=12345) return;
    NSData *ip=note.userInfo[@"source"],*data=note.userInfo[@"payload"];
    if(ip.length==4){[self capture:@{@"event":@"datagram",@"direction":@"rx",@"ip_b64":[ip base64EncodedStringWithOptions:0],@"port":@12345,@"payload_b64":[data base64EncodedStringWithOptions:0],@"length":@(data.length)}];IOSRelayReceive(_backend,ip.bytes,data.bytes,data.length);}
}
- (void)pickerForSave:(BOOL)save {
    if(self.relay.joined) { [self record:@"Leave the relay session before importing files."];return; }
    _importingSave=save;
    if(save && !_core) { [self record:@"Load the ROM that this save belongs to first."];return; }
    UIDocumentPickerViewController *picker=[[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[UTTypeData] asCopy:YES];
    picker.delegate=self;picker.allowsMultipleSelection=!save;[self presentViewController:picker animated:YES completion:nil];
}
- (void)importROM { [self pickerForSave:NO]; }
- (void)importSave { [self pickerForSave:YES]; }
- (void)openGame {
    if(self.relay.joined){[self record:@"Leave multiplayer before changing games."];return;}
    UIAlertController *menu=[UIAlertController alertControllerWithTitle:@"Open game" message:nil preferredStyle:UIAlertControllerStyleActionSheet];
    [menu addAction:[UIAlertAction actionWithTitle:@"Import .gba file…" style:UIAlertActionStyleDefault handler:^(UIAlertAction *a){[self importROM];}]];
    NSURL *games=[[self documents] URLByAppendingPathComponent:@"Games"];
    NSDirectoryEnumerator *files=[NSFileManager.defaultManager enumeratorAtURL:games includingPropertiesForKeys:nil options:NSDirectoryEnumerationSkipsHiddenFiles errorHandler:nil];
    NSMutableArray<NSURL *> *roms=[NSMutableArray new];for(NSURL *url in files)if([url.pathExtension.lowercaseString isEqual:@"gba"])[roms addObject:url];
    [roms sortUsingComparator:^NSComparisonResult(NSURL *a,NSURL *b){return [a.lastPathComponent localizedStandardCompare:b.lastPathComponent];}];
    for(NSURL *rom in roms)[menu addAction:[UIAlertAction actionWithTitle:rom.lastPathComponent.stringByDeletingPathExtension style:UIAlertActionStyleDefault handler:^(UIAlertAction *a){[self loadROM:rom];}]];
    [menu addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    menu.popoverPresentationController.sourceView=self.menuButton;menu.popoverPresentationController.sourceRect=self.menuButton.bounds;
    [self presentViewController:menu animated:YES completion:nil];
}
- (void)importROMURL:(NSURL *)url {
    if(self.relay.joined){[self record:@"Leave multiplayer before changing games."];return;}
    NSData *data=[NSData dataWithContentsOfURL:url];
    if(![url.pathExtension.lowercaseString isEqual:@"gba"] || data.length<192 || data.length>32*1024*1024) { [self record:@"Choose an uncompressed .gba ROM (up to 32 MB)."];return; }
    struct mCore *probe=mCoreFind(url.fileSystemRepresentation);BOOL valid=probe && probe->platform(probe)==mPLATFORM_GBA;if(probe)free(probe);
    if(!valid){[self record:@"This file is not a supported GBA ROM."];return;}
    unsigned char hash[CC_SHA256_DIGEST_LENGTH];CC_SHA256(data.bytes,(CC_LONG)data.length,hash);
    NSMutableString *hex=[NSMutableString string];for(unsigned i=0;i<sizeof(hash);++i) [hex appendFormat:@"%02x",hash[i]];
    NSURL *dir=[[[self documents] URLByAppendingPathComponent:@"Games"] URLByAppendingPathComponent:hex];
    NSError *error=nil;[NSFileManager.defaultManager createDirectoryAtURL:dir withIntermediateDirectories:YES attributes:nil error:&error];
    NSURL *dest=[dir URLByAppendingPathComponent:url.lastPathComponent];
    if(![data writeToURL:dest options:NSDataWritingAtomic error:&error]) { [self record:error.localizedDescription];return; }
    [self loadROM:dest];
}
- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    NSURL *url=urls.firstObject;if(!url)return;
    BOOL access=[url startAccessingSecurityScopedResource];
    if(_importingSave) {
        NSData *data=[NSData dataWithContentsOfURL:url];
        if(!MGBASaveSizeIsValid(data.length)) [self record:@"Unsupported save size. Import a raw .sav file for this ROM."];
        else {
            UIAlertController *confirm=[UIAlertController alertControllerWithTitle:@"Replace this game's save?" message:@"The game will restart. A backup of your current save will be kept in the game's folder." preferredStyle:UIAlertControllerStyleAlert];
            [confirm addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
            [confirm addAction:[UIAlertAction actionWithTitle:@"Import save" style:UIAlertActionStyleDefault handler:^(UIAlertAction *a){
                if(self.relay.joined){[self record:@"Leave multiplayer before importing a save."];return;}
                NSURL *rom=self.romURL,*save=self.saveURL;if(!rom || !save || ![self unload])return;
                NSError *error=nil;BOOL imported=MGBAImportSave(data,save,&error);
                [self loadROM:rom];
                if(!imported)[self record:[@"Save import failed: " stringByAppendingString:error.localizedDescription]];
            }]];
            [self presentViewController:confirm animated:YES completion:nil];
        }
    } else {for(NSURL *rom in urls){BOOL scoped=[rom startAccessingSecurityScopedResource];[self importROMURL:rom];if(scoped)[rom stopAccessingSecurityScopedResource];}}
    if(access) [url stopAccessingSecurityScopedResource];
}
- (void)exportSave {
    if(!_core) { [self record:@"Load a ROM first."];return; }
    if(![self save])return;
    NSError *error=nil;NSURL *export=MGBAExportSave(self.saveURL,self.romURL.lastPathComponent,&error);
    if(!export){[self record:error.localizedDescription?:@"No save yet. Save inside the game first."];return;}
    UIDocumentPickerViewController *picker=[[UIDocumentPickerViewController alloc] initForExportingURLs:@[export] asCopy:YES];
    [self presentViewController:picker animated:YES completion:nil];
}
@end

@interface SceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic,strong) UIWindow *window;
@end
@implementation SceneDelegate
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    if(![scene isKindOfClass:UIWindowScene.class])return;
    RelayController *relay=[RelayController new];relay.tabBarItem=[[UITabBarItem alloc] initWithTitle:@"Multiplayer" image:[UIImage systemImageNamed:@"antenna.radiowaves.left.and.right"] tag:1];
    GameController *game=[GameController new];game.relay=relay;game.tabBarItem=[[UITabBarItem alloc] initWithTitle:@"Play" image:[UIImage systemImageNamed:@"gamecontroller"] tag:0];

    self.window=[[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];self.window.rootViewController=game;[self.window makeKeyAndVisible];
}
@end
@interface AppDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation AppDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)options { return YES; }
- (UISceneConfiguration *)application:(UIApplication *)app configurationForConnectingSceneSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    UISceneConfiguration *c=[[UISceneConfiguration alloc] initWithName:@"Game" sessionRole:session.role];c.delegateClass=SceneDelegate.class;return c;
}
@end
int main(int argc,char **argv) { @autoreleasepool { return UIApplicationMain(argc,argv,nil,NSStringFromClass(AppDelegate.class)); } }
