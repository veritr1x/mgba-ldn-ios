/* MPL-2.0. Layout and display transforms adapted from android-ldn/GameView.java. */
#ifndef MGBA_PLAYER_MATH_H
#define MGBA_PLAYER_MATH_H
#include <stdint.h>
#include <math.h>
typedef struct { double x,y,w,h; } MPRect;
typedef struct { MPRect game,dpad,buttons[6]; } MPLayout;
static inline MPLayout MPComputeLayout(double w,double h) {
    MPLayout l={0}; if(w<=0 || h<=0)return l;
    double scale=fmin(w/240,h/160),gw=240*scale,gh=160*scale;
    int portrait=h>=w;double unit=fmin(w,h),top=portrait?gh:0,area=portrait?h-gh:h;
    l.game=(MPRect){(w-gw)/2,portrait?0:(h-gh)/2,gw,gh};
    double size=fmin(unit*.5,area*.55),cy=portrait?top+area*.48:h*.62,cx=w*(portrait?.24:.14);
    cx=fmax(size/2+4,fmin(w-size/2-4,cx));
    l.dpad=(MPRect){cx-size/2,cy-size/2,size,size};
    double ab=fmin(unit*.2,area*.24),ax=w*(portrait?.86:.90),bx=w*(portrait?.66:.78);
    l.buttons[0]=(MPRect){ax-ab/2,cy-1.1*ab,ab,ab};
    l.buttons[1]=(MPRect){bx-ab/2,cy+.05*ab,ab,ab};
    double sw=fmin(w*.3,unit*.34),sh=sw*.36,sy=portrait?top+area*.04:h*.08;
    l.buttons[2]=(MPRect){w*.04,sy,sw,sh};l.buttons[3]=(MPRect){w*.96-sw,sy,sw,sh};
    double bw=fmin(w*.2,unit*.24),bh=bw*.32,by=h-bh*1.8;
    l.buttons[4]=(MPRect){w*.5-bw*1.1,by,bw,bh};l.buttons[5]=(MPRect){w*.5+bw*.1,by,bw,bh};
    return l;
}
static inline uint32_t MPKeysAt(MPLayout l,double x,double y) {
    static const unsigned bits[6]={0,1,9,8,2,3};uint32_t keys=0;
    for(int i=0;i<6;i++){MPRect r=l.buttons[i];double e=r.h*.25;if(r.w>0 && x>=r.x-e && x<=r.x+r.w+e && y>=r.y-e && y<=r.y+r.h+e)keys|=1u<<bits[i];}
    double dx=x-l.dpad.x-l.dpad.w/2,dy=y-l.dpad.y-l.dpad.h/2,reach=l.dpad.w*.75,dead=l.dpad.w*.1;
    if(reach>0 && fabs(dx)<=reach && fabs(dy)<=reach){if(dx>dead)keys|=1u<<4;if(dx<-dead)keys|=1u<<5;if(dy>dead)keys|=1u<<7;if(dy<-dead)keys|=1u<<6;}
    return keys;
}
/* Byte-oriented: matches the existing RGBA video buffer on both Apple targets. */
static inline void MPColor(uint8_t *p,int mode,double saturation) {
    double rgb[3]={p[0],p[1],p[2]};
    if(mode==4){double gray=.299*rgb[0]+.587*rgb[1]+.114*rgb[2];const double dark[3]={22,51,22},light[3]={156,179,58};for(int c=0;c<3;c++)p[c]=(uint8_t)lround(dark[c]+gray*(light[c]-dark[c])/255);return;}
    double s=fmax(0,fmin(1,saturation));if(mode==1)s*=.72;else if(mode==2)s*=1.3;else if(mode==3)s=0;
    double gray=.213*rgb[0]+.715*rgb[1]+.072*rgb[2],contrast=mode==1?.92:mode==2?1.08:1,offset=mode==1?10:mode==2?-10:0;
    for(int c=0;c<3;c++)p[c]=(uint8_t)lround(fmax(0,fmin(255,(gray+(rgb[c]-gray)*s)*contrast+offset)));
}
static inline void MPDisplayFrame(const uint8_t *raw,uint8_t *out,uint8_t *previous,unsigned count,int blend,int previousValid,int mode,double saturation){
    for(unsigned i=0;i<count;i++){for(unsigned c=0;c<4;c++){unsigned k=i*4+c;out[k]=blend && previousValid?(raw[k]+previous[k])/2:raw[k];previous[k]=raw[k];}MPColor(out+i*4,mode,saturation);}
}
#endif
