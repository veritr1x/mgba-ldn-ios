// AGPL-3.0-or-later. Independent simulated recipient; no ROM, save or console.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
vm.runInThisContext(fs.readFileSync(process.argv[2], 'utf8'));
const mg = modules['gift/mystery-gift'];
const { EVENTS, eventPayloads } = modules['gift/events'];
const { RfuLeader } = modules['cable/leader'];
const { GiftSession, giftBeacon, linkPlayerBlock } = modules['gift/session'];
const turn = () => new Promise(resolve => setImmediate(resolve));
const u16 = (p,i=0) => p[i] | p[i+1]<<8;
const put16 = (p,i,n) => {p[i]=n;p[i+1]=n>>8;};
const put32 = (p,i,n) => {put16(p,i,n);put16(p,i+2,n>>>16);};
function gameData(code='BPRE',rev=10,card=0) {
  const p=new Uint8Array(100);put32(p,0,0x101);put16(p,4,1);put32(p,8,1);put16(p,12,1);put32(p,16,1);put16(p,20,card);
  [...code].forEach((c,i)=>p[92+i]=c.charCodeAt(0));p[96]=rev;return p;
}
function deliver(server,id,bytes) {for(const b of mg.messageBlocks(id,bytes))server.block(b);}
assert.equal(EVENTS.length,76);assert.equal(new Set(EVENTS.map(e=>e.id)).size,76);
assert.deepEqual(Gifts.catalogue().map(g=>g.events.length),[9,7,60]);
let payloadCount=0;
for(const event of EVENTS)for(const [rom,payloads] of Object.entries(event.payloads))for(const p of payloads){
  assert(p.length>=336 && p.length<=336+995,`${event.id}: invalid size ${p.length}`);
  assert(u16(p)>0,`${event.id}: empty card`);payloadCount++;
}
console.log(`PASS all 76 cards / ${payloadCount} payloads, unique IDs, sizes and offline catalogue`);
// Exercise every selectable payload against the actual async gift server.
for(const event of EVENTS)for(const code of ['BPRE','BPGE']) {
  const game=mg.parseGameData(gameData(code));const payloads=eventPayloads(event,game);
  assert(payloads.length,`${event.id} missing ${code}`);
  for(const payload of payloads){
    let blocks=[];
    const server=new mg.WonderCardServer({link:{sendBlock:(b,id)=>blocks.push({b,id})},payload:()=>payload,confirm:async()=>false});
    const result=server.run();deliver(server,mg.MG_LINK.GAME_DATA,gameData(code));await turn();
    assert.equal(server.receiver.ident,mg.MG_LINK.READY_END);
    for(const [id,expected] of [[mg.MG_LINK.CARD,payload.card],[mg.MG_LINK.RAM_SCRIPT,payload.script]]){
      if(!expected.length)continue;
      const r=new mg.MessageReceiver(id);let received;
      for(const {b} of blocks.filter(b=>b.id===id))received=r.push(b)??received;
      assert.deepEqual(received,expected,`${event.id} ${code} content changed`);
    }
    deliver(server,mg.MG_LINK.READY_END,new Uint8Array(4));assert.equal((await result).outcome,'sent');
  }
}
console.log('PASS every FireRed/LeafGreen payload delivered byte-for-byte through headers, CRC and script exchange');
async function policyCase({data=gameData(),payload=EVENTS[0].payloads.frlg[0],accept=false,toss=0,expected}){
 let sent=[];const s=new mg.WonderCardServer({link:{sendBlock:(b,id)=>sent.push(id)},payload:()=>payload?{card:payload.slice(0,332),script:payload.slice(336)}:null,confirm:async()=>accept});
 const p=s.run();deliver(s,mg.MG_LINK.GAME_DATA,data);await turn();
 if(s.receiver.ident===mg.MG_LINK.RESPONSE){const b=new Uint8Array(4);put32(b,0,toss);deliver(s,mg.MG_LINK.RESPONSE,b);await turn();}
 deliver(s,mg.MG_LINK.READY_END,new Uint8Array(4));assert.equal((await p).outcome,expected);
 if(expected!=='sent')assert(!sent.includes(mg.MG_LINK.CARD));
}
await policyCase({data:gameData('BPRE',10,u16(EVENTS[0].payloads.frlg[0])),expected:'had-card'});
await policyCase({data:gameData('BPRE',10,u16(EVENTS[0].payloads.frlg[0])),accept:true,expected:'sent'});
await policyCase({data:gameData('BPRE',10,65534),toss:1,expected:'kept-card'});
await policyCase({data:gameData('BPRE',10,65534),toss:0,expected:'sent'});
await policyCase({payload:null,expected:'unsupported'});
await policyCase({data:new Uint8Array(100),expected:'cant-accept'});
for(const e of EVENTS.filter(e=>e.roms))assert.equal(eventPayloads(e,mg.parseGameData(gameData('BPRE',1))).length,0);
const r=new mg.MessageReceiver(22),blocks=mg.messageBlocks(22,Uint8Array.of(1,2,3));r.push(blocks[0]);assert.throws(()=>r.push(Uint8Array.of(1,2,4)),/checksum/i);
const {wc3Event}=modules['gift/wc3'];assert.throws(()=>wc3Event(new Uint8Array(0x4e4),'JP.wc3'),/Japanese/);assert.throws(()=>wc3Event(new Uint8Array(12),'bad.wc3'),/bytes/);assert.throws(()=>wc3Event(new Uint8Array(0x58c),'bad.wc3'),/accept/);
const wc3=new Uint8Array(0x58c);wc3.set(EVENTS[0].payloads.frlg[0].slice(0,332),4);wc3[0x1a4]=51;wc3[0x1a8]=2;
assert.equal(wc3Event(wc3,'E - example.wc3').emerald,true);
assert.equal(Gifts.importCard([...wc3],'My card.wc3').id,'wc3-file');
console.log('PASS duplicate/replace decisions, rejection, version restrictions, corrupt CRC, malformed and imported WC3');

