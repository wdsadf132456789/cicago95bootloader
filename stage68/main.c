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


void stage68_entry(void) {
    kf(); clr(0);
    const char *art[]={"\xDB\xDB\xDB\xDB\xDB  \xDB\xDB\xDB\xDB\xDB  \xDB\xDB\xDB\xDB\xDB",
                        "\xDB      \xDB   \xDB  \xDB",
                        "\xDB      \xDB   \xDB  \xDB\xDB\xDB\xDB\xDB",
                        "\xDB      \xDB   \xDB      \xDB",
                        "\xDB\xDB\xDB\xDB\xDB  \xDB\xDB\xDB\xDB\xDB  \xDB\xDB\xDB\xDB\xDB"};
    const char *bootmsg[]={"Booting Chicago-95...",
                            "Loading stage modules...",
                            "Parsing E820 memory map...",
                            "Initializing 55 security modules...",
                            "Mounting BrainFS...",
                            "Handing off to kernel...",
                            "Welcome to Chicago-95"};
    int bx=(COLS-40)/2,by=2,bw=40,bh=14;
    int c0=9;
    for(int f=0;f<160;f++) {
        clr(0);
        uint8_t fc0=c0+(f/10)%2;
        px(bx,by,0xDA,fc0);
        for(int i=1;i<bw-1;i++)px(bx+i,by,0xC4,fc0);
        px(bx+bw-1,by,0xBF,fc0);
        for(int r=1;r<bh-1;r++) {
            uint8_t side=c0+((bh-2-r)%8);
            px(bx,by+r,0xB3,side);
            px(bx+bw-1,by+r,0xB3,side);
        }
        px(bx,by+bh-1,0xC0,fc0);
        for(int i=1;i<bw-1;i++)px(bx+i,by+bh-1,0xC4,fc0);
        px(bx+bw-1,by+bh-1,0xD9,fc0);
        int ax=bx+(bw-18)/2;
        for(int r=0;r<5;r++) {
            uint8_t cl=(c0+r+((f/4)%2))%14+1;
            for(int c=0;art[r][c];c++)px(ax+c,by+1+r,art[r][c],cl);
        }
        txt(ax+9,by+7,"CHICAGO-95 x86_64",c0+2);
        txt(ax+9,by+8,"BrainFS Bootloader",c0+2);
        for(int i=0;i<18;i++)px(ax+i,by+10,0xC4,9+1);
        int pct=(f*100)/160;
        int bar=pct*18/100;
        for(int i=0;i<18;i++)px(ax+i,by+11,i<bar?0xDB:' ',i<bar?c0+((f/4)%6)+1:0);
        pn(ax+18,by+11,pct,c0+4);px(ax+21,by+11,'%',c0+4);
        int mi=(f/20)%7;
        txt(ax,by+13,bootmsg[mi],c0+3);
        txt(ax+20,by+13,"[skip]",8);
        dl(25000);
        if(kh()){kg();break;}
    }
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}
