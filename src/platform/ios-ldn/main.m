/* Copyright (c) 2026 mGBA LDN iOS contributors. MPL-2.0. */
#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <CommonCrypto/CommonDigest.h>
#import "RelayController.h"
#include "relay-backend.h"
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
    struct GBASIORFUBackend *_backend;
    struct AudioStream _stream;
    mColor _pixels[240*160];
    uint32_t _keys;
    BOOL _attached, _paused, _importingSave;
    double _accumulator, _lastTime, _phase, _sumL, _sumR;
    unsigned _samples, _frames;
    float _ring[32768*2];
    _Atomic(uint32_t) _readAudio, _writeAudio;
}
@property(nonatomic,strong) RelayController *relay;
@property(nonatomic,strong) UIImageView *screen;
@property(nonatomic,strong) UILabel *status;
@property(nonatomic,strong) UILabel *titleLabel;
@property(nonatomic,strong) UIButton *pauseButton;
@property(nonatomic,strong) CADisplayLink *display;
@property(nonatomic,strong) AVAudioEngine *audio;
@property(nonatomic,strong) AVAudioSourceNode *source;
@property(nonatomic,strong) NSURL *romURL;
@property(nonatomic,strong) NSURL *saveURL;
@property(nonatomic,strong) NSURL *logURL;
- (void)record:(NSString *)text;
@end
static bool canSendPia(void *ctx) { return [((__bridge GameController *)ctx).relay canSendGameDatagram]; }
static bool sendPia(void *ctx, const uint8_t ip[4], const uint8_t *p, size_t n) {
    GameController *g=(__bridge GameController *)ctx;
    return [g.relay sendDatagram:[NSData dataWithBytes:p length:n] slot:0 address:[NSData dataWithBytes:ip length:4] port:12345]!=0;
}
static void logPia(void *ctx, const char *text) { [(__bridge GameController *)ctx record:[NSString stringWithUTF8String:text]]; }

