#include <stdint.h>

#define VB ((volatile uint16_t *)0xB8000)
#define COLS 80
#define KBD_DATA 0x60
#define KBD_STAT 0x64

static inline uint8_t inb(uint16_t p) { uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"dN"(p)); return v; }
static int kh(void) { return inb(KBD_STAT)&1; }
static uint8_t kg(void) { return inb(KBD_DATA); }
static void kf(void) { while(kh()) kg(); }
static void wa(void) { while(!kh()); kg(); }

static void px(int x,int y,char c,uint8_t cl) {
    if((uint32_t)x>=COLS||(uint32_t)y>=25)return;
    VB[y*COLS+x]=(uint16_t)cl<<8|(uint8_t)c;
}

static void clr(uint8_t bg) {
    for(int i=0;i<25*COLS;i++) VB[i]=(uint16_t)bg<<8|' ';
}

static void dl(uint32_t n) __attribute__((unused));
static void dl(uint32_t n) { for(volatile uint32_t i=0;i<n;i++) __asm__ volatile("pause"); }

void *memset(void *s,int c,__SIZE_TYPE__ n) { for(__SIZE_TYPE__ i=0;i<n;i++) ((uint8_t *)s)[i]=c; return s; }

static void txt(int x,int y,const char *s,uint8_t cl) {
    for(int i=0;s[i];i++) px(x+i,y,s[i],cl);
}

static void pn(int x,int y,uint32_t v,uint8_t cl) __attribute__((unused));
static void pn(int x,int y,uint32_t v,uint8_t cl) {
    char b[12];int i=11;b[11]=0;
    do{b[--i]='0'+v%10;v/=10;}while(v);
    txt(x,y,b+i,cl);
}


static uint32_t rnd=567870;
static int rn(void){rnd=rnd*1103515245+12345;return(rnd>>16)&0x7FFF;}

void stage46_entry(void) {
    kf(); clr(0);
    txt((COLS-24)/2,0,"Maze Generator (Stage 46)",2);
    uint8_t m[80*24];for(int i=0;i<80*24;i++)m[i]=1;
    int sx=2,sy=2;m[sy*80+sx]=0;
    int dx[]={1,-1,0,0},dy[]={0,0,1,-1};
    for(int c=0;c<500;c++) {
        int d=rn()%4;
        int nx=sx+dx[d]*2,ny=sy+dy[d]*2;
        if(nx>0&&nx<79&&ny>1&&ny<23&&m[ny*80+nx]) {
            m[(sy+dy[d])*80+sx+dx[d]]=0;
            m[ny*80+nx]=0;
            sx=nx;sy=ny;
        } else {
            int ok=0;
            for(int t=0;t<20;t++) {
                d=rn()%4;
                nx=sx+dx[d]*2;ny=sy+dy[d]*2;
                if(nx>0&&nx<79&&ny>1&&ny<23&&m[ny*80+nx]){ok=1;break;}
            }
            if(!ok){sx=2+rn()%38*2;sy=2+rn()%10*2;}
        }
        for(int y=2;y<23;y++)for(int x=0;x<80;x++)
            px(x,y,m[y*80+x]?0xDB:' ',(m[y*80+x]?2:0));
        dl(5000);
        if(kh()){kg();break;}
    }
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}
