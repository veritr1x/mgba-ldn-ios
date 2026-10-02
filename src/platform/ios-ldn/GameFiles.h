/* Copyright (c) 2026 veritr1x. MPL-2.0. */
#import <Foundation/Foundation.h>

BOOL MGBASaveSizeIsValid(NSUInteger size);
// Back up the existing save before an atomic replacement. Fail without replacing
// it if the backup cannot be written. The caller must first flush the emulator.
BOOL MGBAImportSave(NSData *data, NSURL *destination, NSError **error);
NSURL *MGBAExportSave(NSURL *save, NSString *gameName, NSError **error);
