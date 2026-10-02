import base64,json,tempfile,unittest
from pathlib import Path
from validate_trade_capture import validate
b=lambda x:base64.b64encode(x).decode()
class CaptureTest(unittest.TestCase):
 def rows(self):
  rows=[dict(event='capture_start',schema=1),dict(event='session_start',metadata_b64=b(b'metadata'),save_b64=b(b'save'),rom_sha256='a'*64),dict(event='datagram',direction='tx',accepted=True,ip_b64=b(bytes([192,168,0,1])),port=12345,payload_b64=b(b'payload'),length=7),dict(event='datagram',direction='rx',ip_b64=b(bytes([192,168,0,1])),port=12345,payload_b64=b(b'reply'),length=5),dict(event='session_end')]
  return [dict(x,seq=i,monotonic_ns=1000+i*100) for i,x in enumerate(rows)]
 def runrows(self,rows):
  with tempfile.TemporaryDirectory() as d:
   p=Path(d)/'capture.jsonl';p.write_text('\n'.join(json.dumps(r) for r in rows));return validate(p)
 def test_complete(self):
  r=self.runrows(self.rows());self.assertEqual(r['sessions'][0]['events'][1]['offset_ns'],200)
 def test_truncated(self):
  with self.assertRaises(ValueError):self.runrows(self.rows()[:-1])
 def test_gap(self):
  r=self.rows();r[2]['seq']=4
  with self.assertRaises(ValueError):self.runrows(r)
 def test_clock(self):
  r=self.rows();r[2]['monotonic_ns']=0
  with self.assertRaises(ValueError):self.runrows(r)
 def test_bytes(self):
  r=self.rows();r[2]['length']=999
  with self.assertRaises(ValueError):self.runrows(r)
 def test_bad_base64(self):
  r=self.rows();r[2]['payload_b64']='!'
  with self.assertRaises(ValueError):self.runrows(r)
if __name__=='__main__':unittest.main()
