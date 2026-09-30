#!/usr/bin/env python3
"""Build the UIKit player for iOS or Mac Catalyst. No ROMs or saves are packaged.
Requires Xcode, CMake, Ninja; iOS signing optionally uses --profile.
"""
from pathlib import Path
import argparse, concurrent.futures, datetime, hashlib, json, plistlib, shlex, shutil, subprocess
p=argparse.ArgumentParser()
p.add_argument('--platform',choices=['ios','mac'],default='ios')
p.add_argument('--profile',type=Path)
p.add_argument('--skip-core',action='store_true')
a=p.parse_args()
root=Path(__file__).resolve().parents[3]; src=root/'src/platform/ios-ldn'
if not a.skip_core: subprocess.run(['sh',str(src/'build-core.sh')],cwd=root,check=True)
commands=json.loads((root/'build/ios-core/compile_commands.json').read_text())
defines=[v for v in shlex.split(commands[0]['command']) if v.startswith('-D')]
is_mac=a.platform=='mac'
sdkname='macosx' if is_mac else 'iphoneos'
sdk=Path(subprocess.check_output(['xcrun','--sdk',sdkname,'--show-sdk-path'],text=True).strip())
target='arm64-apple-ios17.0'+('-macabi' if is_mac else '')
flags=['-target',target,'-isysroot',str(sdk)]
if is_mac:
 support=sdk/'System/iOSSupport'; frameworks=support/'System/Library/Frameworks'
 flags+=['-isystem',str(support/'usr/include'),'-iframework',str(frameworks),'-F',str(frameworks),'-L',str(support/'usr/lib')]
out=root/'build'/('player-mac' if is_mac else 'player-ios');out.mkdir(parents=True,exist_ok=True)
app=out/'mGBA LDN.app'
# Recreate only the generated bundle; avoid stale signatures/profiles in unsigned output.
if app.exists(): shutil.rmtree(app)
contents=app/'Contents' if is_mac else app
binary=contents/'MacOS/mGBALDN' if is_mac else app/'mGBALDN'
binary.parent.mkdir(parents=True,exist_ok=True)
if is_mac: (contents/'Resources').mkdir(exist_ok=True)
archive=root/'build/ios-core/libmgba.a'
if is_mac and (not a.skip_core or not (out/'libmgba-catalyst.a').exists()):
 objects=out/'core-objects';objects.mkdir(exist_ok=True)
 def compile_core(pair):
  index,entry=pair; cmd=shlex.split(entry['command']); clean=[cmd[0]];i=1
  while i<len(cmd):
   v=cmd[i]
   if v in ('-arch','-isysroot','-o'): i+=2;continue
   if v.startswith('-miphoneos-version-min=') or v=='-flto': i+=1;continue
   clean.append(v);i+=1
  obj=objects/f'{index}.o'
  subprocess.run(clean+flags+['-o',str(obj)],cwd=entry['directory'],check=True,stdout=subprocess.DEVNULL)
  return str(obj)
 with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool: objs=list(pool.map(compile_core,enumerate(commands)))
 archive=out/'libmgba-catalyst.a'
 subprocess.run(['xcrun','libtool','-static','-o',str(archive),*objs],check=True)