@implementation GameController
- (NSURL *)documents {
#if TARGET_OS_MACCATALYST
    NSURL *dir=[[NSFileManager.defaultManager URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask].firstObject URLByAppendingPathComponent:@"mGBA LDN"];
    [NSFileManager.defaultManager createDirectoryAtURL:dir withIntermediateDirectories:YES attributes:nil error:nil]; return dir;
#else
    return [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
#endif
}
- (void)record:(NSString *)text {
    if (![text hasPrefix:@"TRACE "]) self.status.text=text;
    NSString *line=[NSString stringWithFormat:@"%@ %@\n",NSDate.date,text];
    if(![NSFileManager.defaultManager fileExistsAtPath:self.logURL.path]) [[NSData data] writeToURL:self.logURL atomically:YES];
    NSFileHandle *f=[NSFileHandle fileHandleForWritingToURL:self.logURL error:nil];
    [f seekToEndOfFile]; [f writeData:[line dataUsingEncoding:NSUTF8StringEncoding]]; [f closeFile];
    if (![text hasPrefix:@"TRACE "]) NSLog(@"mGBA LDN: %@",text);
}
- (UIButton *)actionButton:(NSString *)title selector:(SEL)action {
    UIButton *b=[UIButton buttonWithType:UIButtonTypeSystem];
    b.configuration=[UIButtonConfiguration borderedButtonConfiguration];
    [b setTitle:title forState:UIControlStateNormal];
    [b addTarget:self action:action forControlEvents:UIControlEventTouchUpInside]; return b;
}
- (UIButton *)keyButton:(NSString *)title bit:(unsigned)bit {
    UIButton *b=[UIButton buttonWithType:UIButtonTypeSystem];
    b.tag=bit; b.exclusiveTouch=NO; b.multipleTouchEnabled=YES;
    b.configuration=[UIButtonConfiguration filledButtonConfiguration];
    b.configuration.baseBackgroundColor=[UIColor colorWithRed:.16 green:.2 blue:.29 alpha:1];
    [b setTitle:title forState:UIControlStateNormal]; b.titleLabel.font=[UIFont boldSystemFontOfSize:20];
    [b.heightAnchor constraintGreaterThanOrEqualToConstant:48].active=YES;
    [b addTarget:self action:@selector(keyDown:) forControlEvents:UIControlEventTouchDown|UIControlEventTouchDragEnter];
    [b addTarget:self action:@selector(keyUp:) forControlEvents:UIControlEventTouchUpInside|UIControlEventTouchUpOutside|UIControlEventTouchCancel|UIControlEventTouchDragExit];
    return b;
}
- (UIStackView *)row:(NSArray<UIView *> *)views {
    UIStackView *s=[[UIStackView alloc] initWithArrangedSubviews:views]; s.axis=UILayoutConstraintAxisHorizontal;
    s.spacing=10; s.distribution=UIStackViewDistributionFillEqually; return s;
}
- (void)viewDidLoad {
    [super viewDidLoad]; self.view.backgroundColor=UIColor.systemBackgroundColor;
    self.logURL=[[self documents] URLByAppendingPathComponent:@"game.log"];
    self.titleLabel=[UILabel new]; self.titleLabel.text=@"mGBA · LDN"; self.titleLabel.font=[UIFont boldSystemFontOfSize:24]; self.titleLabel.numberOfLines=2;
    self.status=[UILabel new]; self.status.numberOfLines=0; self.status.font=[UIFont systemFontOfSize:13]; self.status.textColor=UIColor.secondaryLabelColor;
    self.screen=[UIImageView new]; self.screen.backgroundColor=UIColor.blackColor; self.screen.contentMode=UIViewContentModeScaleAspectFit;
    self.screen.layer.magnificationFilter=kCAFilterNearest; self.screen.layer.minificationFilter=kCAFilterNearest;
    self.screen.accessibilityLabel=@"Game screen";
    NSLayoutConstraint *aspect=[self.screen.heightAnchor constraintEqualToAnchor:self.screen.widthAnchor multiplier:2.0/3.0];aspect.priority=750;aspect.active=YES;
    [self.screen.heightAnchor constraintLessThanOrEqualToConstant:280].active=YES;
    self.pauseButton=[self actionButton:@"Pause" selector:@selector(togglePause)];
    UIStackView *top=[self row:@[[self actionButton:@"Load ROM" selector:@selector(importROM)],self.pauseButton]];
    UIStackView *dpad=[[UIStackView alloc] initWithArrangedSubviews:@[
      [self row:@[[UIView new],[self keyButton:@"↑" bit:6],[UIView new]]],
      [self row:@[[self keyButton:@"←" bit:5],[UIView new],[self keyButton:@"→" bit:4]]],
      [self row:@[[UIView new],[self keyButton:@"↓" bit:7],[UIView new]]]]];
    dpad.axis=UILayoutConstraintAxisVertical; dpad.spacing=4;
    UIStackView *ab=[[UIStackView alloc] initWithArrangedSubviews:@[[self keyButton:@"A" bit:0],[self keyButton:@"B" bit:1]]];
    ab.axis=UILayoutConstraintAxisVertical;ab.spacing=10;ab.distribution=UIStackViewDistributionFillEqually;
    UIStackView *controls=[self row:@[dpad,ab]];
    UIStackView *stack=[[UIStackView alloc] initWithArrangedSubviews:@[self.titleLabel,top,self.screen,
      [self row:@[[self keyButton:@"L" bit:9],[self keyButton:@"R" bit:8]]],controls,
      [self row:@[[self keyButton:@"Select" bit:2],[self keyButton:@"Start" bit:3]]],
      [self row:@[[self actionButton:@"Import save" selector:@selector(importSave)],[self actionButton:@"Export save" selector:@selector(exportSave)]]],self.status]];
    stack.axis=UILayoutConstraintAxisVertical; stack.spacing=10;stack.translatesAutoresizingMaskIntoConstraints=NO;
    UIScrollView *scroll=[UIScrollView new];scroll.translatesAutoresizingMaskIntoConstraints=NO;scroll.delaysContentTouches=NO;scroll.canCancelContentTouches=NO;
    [self.view addSubview:scroll]; [scroll addSubview:stack];UILayoutGuide *safe=self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[[scroll.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor],
      [scroll.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor],[scroll.topAnchor constraintEqualToAnchor:safe.topAnchor],
      [scroll.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor],[stack.leadingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.leadingAnchor constant:16],
      [stack.trailingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.trailingAnchor constant:-16],
      [stack.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor constant:12],
      [stack.bottomAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor constant:-16],
      [stack.widthAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.widthAnchor constant:-32]]];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(bound:) name:@"LDNRelayBound" object:self.relay];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(lost:) name:@"LDNRelayLost" object:self.relay];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(datagram:) name:@"LDNRelayDatagram" object:self.relay];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(inactive:) name:UIApplicationWillResignActiveNotification object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(active:) name:UIApplicationDidBecomeActiveNotification object:nil];
    [self startAudio];
    self.display=[CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
    self.display.preferredFrameRateRange=CAFrameRateRangeMake(60,60,60);
    [self.display addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    UIApplication.sharedApplication.idleTimerDisabled=YES;
    [self record:@"Load a GBA ROM. Multiplayer preview: join FireRed/LeafGreen through LDN Relay."];
    NSArray *args=NSProcessInfo.processInfo.arguments;
    NSUInteger romArg=[args indexOfObject:@"--rom"];
    if(romArg!=NSNotFound && romArg+1<args.count) { [self importROMURL:[NSURL fileURLWithPath:args[romArg+1]]];return; }
    NSString *last=[NSUserDefaults.standardUserDefaults stringForKey:@"lastROM"];
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
    for(UIPress *p in presses) { int bit=[self bitForPress:p];if(bit>=0)_keys|=1u<<bit;else [super pressesBegan:[NSSet setWithObject:p] withEvent:event]; }
}
- (void)pressesEnded:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
    for(UIPress *p in presses) { int bit=[self bitForPress:p];if(bit>=0)_keys&=~(1u<<bit);else [super pressesEnded:[NSSet setWithObject:p] withEvent:event]; }
}
- (void)pressesCancelled:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event { _keys=0; }
- (void)keyDown:(UIButton *)b { _keys|=1u<<b.tag; }
- (void)keyUp:(UIButton *)b { _keys&=~(1u<<b.tag); }
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
- (void)save {
    if(!_core || !self.saveURL) return;
    void *bytes=NULL;size_t n=_core->savedataClone(_core,&bytes);
    if(n && bytes) {
        NSError *error=nil;
        [[NSData dataWithBytesNoCopy:bytes length:n freeWhenDone:YES] writeToURL:self.saveURL options:NSDataWritingAtomic error:&error];
        if(error) [self record:[@"Save failed: " stringByAppendingString:error.localizedDescription]];
    } else free(bytes);
}
- (void)unload {
    [self save];
    if(_core) {
        if(_attached) { _core->setPeripheral(_core,mPERIPH_GBA_LINK_PORT,NULL);GBASIORFUDestroy(&_rfu);GBASIORFUBackendDestroy(_backend);_backend=NULL;_attached=NO; }
        _core->unloadROM(_core);mCoreConfigDeinit(&_core->config);_core->deinit(_core);_core=NULL;
    }
}
- (BOOL)loadROM:(NSURL *)url {
    if(self.relay.joined) { [self record:@"Leave the relay session before changing games or saves."];return NO; }
    [self unload];
    _core=mCoreFind(url.fileSystemRepresentation);
    if(!_core || _core->platform(_core)!=mPLATFORM_GBA) { if(_core) free(_core);_core=NULL;[self record:@"This file is not a supported GBA ROM."];return NO; }
    mCoreInitConfig(_core,NULL);
    if(!_core->init(_core)) { mCoreConfigDeinit(&_core->config);free(_core);_core=NULL;return NO; }
    mCoreConfigSetDefaultIntValue(&_core->config,"volume",0x100);
    mCoreConfigSetDefaultIntValue(&_core->config,"mute",0);
    mCoreLoadConfig(_core);
    _core->setVideoBuffer(_core,_pixels,240); _core->setAudioBufferSize(_core,16384);
    memset(&_stream,0,sizeof(_stream));_stream.d.audioRateChanged=audioRate;
    _core->setAVStream(_core,&_stream.d);
    if(!mCoreLoadFile(_core,url.fileSystemRepresentation)) { [self unload];[self record:@"Could not load ROM."];return NO; }
    self.romURL=url;self.saveURL=[[url URLByDeletingLastPathComponent] URLByAppendingPathComponent:@"game.sav"];
    NSData *save=[NSData dataWithContentsOfURL:self.saveURL];
    if(save.length) {
        NSString *name=[NSString stringWithFormat:@"backup-%.0f.sav",NSDate.date.timeIntervalSince1970];
        [save writeToURL:[[url URLByDeletingLastPathComponent] URLByAppendingPathComponent:name] atomically:YES];
    }
    /* Keep the core's save in memory; atomically persist snapshots in our own sandbox. */
    struct VFile *vf=VFileMemChunk(save.bytes,save.length);
    if(!vf || !_core->loadSave(_core,vf)) { if(vf) vf->close(vf);[self unload];return NO; }
    _core->reset(_core);
    _backend=IOSRelayCreate(sendPia,canSendPia,logPia,(__bridge void *)self);
    if(!_backend) { [self unload];return NO; }
    GBASIORFUCreate(&_rfu,_backend);
    NSURL *trace=[[self documents] URLByAppendingPathComponent:@"rfu.log"];
    if([NSProcessInfo.processInfo.arguments containsObject:@"--trace-rfu"]) GBASIORFUSetTraceFile(&_rfu,trace.fileSystemRepresentation);
    _core->setPeripheral(_core,mPERIPH_GBA_LINK_PORT,&_rfu.d);_attached=YES;
    _stream.rate=_core->audioSampleRate(_core);_phase=_sumL=_sumR=0;_samples=0;
    _frames=0;_keys=0;_lastTime=0;_accumulator=0;_paused=NO;
    [self.pauseButton setTitle:@"Pause" forState:UIControlStateNormal];
    NSString *relative=[url.path substringFromIndex:[self documents].path.length+1];
    [NSUserDefaults.standardUserDefaults setObject:relative forKey:@"lastROM"];
    self.titleLabel.text=url.lastPathComponent.stringByDeletingPathExtension;
    [self record:@"ROM loaded · Wireless Adapter attached · use the game's own save menu"];
    return YES;
}
- (void)tick:(CADisplayLink *)display {
    if(!_core || _paused) { _lastTime=0;return; }
    if(!_lastTime) _lastTime=display.timestamp;
    _accumulator+=MIN(display.timestamp-_lastTime,0.05);_lastTime=display.timestamp;
    const double frameTime=280896.0/16777216.0;
    BOOL rendered=NO;
    while(_accumulator>=frameTime) {
        _accumulator-=frameTime;
        IOSRelayTick(_backend,monotonicMs());_core->setKeys(_core,_keys);_core->runFrame(_core);
        [self drainAudio];++_frames;rendered=YES;
        if(_frames%600==0) [self save];
    }
    if(!rendered) return;
    NSData *pixels=[NSData dataWithBytes:_pixels length:sizeof(_pixels)];
    CGDataProviderRef provider=CGDataProviderCreateWithCFData((__bridge CFDataRef)pixels);
    CGColorSpaceRef color=CGColorSpaceCreateDeviceRGB();
    CGImageRef cg=CGImageCreate(240,160,8,32,240*4,color,kCGBitmapByteOrder32Big|kCGImageAlphaNoneSkipLast,provider,NULL,false,kCGRenderingIntentDefault);
    UIImage *image=[UIImage imageWithCGImage:cg];self.screen.image=image;
    CGImageRelease(cg);CGColorSpaceRelease(color);CGDataProviderRelease(provider);
    if(_frames==180 || _frames==600) {
        [UIImagePNGRepresentation(image) writeToURL:[[self documents] URLByAppendingPathComponent:@"frame.png"] atomically:YES];
        UIGraphicsImageRenderer *renderer=[[UIGraphicsImageRenderer alloc] initWithBounds:self.view.bounds];
        UIImage *ui=[renderer imageWithActions:^(UIGraphicsImageRendererContext *ctx){ [self.view drawViewHierarchyInRect:self.view.bounds afterScreenUpdates:NO]; }];
        [UIImagePNGRepresentation(ui) writeToURL:[[self documents] URLByAppendingPathComponent:@"screen.png"] atomically:YES];
        [self record:[NSString stringWithFormat:@"Rendered %u GBA frames; boot snapshot saved. Multiplayer remains unverified.",_frames]];
    }
}
- (void)togglePause {
    if(!_core) return;
    _paused=!_paused;_keys=0;_lastTime=0;
    if(_paused) { [self save]; if(self.relay.joined) [self.relay leave];IOSRelayStop(_backend);[self.audio pause]; }
    else { [self.audio startAndReturnError:nil]; }
    [self.pauseButton setTitle:_paused?@"Resume":@"Pause" forState:UIControlStateNormal];
}
- (void)inactive:(NSNotification *)note {
    #if !TARGET_OS_MACCATALYST
    if(_core && !_paused) [self togglePause];
#else
    [self save]; _keys=0;
#endif
}
- (void)active:(NSNotification *)note { /* User resumes explicitly; never silently resume a stale multiplayer session. */ }
- (void)bound:(NSNotification *)note {
    NSData *metadata=note.userInfo[@"metadata"];
    if(!_core || _paused || !IOSRelayConfigure(_backend,metadata.bytes,metadata.length,monotonicMs())) {
        [self record:@"Load and resume FireRed before joining. Relay metadata must describe an FRLG host."];
        [self.relay leave];
    }
}
- (void)lost:(NSNotification *)note { IOSRelayStop(_backend); }
- (void)datagram:(NSNotification *)note {
    if(!_backend || _paused || [note.userInfo[@"slot"] unsignedIntValue]!=0 || [note.userInfo[@"port"] unsignedIntValue]!=12345) return;
    NSData *ip=note.userInfo[@"source"],*data=note.userInfo[@"payload"];
    if(ip.length==4) IOSRelayReceive(_backend,ip.bytes,data.bytes,data.length);
}
- (void)pickerForSave:(BOOL)save {
    if(self.relay.joined) { [self record:@"Leave the relay session before importing files."];return; }
    _importingSave=save;
    if(save && !_core) { [self record:@"Load the ROM that this save belongs to first."];return; }
    UIDocumentPickerViewController *picker=[[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[UTTypeData] asCopy:YES];
    picker.delegate=self;[self presentViewController:picker animated:YES completion:nil];
}
- (void)importROM { [self pickerForSave:NO]; }
- (void)importSave { [self pickerForSave:YES]; }
- (void)importROMURL:(NSURL *)url {
    NSData *data=[NSData dataWithContentsOfURL:url];
    if(data.length<192 || data.length>32*1024*1024) { [self record:@"Choose an uncompressed GBA ROM (up to 32 MB)."];return; }
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
        if(data.length!=32768 && data.length!=65536 && data.length!=131072 && data.length!=512 && data.length!=8192) [self record:@"Unsupported save size. Import a raw .sav file for this ROM."];
        else {
            NSURL *rom=self.romURL;[self unload];
            NSError *error=nil;
            if([data writeToURL:self.saveURL options:NSDataWritingAtomic error:&error]) [self loadROM:rom];
            else [self record:error.localizedDescription];
        }
    } else [self importROMURL:url];
    if(access) [url stopAccessingSecurityScopedResource];
}
- (void)exportSave {
    if(!_core) { [self record:@"Load a ROM first."];return; }
    [self save];
    UIDocumentPickerViewController *picker=[[UIDocumentPickerViewController alloc] initForExportingURLs:@[self.saveURL] asCopy:YES];
    [self presentViewController:picker animated:YES completion:nil];
}
@end

@interface SceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic,strong) UIWindow *window;
@end
@implementation SceneDelegate
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    if(![scene isKindOfClass:UIWindowScene.class])return;
    RelayController *relay=[RelayController new];relay.tabBarItem=[[UITabBarItem alloc] initWithTitle:@"Switch relay" image:[UIImage systemImageNamed:@"antenna.radiowaves.left.and.right"] tag:1];
    GameController *game=[GameController new];game.relay=relay;game.tabBarItem=[[UITabBarItem alloc] initWithTitle:@"Play" image:[UIImage systemImageNamed:@"gamecontroller"] tag:0];
    UITabBarController *tabs=[UITabBarController new];tabs.viewControllers=@[game,relay];
    self.window=[[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];self.window.rootViewController=tabs;[self.window makeKeyAndVisible];
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