// Full gift path, including RFU parent/child link frames and 12-byte block fragmentation.
async function fullSession() {
 let host=[],child=[],tag=0,result=null,decision=0,closed=false;
 const leader=new RfuLeader({send:f=>{const type=(f[4]<<24|f[5]<<16|f[6]<<8|f[7])>>>0;if(type===5)host.push(f.slice(12,12+(f[11]&127)));if(type===4)closed=true;}});
 const session=new GiftSession({leader});session.setEvent(EVENTS[0]);session.onResult=r=>result=r;session.onDecision=()=>decision++;session.start();
 leader.boardFrame({type:1,header:leader.devid,frame:new Uint8Array()});
 function raw(data){const f=new Uint8Array(104);f[8]=data.length;f.set(data,12);child.push(f);}
 function command(words){const p=new Uint8Array(16);p[0]=14;p[1]=16;for(let i=0;i<7;i++)put16(p,2+i*2,words[i]??0);p[2]=(p[2]&31)|((tag++&7)<<5);raw(p);}
 function block(bytes){const count=Math.ceil(bytes.length/12);command([0x8800,count,0x80]);for(let i=0;i<count;i++){const words=[0x8900|i];for(let j=0;j<6;j++)words.push((bytes[i*12+j*2]??0)|((bytes[i*12+j*2+1]??0)<<8));command(words);}}
 function message(id,bytes){for(const b of mg.messageBlocks(id,bytes))block(b);}
 // The NI name negotiation and JOIN_OK acknowledgements use genuine child headers.
 raw(Uint8Array.of(0,12));raw(Uint8Array.of(0,0));
 let joinAck=-1, playerDone=false, reception=null, lastBlock=null, msg=null, savedCard=null,savedScript=null,closingSent=false;
 function receivedBlock(bytes){
  // Link-player exchange precedes the Mystery Gift message headers.
  if(!playerDone){assert.equal(bytes[0],'G'.charCodeAt(0));playerDone=true;command([0x6600,0]);return;}
  if(!msg){msg={id:u16(bytes),crc:u16(bytes,2),size:u16(bytes,4),parts:[]};return;}
  msg.parts.push(...bytes.slice(0,msg.size-msg.parts.length));if(msg.parts.length<msg.size)return;
  const data=Uint8Array.from(msg.parts);assert.equal(mg.crc16(data),msg.crc);const id=msg.id;msg=null;
  if(id===mg.MG_LINK.CLIENT_SCRIPT){
   if(data[0]===mg.CLI.LOAD_GAME_DATA)message(mg.MG_LINK.GAME_DATA,gameData());
  }else if(id===mg.MG_LINK.CARD)savedCard=data;
  else if(id===mg.MG_LINK.RAM_SCRIPT){savedScript=data;message(mg.MG_LINK.READY_END,new Uint8Array(4));}
 }
 for(let tick=0;tick<12000 && !closed;tick++){
  if(child.length)leader.boardFrame({type:6,header:0,frame:child.shift()});
  leader.tick();await turn();
  for(const p of host.splice(0)){
   if(p.length<3)continue;const h=p[0]|p[1]<<8|p[2]<<16,state=(h>>14)&15,n=(h>>11)&3;
   if(leader.state==='answering' && state && joinAck!==leader.joinStep){joinAck=leader.joinStep;const ack=(state<<10)|(n<<7)|(1<<9);raw(Uint8Array.of(ack&255,ack>>8));}
   if(leader.state==='answering' && leader.joinStep===4 && !child.length)command([0]);
   if(state!==4 || p.length!==73)continue;
   const op=u16(p,3),a=u16(p,5);
   if(op===0xa100 && a===0 && !playerDone && !child.length)block(linkPlayerBlock());
   if(op===0x8800){if(!reception || reception.done)reception={count:a,parts:[],done:false};}
   if((op&0xff00)===0x8900 && reception && !reception.done){
    reception.parts[op&31]=p.slice(5,17);
    if(reception.parts.filter(Boolean).length===reception.count){
     reception.done=true;const bytes=Uint8Array.from(reception.parts.flatMap(x=>[...x]));
     // INIT is repeated four times but one logical block is consumed.
     receivedBlock(bytes);lastBlock=bytes;
    }
   }
   if(op===0x5f00 && !closingSent){closingSent=true;command([0x5f00]);}
  }
 }
 assert(closed,'gift did not finish');assert.equal(result?.outcome,'sent');assert.equal(decision,0);
 assert.deepEqual(savedCard,EVENTS[0].payloads.frlg[0].slice(0,332));assert.deepEqual(savedScript,EVENTS[0].payloads.frlg[0].slice(336));
}
await fullSession();
console.log('PASS complete simulated RFU join, player exchange, standby, Wonder Card + script, READY_END and closing grace');
