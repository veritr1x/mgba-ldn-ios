#!/usr/bin/env python3
"""Bundle the validated, pinned LDN Relay download with its source and MIT notices."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

def package(source, output):
    info=json.loads((source/'build-info.json').read_text())
    filename=info['file']
    if Path(filename).name!=filename or not filename.endswith('.nro'):
        raise ValueError('Unexpected NRO filename')
    nro=source/filename
    if hashlib.sha256(nro.read_bytes()).hexdigest()!=info['sha256'] or info['source_dirty']:
        raise ValueError('Unverified Switch build')
    output.mkdir(parents=True,exist_ok=True)
    if any(output.iterdir()):
        raise ValueError('Output directory must be empty')
    shutil.copy2(nro,output/filename)
    # ZIP includes all notices and the relay's source manifest/checksums.
    with zipfile.ZipFile(output/f"LDN-Relay-{info['version']}-Switch.zip",'w',zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(source.rglob('*')):
            if path.is_file():archive.write(path,str(Path('LDN-Relay')/path.relative_to(source)))
    (output/'SWITCH-RELAY-SOURCE.txt').write_text(
        f"LDN Relay {info['version']} by {info['author']}\n"
        f"Source: {info['source_url']}\nLicense: MIT (included in the Switch ZIP)\n"
        f"Toolchain: {info['toolchain_image']}\nSHA256 ({filename}): {info['sha256']}\n"
        "Copy the NRO to /switch/ldn-relay/ on the modified Switch SD card.\n"
        "The Switch ZIP contains installation instructions, licenses and build provenance.\n"
        "CI checks the binary and transport tests; it does not perform a console trade.\n")
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path);parser.add_argument('output',type=Path)
    args=parser.parse_args();package(args.source,args.output)
