#!/usr/bin/env python3
"""Exercise notification backpressure with a fake platform and real stream codec."""
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[4]
src = root / 'src/platform/ios-ldn'
out = root / 'build/lab-tests/notification-test'
out.parent.mkdir(parents=True, exist_ok=True)
subprocess.run(['xcrun', 'clang', '-std=c11', '-g', '-O1',
                '-fsanitize=address,undefined', '-I' + str(src / 'relay'),
                str(src / 'notification-test.c'), str(src / 'relay/relay_stream.c'),
                str(src / 'relay/relay_codec.c'), '-o', str(out)], check=True)
subprocess.run([str(out)], check=True)
