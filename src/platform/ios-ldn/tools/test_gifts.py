#!/usr/bin/env python3
"""Offline catalogue/protocol tests and native JavaScriptCore/Pia integration."""
from pathlib import Path
import json, shlex, subprocess
from build_gifts import bundle
root=Path(__file__).resolve().parents[4];src=root/'src/platform/ios-ldn';ldn=root/'src/gba/sio/ldn'
out=root/'build/gift-tests';out.mkdir(parents=True,exist_ok=True)
script=out/'gifts.js';script.write_text(bundle())
subprocess.run(['node',str(src/'gifts/test.mjs'),str(script)],check=True)
commands=json.loads((root/'build/ios-core/compile_commands.json').read_text())
defines=[x for x in shlex.split(commands[0]['command']) if x.startswith('-D') and x!='-DNDEBUG']
flags=['-g','-O1','-fwrapv','-fsanitize=address,undefined',*defines,*['-I'+str(p) for p in [root/'include',root/'build/ios-core/include',root/'src',ldn,src,src/'relay']]]
sources=[src/'gifts/native-test.m',src/'GiftEngine.m',src/'relay-backend.c',src/'pia-host.c',src/'relay/relay_codec.c',src/'apple-crypto.c',ldn/'ldn-pia.c',ldn/'ldn-pia-connect.c',ldn/'ldn-pia-reliable.c',ldn/'trade-shim.c',root/'src/third-party/zstd/zstdlib.c']
objects=[]
for i,p in enumerate(sources):
 obj=out/f'{i}.o';objects.append(str(obj));extra=['-fobjc-arc','-fmodules'] if p.suffix=='.m' else ['-std=c11']
 subprocess.run(['xcrun','clang',*flags,*extra,'-c',str(p),'-o',str(obj)],check=True)
exe=out/'native-test'
subprocess.run(['xcrun','clang','-fsanitize=address,undefined',*objects,'-lz','-framework','Foundation','-framework','JavaScriptCore','-o',str(exe)],check=True)
subprocess.run([str(exe),str(script),str(src/'gifts/fake-recipient.js')],check=True)
