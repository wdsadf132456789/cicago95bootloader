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


void stage78_entry(void) {
    kf(); clr(0);
    txt((COLS-16)/2,0,"Solo Pong (Stage 78)",4);
    int bx=40,by=12,bdx=1,bdy=1;
    int py=12;
    for(int f=0;f<500;f++) {
        for(int i=0;i<80;i++){px(i,1,' ',0);px(i,24,' ',0);}
        px(1,py,0xDB,4);px(1,py+1,0xDB,4);px(1,py+2,0xDB,4);
        bx+=bdx;by+=bdy;
        if(by<=2||by>=23)bdy=-bdy;
        if(bx<=2){bdx=-bdx;if(by>=py-1&&by<=py+3){}else{txt(30,12,"GAME OVER",4);wa();clr(0);txt((COLS-20)/2,12,"Score: 0",7);goto scr;}}
        if(bx>=78)bdx=-bdx;
        px(bx,by,0xDB,4+3);
        if(kh()){uint8_t k=kg();if(!(k&0x80)){if(k==0x48&&py>2)py--;if(k==0x50&&py<21)py++;}}
        dl(14000);
        if(kh()){kg();break;}
    }
    clr(0);
scr:
    txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}
