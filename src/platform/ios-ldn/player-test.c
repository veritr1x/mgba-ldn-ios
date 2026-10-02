/* MPL-2.0. Geometry, input and color regression tests for the Apple player. */
#include "PlayerMath.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
    double sizes[][2]={{390,800},{844,390},{1024,768},{768,1024},{912,560},{320,480}};
    unsigned bits[]={0,1,9,8,2,3};
    for(unsigned i=0;i<sizeof(sizes)/sizeof(*sizes);i++){
        MPLayout l=MPComputeLayout(sizes[i][0],sizes[i][1]);
        assert(fabs(l.game.w/l.game.h-1.5)<1e-6);
        assert(l.game.x>=0 && l.game.y>=0);
        assert(l.game.x+l.game.w<=sizes[i][0]+1e-6 && l.game.y+l.game.h<=sizes[i][1]+1e-6);
        for(int b=0;b<6;b++){MPRect r=l.buttons[b];assert(MPKeysAt(l,r.x+r.w/2,r.y+r.h/2)&(1<<bits[b]));}
        double x=l.dpad.x+l.dpad.w/2,y=l.dpad.y+l.dpad.h/2;
        assert((MPKeysAt(l,x,y)&0xf0)==0);
        assert((MPKeysAt(l,x+l.dpad.w*.3,y-l.dpad.h*.3)&0xf0)==((1<<4)|(1<<6)));
        assert((MPKeysAt(l,x-l.dpad.w*.3,y+l.dpad.h*.3)&0xf0)==((1<<5)|(1<<7)));
    }
    assert(MPKeysAt(MPComputeLayout(0,0),0,0)==0);
    uint8_t raw[]={255,100,20,255},previous[4]={1,10,100,255},out[4];
    MPDisplayFrame(raw,out,previous,1,0,0,0,1);assert(!memcmp(raw,out,4));
    uint8_t next[]={0,101,21,255};
    MPDisplayFrame(next,out,previous,1,1,1,0,1);assert(out[0]==127 && out[1]==100 && out[2]==20);assert(!memcmp(next,previous,4));
    MPDisplayFrame(raw,out,previous,1,1,0,0,1);assert(!memcmp(raw,out,4)); // first frame after enabling blending is untouched
    uint8_t gray[]={255,0,0,255};MPColor(gray,3,1);assert(gray[0]==54 && gray[1]==54 && gray[2]==54);
    uint8_t black[]={0,0,0,255},white[]={255,255,255,255};MPColor(black,4,1);MPColor(white,4,1);assert(black[0]==22 && black[1]==51 && black[2]==22);assert(white[0]==156 && white[1]==179 && white[2]==58);
    for(int mode=0;mode<=4;mode++){uint8_t p[]={250,5,128,255};MPColor(p,mode,100);assert(p[3]==255);}
    puts("Player geometry, diagonal input, color and frame blending: PASS");
}
