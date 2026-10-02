#!/usr/bin/env python3
"""Check the Apple addition for private paths, credentials and runtime data."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[4]
SRC = ROOT / 'src/platform/ios-ldn'


def main():
    failures = []
    for path in SRC.rglob('*'):
        if not path.is_file() or '__pycache__' in path.parts:
            continue
        if path.suffix in ('.gba', '.sav', '.jsonl', '.mobileprovision', '.p12', '.key'):
            failures.append(f'{path.relative_to(ROOT)}: private file type')
        text = path.read_text(errors='replace')
        # Match actual user-directory paths, not the patterns in the checker.
        if re.search(r'/(?:Users|home)/[A-Za-z0-9_.-]+/', text):
            failures.append(f'{path.relative_to(ROOT)}: absolute home path')
        if re.search(r'\b[0-9A-Fa-f]{8}-[0-9A-Fa-f]{16}\b', text):
            failures.append(f'{path.relative_to(ROOT)}: device identifier')
        if re.search(r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----', text):
            failures.append(f'{path.relative_to(ROOT)}: private key')
    if failures:
        raise SystemExit('\n'.join(failures))
    print('Apple source privacy checks: PASS')


if __name__ == '__main__':
    main()
