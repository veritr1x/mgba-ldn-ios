#!/usr/bin/env python3
"""Compile/run encrypted peer tests under address + undefined-behavior sanitizers."""
from pathlib import Path
import json,shlex,subprocess
root=Path(__file__).resolve().parents[4];src=root/'src/platform/ios-ldn';ldn=root/'src/gba/sio/ldn'
commands=json.loads((root/'build/ios-core/compile_commands.json').read_text())
defines=[x for x in shlex.split(commands[0]['command']) if x.startswith('-D') and x!='-DNDEBUG']
out=root/'build/lab-tests';out.mkdir(exist_ok=True)
common=['xcrun','clang','-std=c11','-g','-O1','-fwrapv','-fsanitize=address,undefined',*defines,'-I'+str(root/'include'),'-I'+str(root/'build/ios-core/include'),'-I'+str(root/'src'),'-I'+str(ldn),'-I'+str(src/'relay')]
sources=[src/'lab-test.c',src/'relay-backend.c',src/'pia-host.c',src/'relay/relay_codec.c',src/'apple-crypto.c',ldn/'ldn-pia.c',ldn/'ldn-pia-connect.c',ldn/'ldn-pia-reliable.c',ldn/'trade-shim.c',root/'src/third-party/zstd/zstdlib.c']
subprocess.run(common+list(map(str,sources))+['-lz','-o',str(out/'lab-test')],check=True)
subprocess.run([str(out/'lab-test')],check=True)

# Existing Switch-join regression uses the same backend with laboratory mode off.
sources[0]=src/'protocol-test.c'
subprocess.run(common+list(map(str,sources))+['-lz','-o',str(out/'protocol-test')],check=True)
subprocess.run([str(out/'protocol-test')],check=True)

# Captured post-save phase mismatch, using the same header as the backend.
subprocess.run(common+[str(src/'host-trade-test.c'),'-o',str(out/'host-trade-test')],check=True)
subprocess.run([str(out/'host-trade-test')],check=True)

# Upstream Ruby/Sapphire cable translator using an injected relay-style backend.
subprocess.run(common+[str(src/'wrapper-test.c'),str(root/'src/gba/sio/rfu-wrapper-air.c'),'-o',str(out/'wrapper-test')],check=True)
subprocess.run([str(out/'wrapper-test')],check=True)
