#!/usr/bin/env python3
"""Validate app-boundary capture completeness and export an ordered replay timeline."""
import argparse,base64,json
from pathlib import Path

def validate(path):
    rows=[];sessions=[];active=None;last=-1
    def decode(v):return base64.b64decode(v,validate=True)
    for lineno,line in enumerate(Path(path).read_text().splitlines(),1):
        if not line.strip():continue
        r=json.loads(line)
        if r.get('seq')!=len(rows):raise ValueError(f'line {lineno}: sequence gap')
        t=r.get('monotonic_ns')
        if not isinstance(t,int) or t<last:raise ValueError(f'line {lineno}: invalid timestamp')
        last=t;rows.append(r);event=r.get('event')
        if len(rows)==1 and (event!='capture_start' or r.get('schema')!=1):raise ValueError('missing supported capture header')
        if event=='session_start':
            if active is not None:raise ValueError('previous session has no end marker')
            if not decode(r['metadata_b64']):raise ValueError('missing connection metadata')
            decode(r['save_b64'])
            if len(r['rom_sha256'])!=64 or any(c not in '0123456789abcdef' for c in r['rom_sha256']):raise ValueError('invalid ROM hash')
            active=dict(start_ns=t,metadata=r,events=[]);sessions.append(active)
        elif event=='session_end':
            if active is None:raise ValueError('unexpected session end')
            directions={x.get('direction') for x in active['events'] if x['event']=='datagram'}
            if directions!={'rx','tx'}:raise ValueError('session lacks bidirectional traffic')
            active['duration_ns']=t-active['start_ns'];active=None
        elif event=='datagram':
            payload=decode(r['payload_b64']);ip=decode(r['ip_b64'])
            if len(ip)!=4 or r['length']!=len(payload) or not 0<len(payload)<=1400:raise ValueError('invalid datagram length/address')
            if r['direction'] not in ('rx','tx') or not 1<=r['port']<=65535:raise ValueError('invalid endpoint/direction')
            if r['direction']=='tx' and not isinstance(r.get('accepted'),bool):raise ValueError('missing send acceptance status')
        if active is not None and event not in ('session_start','session_end'):
            active['events'].append(dict(r,offset_ns=t-active['start_ns']))
    if not rows or not sessions or active is not None:raise ValueError('capture incomplete: no completed session or missing end marker')
    return {'schema':1,'scope':'App-boundary timeline. Completeness does not prove a successful trade or make ciphertext valid for a new live session.','sessions':sessions,'records':len(rows)}

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('capture',type=Path);p.add_argument('--output',type=Path);a=p.parse_args()
    result=validate(a.capture)
    if a.output:
        with a.output.open('x') as f:json.dump(result,f,indent=2)
    print(f"Validated {result['records']} records in {len(result['sessions'])} complete session(s); trade success remains separately verified.")
