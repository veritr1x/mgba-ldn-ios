/* MPL-2.0. FRLG RFU advertisement -> Switch application advertisement.
 * RFU gname layout: pret/pokefirered include/link_rfu.h RfuGameData.
 * Switch layout/base85: Decryptu/pokeldn pokeldn/frlg/link/beacon.py.
 * Unknown Switch-only record flags remain zero; native interoperability requires hardware testing.
 */
#ifndef IOS_NATIVE_HOST_H
#define IOS_NATIVE_HOST_H
#include "relay/relay_codec.h"
#include <string.h>
#define IOS_NATIVE_AD_SIZE 122
static inline bool IOSNativeAdvertisement(const uint32_t words[6],uint16_t device,bool accepted,uint8_t out[122]){
    if(!words || !out || !device || (words[0]&65535)!=2)return false;
    uint8_t raw[24],rec[24]={0};for(unsigned i=0;i<6;i++)lr_put32(raw+i*4,words[i]);
    const uint8_t *g=raw+2;uint16_t compat=lr_get16(g),trade=lr_get16(g+8);
    memcpy(rec,g+2,2);memcpy(rec+2,raw+16,8);lr_put16(rec+10,device);memcpy(rec+12,g+4,4);
    uint16_t search=(g[10]&127)|(((compat>>10)&7)<<8)|((compat&7)<<11)|((compat&(1<<5))?0x4000:0)|((g[10]&128)?0x8000:0);
    lr_put16(rec+16,search);rec[18]=(trade>>10)<<2;rec[19]=g[11];lr_put16(rec+22,trade&1023);
    memset(out,0,122);out[1]=92;out[2]=22;out[4]=88;out[21]=1;out[22]=accepted?2:1;
    out[26]=6;out[27]=1;memcpy(out+28,"iPhone",6);
    for(unsigned group=0;group<6;group++){uint32_t v=lr_get32(rec+group*4);
        for(unsigned digit=0;digit<5;digit++){uint8_t c=0x23+v%85;v/=85;if(c>=0x5c)c++;out[92+group*5+digit]=c;}}
    return true;
}
#endif