if is_mac: archive=out/'libmgba-catalyst.a'
info={
 'CFBundleIdentifier':'dev.local.mgba-ldn'+('.mac' if is_mac else ''),
 'CFBundleExecutable':'mGBALDN','CFBundleName':'mGBA LDN','CFBundleDisplayName':'mGBA LDN',
 'CFBundlePackageType':'APPL','CFBundleVersion':'1','CFBundleShortVersionString':'0.1.0',
 'CFBundleInfoDictionaryVersion':'6.0','CFBundleSupportedPlatforms':['MacOSX' if is_mac else 'iPhoneOS'],
 'UIDeviceFamily':[2] if is_mac else [1,2],'UILaunchScreen':{},
 'UISupportedInterfaceOrientations':['UIInterfaceOrientationPortrait','UIInterfaceOrientationLandscapeLeft','UIInterfaceOrientationLandscapeRight'],
 'UIApplicationSupportsIndirectInputEvents':True,'UIFileSharingEnabled':True,'LSSupportsOpeningDocumentsInPlace':True,
 'NSBluetoothAlwaysUsageDescription':'Connect to your modified Switch to play local multiplayer through LDN Relay.',
 'UIApplicationSceneManifest':{'UIApplicationSupportsMultipleScenes':False,'UISceneConfigurations':{'UIWindowSceneSessionRoleApplication':[{'UISceneConfigurationName':'Game','UISceneDelegateClassName':'SceneDelegate'}]}}
}
if is_mac: info['LSMinimumSystemVersion']='14.0'
else: info.update(MinimumOSVersion='17.0',LSRequiresIPhoneOS=True)
(contents/'Info.plist').write_bytes(plistlib.dumps(info))
ldn=root/'src/gba/sio/ldn'
sources=[src/'main.m',src/'RelayController.m',src/'relay-backend.c',src/'apple-crypto.c',src/'relay/relay_codec.c',
 ldn/'ldn-pia.c',ldn/'ldn-pia-connect.c',ldn/'ldn-pia-reliable.c',ldn/'trade-shim.c',root/'src/third-party/zstd/zstdlib.c']
common=flags+defines+['-I'+str(root/'include'),'-I'+str(root/'build/ios-core/include'),'-I'+str(root/'src'),'-I'+str(ldn),'-I'+str(src/'relay'),'-O2','-fwrapv','-Wall','-Wextra','-Wno-unused-parameter','-Wno-deprecated-declarations']
objs=[]
for i,source in enumerate(sources):
 obj=out/f'app-{i}.o';objc=['-fobjc-arc','-fmodules'] if source.suffix=='.m' else ['-std=c11']
 subprocess.run(['xcrun','--sdk',sdkname,'clang',*common,*objc,'-c',str(source),'-o',str(obj)],check=True)
 objs.append(str(obj))
frameworks=['UIKit','Foundation','CoreBluetooth','AVFoundation','UniformTypeIdentifiers','CoreGraphics']
link=['xcrun','--sdk',sdkname,'clang',*flags,*objs,str(archive),'-lz','-lm','-o',str(binary)]
for f in frameworks: link+=['-framework',f]
if is_mac: link+=['-Wl,-rpath,/System/iOSSupport/System/Library/Frameworks']
subprocess.run(link,check=True)
identity='-';entitlements=[]
if a.profile and not is_mac:
 profile=plistlib.loads(subprocess.check_output(['security','cms','-D','-i',str(a.profile)],stderr=subprocess.DEVNULL))
 allowed=profile['Entitlements']
 assert allowed.get('get-task-allow') and allowed['application-identifier'].endswith('*') and profile['ExpirationDate']>datetime.datetime.now(), 'Unexpired wildcard development profile required'
 identities=subprocess.check_output(['security','find-identity','-v','-p','codesigning'],text=True)
 identity=next((hashlib.sha1(c).hexdigest().upper() for c in profile['DeveloperCertificates'] if hashlib.sha1(c).hexdigest().upper() in identities),None)
 assert identity,'No matching local signing certificate'
 ent={'application-identifier':allowed['application-identifier'][:-1]+info['CFBundleIdentifier'], 'com.apple.developer.team-identifier':allowed['com.apple.developer.team-identifier'],'get-task-allow':True}
 entfile=out/'entitlements.plist';entfile.write_bytes(plistlib.dumps(ent));entitlements=['--entitlements',str(entfile)]
 shutil.copy2(a.profile,app/'embedded.mobileprovision')
subprocess.run(['codesign','--force','--sign',identity,*entitlements,str(app)],check=True,capture_output=True)
subprocess.run(['codesign','--verify','--strict',str(app)],check=True)
print('Built:',app)
if not is_mac and not a.profile: print('Ad-hoc signed build only; install requires a development profile.')
