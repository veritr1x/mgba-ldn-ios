// AGPL-3.0-or-later. Independent test recipient, not included in app bundles.
// Real RFU command framing; emulates only the Mystery Gift client, not a console radio.
globalThis.GiftPeer = (() => {
    const { crc16 } = modules['gift/mystery-gift'];
    const u16 = (p,i=0) => (p[i]??0)|((p[i+1]??0)<<8);
    const put16 = (p,i,n) => {p[i]=n&255;p[i+1]=(n>>8)&255;};
    let queue=[],tag=0,named=false,joined=false,player=false,blockRx=null,messageRx=null,closed=false;
    let card=null,script=null;
    function command(words){const p=new Uint8Array(16);p[0]=14;p[1]=16;for(let i=0;i<7;i++)put16(p,2+i*2,words[i]??0);p[2]=(p[2]&31)|((tag++&7)<<5);queue.push([...p]);}
    function block(bytes){const count=Math.ceil(bytes.length/12);command([0x8800,count,128]);for(let i=0;i<count;i++){const words=[0x8900|i];for(let j=0;j<6;j++)words.push(u16(bytes,i*12+j*2));command(words);}}
    function message(id,bytes){const h=new Uint8Array(6);put16(h,0,id);put16(h,2,crc16(bytes));put16(h,4,bytes.length);block(h);for(let i=0;i<bytes.length;i+=252)block(bytes.slice(i,i+252));}
    function onBlock(bytes){
        if(!player){if(bytes[0]!==71)throw Error('Expected LinkPlayer');player=true;command([0x6600,0]);return;}
        if(!messageRx){messageRx={id:u16(bytes),crc:u16(bytes,2),size:u16(bytes,4),data:[]};return;}
        messageRx.data.push(...bytes.slice(0,messageRx.size-messageRx.data.length));
        if(messageRx.data.length<messageRx.size)return;
        const {id,crc,data}=messageRx;messageRx=null;if(crc16(data)!==crc)throw Error('Gift checksum mismatch');
        if(id===16 && data[0]===8){
            const gd=new Uint8Array(100);put16(gd,0,0x101);gd[4]=gd[8]=gd[12]=gd[16]=1;gd.set([66,80,82,69],92);gd[96]=10;message(17,gd);
        }else if(id===22)card=data;
        else if(id===25){script=data;message(20,new Uint8Array(4));}
    }
    return {
        start(){queue.push([0,12],[0,0]);},
        take(){return queue.shift()??null;},
        receive(p){
            if(p.length<3)return;const h=p[0]|p[1]<<8|p[2]<<16,state=(h>>14)&15,n=(h>>11)&3;
            if(!joined){
                if(state && state!==4){const ack=(state<<10)|(n<<7)|512;queue.push([ack&255,ack>>8]);if(state===3)named=true;}
                else if(!state && named){joined=true;command([0]);}
            }
            if(state!==4 || p.length!==73)return;
            const op=u16(p,3),a=u16(p,5);
            if(op===0xa100 && a===0 && !player && !queue.length){
                const b=new Uint8Array(200);for(let i=0;i<13;i++){b[i]='GameFreak inc.' .charCodeAt(i);b[44+i]=b[i];}put16(b,16,0x4004);put16(b,18,0x8000);b[24]=0xbb;b[25]=255;put16(b,42,2);block(b);
            }
            if(op===0x8800 && (!blockRx || blockRx.done))blockRx={count:a,parts:[],done:false};
            if((op&0xff00)===0x8900 && blockRx && !blockRx.done){
                blockRx.parts[op&31]=p.slice(5,17);
                if(blockRx.parts.filter(Boolean).length===blockRx.count){blockRx.done=true;onBlock(blockRx.parts.flat());}
            }
            if(op===0x5f00 && !closed){closed=true;command([0x5f00]);}
        },
        result(){return {card,script,closed};},
    };
})();
