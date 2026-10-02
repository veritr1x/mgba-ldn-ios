#!/usr/bin/env python3
"""Exercise the same layout and pixel transform functions used by the UIKit player."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[4]
src=root/'src/platform/ios-ldn'
binary=root/'build/ios-tests/player-test';binary.parent.mkdir(parents=True,exist_ok=True)
subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-O1',str(src/'player-test.c'),'-lm','-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True)
