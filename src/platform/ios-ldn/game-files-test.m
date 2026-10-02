/* Copyright (c) 2026 veritr1x. MPL-2.0. */
#import "GameFiles.h"
#include <assert.h>
int main(void) { @autoreleasepool {
    NSFileManager *fm=NSFileManager.defaultManager;
    NSURL *dir=[[NSURL fileURLWithPath:NSTemporaryDirectory()] URLByAppendingPathComponent:NSUUID.UUID.UUIDString];
    assert([fm createDirectoryAtURL:dir withIntermediateDirectories:YES attributes:nil error:nil]);
    NSURL *save=[dir URLByAppendingPathComponent:@"game.sav"];
    NSMutableData *original=[NSMutableData dataWithLength:131072];memset(original.mutableBytes,0x42,original.length);
    NSMutableData *replacement=[NSMutableData dataWithLength:131072];memset(replacement.mutableBytes,0x23,replacement.length);
    assert([original writeToURL:save atomically:YES]);
    NSError *error=nil;
    assert(!MGBAImportSave([NSMutableData dataWithLength:123],save,&error));assert(error);
    assert([[NSData dataWithContentsOfURL:save] isEqual:original]);
    assert(MGBAImportSave(replacement,save,&error));
    assert([[NSData dataWithContentsOfURL:save] isEqual:replacement]);
    NSUInteger backups=0;
    for(NSURL *file in [fm contentsOfDirectoryAtURL:dir includingPropertiesForKeys:nil options:0 error:nil])
        if([file.lastPathComponent hasPrefix:@"backup-"]){assert([[NSData dataWithContentsOfURL:file] isEqual:original]);backups++;}
    assert(backups==1);
    NSURL *export=MGBAExportSave(save,@"Example.gba",&error);
    assert([export.lastPathComponent isEqual:@"Example.sav"]);
    assert([[NSData dataWithContentsOfURL:export] isEqual:replacement]);
    // A missing or unreadable destination must not become a successful export.
    assert(!MGBAExportSave([dir URLByAppendingPathComponent:@"missing.sav"],@"Missing",&error));
    NSURL *blocked=[dir URLByAppendingPathComponent:@"directory.sav"];
    assert([fm createDirectoryAtURL:blocked withIntermediateDirectories:NO attributes:nil error:nil]);
    assert(!MGBAImportSave(replacement,blocked,&error));BOOL isDirectory=NO;
    assert([fm fileExistsAtPath:blocked.path isDirectory:&isDirectory] && isDirectory);
    assert([[NSData dataWithContentsOfURL:save] isEqual:replacement]);
    [fm removeItemAtURL:export.URLByDeletingLastPathComponent error:nil];[fm removeItemAtURL:dir error:nil];
    puts("Save import, backup, export and failure preservation: PASS");
} return 0; }
