#!/usr/bin/env python3
"""Package clean Apple release builds; never distribute a private profile."""
from pathlib import Path
import argparse
import hashlib
import plistlib
import shutil
import subprocess
import tempfile
import zipfile
from build_gifts import bundle as gift_bundle

ROOT = Path(__file__).resolve().parents[4]
SRC = ROOT / 'src/platform/ios-ldn'


def validate_bundle(app, platform, version):
    contents = app / 'Contents' if platform == 'mac' else app
    info = plistlib.loads((contents / 'Info.plist').read_bytes())
    if info['CFBundleShortVersionString'] != version:
        raise ValueError('Bundle version does not match VERSION')
    if info.get('LDNRelayCaptureTrade') or info.get('MGBADiagnostics') or info.get('MGBALabHost') or info.get('LDNRelayTransport'):
        raise ValueError('Rebuild the default release without diagnostics or transport overrides')
    expected_id = 'dev.local.mgba-ldn' + ('.mac' if platform == 'mac' else '')
    if info['CFBundleIdentifier'] != expected_id:
        raise ValueError('Public downloads must use the standard bundle identifier')
    resources = contents / 'Resources' if platform == 'mac' else app
    if (resources / 'gifts.js').read_text() != gift_bundle():
        raise ValueError('Wonder Card catalogue is missing or stale; rebuild the app')
    for name in ('GB-Link-AGPL-3.0.txt', 'GB-Link-GPL-3.0.txt', 'Wonder-Cards.md'):
        if not (resources / 'Notices' / name).is_file():
            raise ValueError('Missing Wonder Card notice: ' + name)
    for path in app.rglob('*'):
        if path.is_symlink():
            raise ValueError('Unexpected symlink in app bundle')
        if path.suffix.lower() in ('.mobileprovision', '.provisionprofile', '.p12', '.gba', '.gb', '.gbc', '.sav', '.jsonl', '.log', '.key'):
            raise ValueError(f'Private or game data in bundle: {path.name}')
        if path.is_file():
            data = path.read_bytes()
            if b'/Users/' in data or b'C:\\Users\\' in data or (b'/home/' + b'runner/') in data:
                raise ValueError(f'Local path in bundle: {path.name}')
    binary = contents / 'MacOS/mGBALDN' if platform == 'mac' else app / 'mGBALDN'
    arch = subprocess.check_output(['lipo', '-archs', str(binary)], text=True).strip()
    if arch != 'arm64':
        raise ValueError(f'Unexpected architecture: {arch}')
    subprocess.run(['codesign', '--verify', '--strict', str(app)], check=True)
    signature = subprocess.check_output(['codesign', '-dvv', str(app)], stderr=subprocess.STDOUT, text=True)
    if 'Signature=adhoc' not in signature or 'TeamIdentifier=not set' not in signature:
        raise ValueError('Public downloads must be ad-hoc signed without a developer identity')


def main():
    version = (SRC / 'VERSION').read_text().strip()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'dist')
    dist = parser.parse_args().output.resolve()
    dist.mkdir(parents=True, exist_ok=True)
    if any(dist.iterdir()):
        raise ValueError('Output directory must be empty; use --output for another release')
    artifacts = []
    for platform in ('ios', 'mac'):
        app = ROOT / f'build/player-{platform}-v{version}/mGBA LDN.app'
        validate_bundle(app, platform, version)
        filename = f'mGBA-LDN-{version}-' + ('iOS.ipa' if platform == 'ios' else 'macOS-arm64.zip')
        archive = dist / filename
        with tempfile.TemporaryDirectory() as tmp:
            staging = Path(tmp)
            parent = staging / 'Payload' if platform == 'ios' else staging
            parent.mkdir(exist_ok=True)
            shutil.copytree(app, parent / app.name)
            # iOS tools expect exactly Payload/*.app. Notices are inside the app.
            if platform == 'mac':
                shutil.copy2(SRC / 'README.md', staging / 'README.md')
            subprocess.run(['ditto', '-c', '-k', '--norsrc', str(staging), str(archive)], check=True)
        with zipfile.ZipFile(archive) as z:
            if z.testzip():
                raise ValueError('Corrupt archive')
            required = 'Payload/mGBA LDN.app/Info.plist' if platform == 'ios' else 'mGBA LDN.app/Contents/Info.plist'
            if required not in z.namelist():
                raise ValueError('Invalid archive layout')
        artifacts.append(archive)
    notices = dist / 'SOURCE-AND-LICENSES.txt'
    notices.write_text('Source for these binaries: https://github.com/veritr1x/mgba-ldn-ios-macos/tree/v' + version + '\n\n' +
                       (SRC / 'THIRD_PARTY.md').read_text() + '\n\n' + (ROOT / 'LICENSE').read_text() +
                       '\n\n' + (SRC / 'relay/LICENSE').read_text() + '\n\n' + (SRC / 'gifts/vendor/gblink/LICENSE').read_text() + '\n\n' + (SRC / 'gifts/vendor/gblink/licenses/LDN-GPL-3.0.txt').read_text() + '\n\n' + (ROOT / 'src/third-party/zstd/LICENSE').read_text())
    artifacts.append(notices)
    (dist / 'SHA256SUMS.txt').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.name + '\n' for p in artifacts))
    print('Release files:', dist)


if __name__ == '__main__':
    main()
