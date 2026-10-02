// AGPL-3.0-or-later. Card content and protocol credited to GB-Link in About and Notices.
#import "GiftController.h"
#import "GiftEngine.h"
#import "RelayController.h"
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <QuartzCore/QuartzCore.h>
#import <TargetConditionals.h>

@interface GiftController () <UIDocumentPickerDelegate, UISearchResultsUpdating>
@property(nonatomic,strong) GiftEngine *engine;
@property(nonatomic,strong) NSArray *groups;
@property(nonatomic,strong) NSArray *filtered;
@property(nonatomic,strong) NSDictionary *selected;
@property(nonatomic,strong) NSDictionary *imported;
@property(nonatomic,strong) UILabel *message;
@property(nonatomic,strong) UIButton *startButton;
@property(nonatomic,strong) NSTimer *timer;
@property(nonatomic,strong) UIAlertController *decision;
@property(nonatomic) BOOL stopping;
@property(nonatomic,copy) NSString *lastResult;
@property(nonatomic) NSUInteger generation;
@end
static uint32_t giftNow(void){return (uint32_t)(CACurrentMediaTime()*1000);}
@implementation GiftController
- (void)viewDidLoad {
    [super viewDidLoad];self.title=@"Wonder Cards";self.navigationItem.largeTitleDisplayMode=UINavigationItemLargeTitleDisplayModeNever;
    self.navigationItem.leftBarButtonItem=[[UIBarButtonItem alloc] initWithTitle:@"Done" style:UIBarButtonItemStyleDone target:self action:@selector(done)];
    self.navigationItem.rightBarButtonItem=[[UIBarButtonItem alloc] initWithTitle:@"Import .wc3" style:UIBarButtonItemStylePlain target:self action:@selector(importCard)];
    NSString *script=[NSString stringWithContentsOfURL:[NSBundle.mainBundle URLForResource:@"gifts" withExtension:@"js"] encoding:NSUTF8StringEncoding error:nil];
    self.engine=[[GiftEngine alloc] initWithScript:script?:@""];self.groups=self.engine.catalogue?:@[];self.filtered=self.groups;
    __weak GiftController *weak=self;
    self.engine.send=^BOOL(NSData *data,NSData *ip){return [weak.relay sendDatagram:data slot:0 address:ip port:12345]!=0;};
    self.engine.canSend=^BOOL{return [weak.relay canSendGameDatagram];};
    self.engine.status=^(NSString *stage,NSDictionary *info){NSUInteger generation=weak.generation;dispatch_async(dispatch_get_main_queue(),^{if(weak.generation==generation)[weak status:stage info:info];});};
    self.relay.nativeAdvertisementProvider=^NSData *{return [weak.engine advertisement];};
    self.relay.giftMode=YES;self.relay.joinOnly=NO;
    self.relay.gameInstructions=@"Connect LDN Relay on your modified Switch, then return to Wonder Cards and tap Start gift. On the receiving Switch: MYSTERY GIFT → WONDER CARDS → FRIEND → GBLINK. Keep this app visible. No ROM is needed in this app.";
    UISearchController *search=[[UISearchController alloc] initWithSearchResultsController:nil];search.searchResultsUpdater=self;search.obscuresBackgroundDuringPresentation=NO;search.searchBar.placeholder=@"Search cards";self.navigationItem.searchController=search;self.definesPresentationContext=YES;
    self.message=[UILabel new];self.message.numberOfLines=0;self.message.font=[UIFont preferredFontForTextStyle:UIFontTextStyleBody];
    self.message.text=self.engine?@"Choose a card, connect your relay, then start.\n\nOn Switch 2: MYSTERY GIFT → WONDER CARDS → FRIEND → GBLINK. Unlock Mystery Gift with LINK TOGETHER WITH ALL on the Poké Mart questionnaire and save first.\n\nCards and gift protocol by GB-Link, with Project Wonder (Goppier) and original contributors. Experimental: local tests only; console delivery needs verification.":@"The Wonder Card catalogue could not be loaded. Reinstall the app.";
    UIButton *connection=[UIButton buttonWithType:UIButtonTypeSystem];[connection setTitle:@"Relay connection" forState:UIControlStateNormal];[connection addTarget:self action:@selector(connection) forControlEvents:UIControlEventTouchUpInside];
    self.startButton=[UIButton buttonWithType:UIButtonTypeSystem];[self.startButton setTitle:@"Start gift" forState:UIControlStateNormal];[self.startButton addTarget:self action:@selector(startStop) forControlEvents:UIControlEventTouchUpInside];self.startButton.enabled=NO;
    UIStackView *stack=[[UIStackView alloc] initWithArrangedSubviews:@[self.message,connection,self.startButton]];stack.axis=UILayoutConstraintAxisVertical;stack.spacing=10;stack.layoutMargins=UIEdgeInsetsMake(16,20,16,20);stack.layoutMarginsRelativeArrangement=YES;self.tableView.tableHeaderView=stack;
    for(NSString *name in @[@"LDNRelayBound",@"LDNHostMembers",@"LDNRelayDatagram",@"LDNRelayLost",@"LDNRelayError"])[NSNotificationCenter.defaultCenter addObserver:self selector:@selector(relayEvent:) name:name object:self.relay];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(background:) name:UIApplicationDidEnterBackgroundNotification object:nil];
}
- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];UIView *h=self.tableView.tableHeaderView;CGFloat w=self.tableView.bounds.size.width;
    CGFloat height=[h systemLayoutSizeFittingSize:CGSizeMake(w,0) withHorizontalFittingPriority:UILayoutPriorityRequired verticalFittingPriority:UILayoutPriorityFittingSizeLevel].height;
    if(h.frame.size.width!=w || fabs(h.frame.size.height-height)>1){h.frame=CGRectMake(0,0,w,height);self.tableView.tableHeaderView=h;}
}
- (void)showMessage:(NSString *)text {self.message.text=text;[self.view setNeedsLayout];}
- (void)connection {self.relay.title=@"Relay connection";self.relay.navigationItem.rightBarButtonItem=nil;[self.navigationController pushViewController:self.relay animated:YES];}
- (NSInteger)numberOfSectionsInTableView:(UITableView *)table {return self.filtered.count;}
- (NSInteger)tableView:(UITableView *)table numberOfRowsInSection:(NSInteger)section {return [self.filtered[section][@"events"] count];}
- (NSString *)tableView:(UITableView *)table titleForHeaderInSection:(NSInteger)section {return self.filtered[section][@"label"];}
- (UITableViewCell *)tableView:(UITableView *)table cellForRowAtIndexPath:(NSIndexPath *)index {
    UITableViewCell *cell=[table dequeueReusableCellWithIdentifier:@"Card"]?:[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:@"Card"];
    NSDictionary *e=self.filtered[index.section][@"events"][index.row];cell.textLabel.text=e[@"label"];cell.textLabel.numberOfLines=0;
    cell.detailTextLabel.text=[e[@"roms"] count]?@"Specific game versions · tap for details":@"FireRed / LeafGreen";
    cell.accessoryType=[self.selected[@"id"] isEqual:e[@"id"]]?UITableViewCellAccessoryCheckmark:UITableViewCellAccessoryNone;return cell;
}
- (void)tableView:(UITableView *)table didSelectRowAtIndexPath:(NSIndexPath *)index {
    [table deselectRowAtIndexPath:index animated:YES];if(self.engine.running){[self showMessage:@"Stop the current gift before choosing another card."];return;}
    NSDictionary *e=self.filtered[index.section][@"events"][index.row];
    NSString *detail=e[@"description"]?:@"";if([e[@"roms"] count])detail=[detail stringByAppendingFormat:@"\n\nSupported versions: %@",[e[@"roms"] componentsJoinedByString:@", "]];
    UIAlertController *a=[UIAlertController alertControllerWithTitle:e[@"label"] message:detail preferredStyle:UIAlertControllerStyleAlert];
    [a addAction:[UIAlertAction actionWithTitle:@"Choose card" style:UIAlertActionStyleDefault handler:^(UIAlertAction *x){self.selected=e;self.startButton.enabled=YES;[self showMessage:[NSString stringWithFormat:@"Selected: %@\nTap Start gift when your relay is connected.",e[@"label"]]];[self.tableView reloadData];}]];
    [a addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];[self presentViewController:a animated:YES completion:nil];
}
- (void)updateSearchResultsForSearchController:(UISearchController *)search {
    NSString *q=search.searchBar.text?:@"";NSMutableArray *groups=[NSMutableArray new];
    NSArray *all=self.imported?[self.groups arrayByAddingObject:@{@"label":@"Imported card",@"events":@[self.imported]}]:self.groups;
    for(NSDictionary *g in all){NSArray *events=[g[@"events"] filteredArrayUsingPredicate:[NSPredicate predicateWithBlock:^BOOL(NSDictionary *e,NSDictionary *b){return !q.length || [e[@"label"] localizedCaseInsensitiveContainsString:q] || [e[@"description"] localizedCaseInsensitiveContainsString:q];}]];if(events.count)[groups addObject:@{@"label":g[@"label"],@"events":events}];}
    self.filtered=groups;[self.tableView reloadData];
}
- (void)startStop {
    if(self.engine.running){[self stopGift];[self showMessage:@"Gift stopped. If the console was receiving, check its result before retrying."];return;}
    ++self.generation;self.stopping=NO;self.lastResult=nil;
    if(![self.engine start:self.selected[@"id"]]){[self showMessage:self.engine.error?:@"Could not start the gift."];return;}
    if(![self.relay beginGiftHosting]){self.stopping=YES;[self.engine stop];[self showMessage:@"Connect and approve the relay first. Disconnect any existing game session, then return here and try Start gift again."];return;}
    [self.startButton setTitle:@"Stop gift" forState:UIControlStateNormal];self.navigationItem.rightBarButtonItem.enabled=NO;
    __weak GiftController *weak=self;
    self.timer=[NSTimer timerWithTimeInterval:1.0/59.7275 repeats:YES block:^(NSTimer *t){[weak.engine tick:giftNow()];}];[NSRunLoop.mainRunLoop addTimer:self.timer forMode:NSRunLoopCommonModes];
    [self showMessage:@"Opening the gift room… Keep this app visible and the screen unlocked."];
}
- (void)stopGift {
    ++self.generation;self.stopping=YES;[self.timer invalidate];self.timer=nil;[self.engine stop];[self.relay cancelGiftHosting];
    [self.decision dismissViewControllerAnimated:NO completion:nil];self.decision=nil;
    [self.startButton setTitle:@"Start gift" forState:UIControlStateNormal];self.navigationItem.rightBarButtonItem.enabled=YES;
}
- (void)status:(NSString *)stage info:(NSDictionary *)info {
    if(self.stopping)return;
    if([stage isEqual:@"decision"]){
        UIAlertController *a=[UIAlertController alertControllerWithTitle:@"Card already received" message:@"The console already has this card. Send it again?" preferredStyle:UIAlertControllerStyleAlert];self.decision=a;
        [a addAction:[UIAlertAction actionWithTitle:@"Keep existing card" style:UIAlertActionStyleCancel handler:^(UIAlertAction *x){[self.engine decide:NO];self.decision=nil;}]];
        [a addAction:[UIAlertAction actionWithTitle:@"Send again" style:UIAlertActionStyleDefault handler:^(UIAlertAction *x){[self.engine decide:YES];self.decision=nil;}]];
        [self.navigationController.topViewController presentViewController:a animated:YES completion:nil];return;
    }
    if([stage isEqual:@"result"]){
        NSDictionary *messages=@{@"sent":@"The console acknowledged the gift. Check that its card is saved, then collect it from the Pokémon Center deliveryman.",@"unsupported":@"This card does not support the receiving game version. Choose a compatible card.",@"cant-accept":@"The receiving game cannot accept this Wonder Card.",@"had-card":@"Kept the card already on the console.",@"kept-card":@"The player kept their existing card. Nothing was replaced.",@"lost":@"The console disconnected before the gift exchange completed."};
        self.lastResult=messages[info[@"outcome"]]?:info[@"message"];[self showMessage:self.lastResult.length?self.lastResult:@"Gift session ended."];return;
    }
    if([stage isEqual:@"closed"] || [stage isEqual:@"error"]){NSString *text=self.lastResult?:info[@"message"]?:@"The gift session ended. Check the result on the console.";[self stopGift];[self showMessage:text];return;}
    NSDictionary *messages=@{@"players":@"Exchanging player details…",@"linked":@"Console connected. Preparing the gift…",@"checking":@"Checking the receiving game…",@"asking":@"Confirm replacing the old card on the receiving console.",@"sending":@"Sending the card and delivery script…",@"closing":@"Finishing the gift. Keep both consoles connected while it saves.",@"open":@"On Switch 2: MYSTERY GIFT → WONDER CARDS → FRIEND → GBLINK."};
    if(messages[stage])[self showMessage:messages[stage]];
}
- (void)relayEvent:(NSNotification *)n {
    if(!self.engine.running || self.stopping)return;
    if([n.name isEqual:@"LDNRelayError"]){[self stopGift];[self showMessage:n.userInfo[@"message"]?:@"Relay command failed. Reconnect and try again."];}
    else if([n.name isEqual:@"LDNRelayLost"]){[self stopGift];[self showMessage:self.lastResult?:@"Relay disconnected. Reconnect it, then start the gift again."];}
    else if([n.name isEqual:@"LDNRelayDatagram"]){if([n.userInfo[@"slot"] unsignedIntValue]==0 && [n.userInfo[@"port"] unsignedIntValue]==12345)[self.engine receive:n.userInfo[@"payload"] from:n.userInfo[@"source"]];}
    else if(![self.engine configure:n.userInfo[@"metadata"] now:giftNow()]){[self stopGift];[self showMessage:@"The relay returned incompatible host metadata. Use LDN Relay 0.5.0 or later."];}
}
- (void)background:(NSNotification *)n {if(self.engine.running){[self stopGift];[self showMessage:@"Gift stopped because the app went into the background. Keep it visible during delivery."];}}
- (void)importCard {
    if(self.engine.running)return;
    UIDocumentPickerViewController *p=[[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[UTTypeData] asCopy:YES];p.delegate=self;[self presentViewController:p animated:YES completion:nil];
}
- (void)documentPicker:(UIDocumentPickerViewController *)picker didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    NSURL *url=urls.firstObject;if(!url || self.engine.running)return;
    if(![url.pathExtension.lowercaseString isEqual:@"wc3"]){[self showMessage:@"Choose a .wc3 Wonder Card file."];return;}
    BOOL access=[url startAccessingSecurityScopedResource];NSNumber *size=nil;[url getResourceValue:&size forKey:NSURLFileSizeKey error:nil];
    NSData *data=size && size.unsignedLongLongValue<=4096?[NSData dataWithContentsOfURL:url]:nil;if(access)[url stopAccessingSecurityScopedResource];
    NSString *error=nil;NSDictionary *card=data?[self.engine importCard:data name:url.lastPathComponent error:&error]:nil;
    if(!card){[self showMessage:error?:@"Could not read this Wonder Card file (maximum 4 KB)."];return;}
    self.imported=card;self.selected=card;self.startButton.enabled=YES;
    self.navigationItem.searchController.searchBar.text=@"";
    [self updateSearchResultsForSearchController:self.navigationItem.searchController];[self showMessage:card[@"description"]];
}
- (void)done {
    if(self.engine.running){UIAlertController *a=[UIAlertController alertControllerWithTitle:@"Stop this gift?" message:@"Leaving closes the gift room. If the console is saving, wait for it to finish." preferredStyle:UIAlertControllerStyleAlert];[a addAction:[UIAlertAction actionWithTitle:@"Keep open" style:UIAlertActionStyleCancel handler:nil]];[a addAction:[UIAlertAction actionWithTitle:@"Stop and close" style:UIAlertActionStyleDestructive handler:^(UIAlertAction *x){[self stopGift];[self done];}]];[self presentViewController:a animated:YES completion:nil];return;}
    [NSNotificationCenter.defaultCenter removeObserver:self];self.relay.giftMode=NO;self.engine.status=nil;[self.engine stop];if(self.finished)self.finished();
}
- (void)dealloc {[self.timer invalidate];[NSNotificationCenter.defaultCenter removeObserver:self];}
@end
