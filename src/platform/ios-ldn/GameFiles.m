/* Copyright (c) 2026 veritr1x. MPL-2.0. */
#import "GameFiles.h"

BOOL MGBASaveSizeIsValid(NSUInteger size) {
    return size==512 || size==8192 || size==32768 || size==65536 || size==131072;
}
BOOL MGBAImportSave(NSData *data, NSURL *destination, NSError **error) {
    if (!MGBASaveSizeIsValid(data.length)) {
        if(error)*error=[NSError errorWithDomain:@"mGBA" code:1 userInfo:@{NSLocalizedDescriptionKey:@"Choose a raw .sav file for this game (512 bytes to 128 KB)."}];
        return NO;
    }
    if ([NSFileManager.defaultManager fileExistsAtPath:destination.path]) {
        NSData *old=[NSData dataWithContentsOfURL:destination options:0 error:error];
        if(!old)return NO;
        NSURL *backup=[[destination URLByDeletingLastPathComponent] URLByAppendingPathComponent:[NSString stringWithFormat:@"backup-%@.sav",NSUUID.UUID.UUIDString]];
        if(![old writeToURL:backup options:NSDataWritingAtomic error:error])return NO;
    }
    return [data writeToURL:destination options:NSDataWritingAtomic error:error];
}
NSURL *MGBAExportSave(NSURL *save, NSString *gameName, NSError **error) {
    NSData *data=[NSData dataWithContentsOfURL:save options:0 error:error];
    if(!data)return nil;
    NSURL *dir=[[NSURL fileURLWithPath:NSTemporaryDirectory() isDirectory:YES] URLByAppendingPathComponent:NSUUID.UUID.UUIDString isDirectory:YES];
    if(![NSFileManager.defaultManager createDirectoryAtURL:dir withIntermediateDirectories:YES attributes:nil error:error])return nil;
    NSString *name=gameName.lastPathComponent.stringByDeletingPathExtension;
    NSURL *export=[dir URLByAppendingPathComponent:[(name.length?name:@"game") stringByAppendingPathExtension:@"sav"]];
    return [data writeToURL:export options:NSDataWritingAtomic error:error]?export:nil;
}
