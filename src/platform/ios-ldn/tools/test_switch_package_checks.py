"""Release packaging must preserve provenance and refuse an unverified NRO."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
import zipfile
from package_switch import package

class SwitchPackageTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.source=self.root/'source';self.source.mkdir();self.out=self.root/'out'
        data=b'validated-test-nro';(self.source/'ldn-relay-v0.5.0.nro').write_bytes(data)
        (self.source/'LICENSE').write_text('MIT license fixture')
        self.info=dict(file='ldn-relay-v0.5.0.nro',sha256=hashlib.sha256(data).hexdigest(),source_dirty=False,version='0.5.0',author='veritrix',source_url='https://github.com/veritr1x/ldn-relay/tree/test',toolchain_image='pinned-test-image')
        self.write_info()
    def write_info(self):(self.source/'build-info.json').write_text(json.dumps(self.info))
    def test_notices_and_source_preserved(self):
        package(self.source,self.out)
        with zipfile.ZipFile(self.out/'LDN-Relay-0.5.0-Switch.zip') as z:
            self.assertEqual(z.read('LDN-Relay/LICENSE'),b'MIT license fixture')
            self.assertEqual(json.loads(z.read('LDN-Relay/build-info.json')),self.info)
        self.assertIn(self.info['source_url'],(self.out/'SWITCH-RELAY-SOURCE.txt').read_text())
    def test_changed_binary_rejected(self):
        (self.source/self.info['file']).write_bytes(b'changed')
        with self.assertRaises(ValueError):package(self.source,self.out)
    def test_dirty_source_rejected(self):
        self.info['source_dirty']=True;self.write_info()
        with self.assertRaises(ValueError):package(self.source,self.out)
    def test_output_never_overwrites(self):
        package(self.source,self.out)
        with self.assertRaises(ValueError):package(self.source,self.out)
    def test_path_escape_rejected(self):
        self.info['file']='../other.nro';self.write_info()
        with self.assertRaises(ValueError):package(self.source,self.out)
if __name__=='__main__':unittest.main()
