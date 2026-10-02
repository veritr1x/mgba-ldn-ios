#!/usr/bin/env python3
"""Test save replacement and export against real temporary files."""
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[4]
src = root / 'src/platform/ios-ldn'
binary = root / 'build/ios-tests/game-files-test'
binary.parent.mkdir(parents=True, exist_ok=True)
subprocess.run(['xcrun', 'clang', '-fobjc-arc', '-g', '-O1',
                '-fsanitize=address,undefined', '-framework', 'Foundation',
                str(src / 'GameFiles.m'), str(src / 'game-files-test.m'),
                '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
