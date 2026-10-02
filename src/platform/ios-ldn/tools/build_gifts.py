#!/usr/bin/env python3
"""Bundle the pinned, offline GB-Link modules for JavaScriptCore; no npm/network."""
from pathlib import Path
import hashlib
import json
import posixpath
import re

SRC = Path(__file__).resolve().parents[1]
VENDOR = SRC / 'gifts/vendor/gblink'

def bundle():
    manifest = json.loads((VENDOR / 'upstream.json').read_text())
    for name, digest in manifest['files'].items():
        if hashlib.sha256((VENDOR / name).read_bytes()).hexdigest() != digest:
            raise ValueError(f'GB-Link source changed without updating provenance: {name}')
    modules = ['gift/payloads', 'gift/mystery-gift', 'gift/official', 'gift/team',
               'gift/events', 'gift/wc3', 'cable/leader', 'gift/session']
    out = ['// GB-Link derived bundle. See Notices and gifts/README.md.\nconst modules = {};']
    for name in modules:
        text = (VENDOR / ('web/js/' + name + '.js')).read_text()
        exports = re.findall(r'^export (?:const|class|function) (\w+)', text, re.M)
        def imported(match):
            target = posixpath.normpath(posixpath.join(posixpath.dirname(name), match[2]))
            return 'const {' + match[1] + '} = modules[' + json.dumps(target.removesuffix('.js')) + '];'
        text = re.sub(r"import \{([^}]+)\} from '([^']+)';", imported, text)
        text = re.sub(r'^export ', '', text, flags=re.M)
        out.append('modules[' + json.dumps(name) + '] = (() => {\n' + text +
                   '\nreturn {' + ','.join(exports) + '};\n})();')
    out.append((SRC / 'gifts/apple.js').read_text())
    return '\n'.join(out) + '\n'

if __name__ == '__main__':
    import argparse
    p = argparse.ArgumentParser(); p.add_argument('output', type=Path); args = p.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(bundle())
