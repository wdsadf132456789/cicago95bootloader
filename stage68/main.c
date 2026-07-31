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
    const char *logo[]={",___________________________,",
                        "|  C H I C A G O - 9 5  x64  |",
                        "|   BrainFS Bare-Metal OS    |",
                        "|  Boot Loader  v0.1.0-beta  |",
                        "'---------------------------'"};
    for(int f=0;f<120;f++) {
        clr(0);
        int cyc=(f/8)%7;
        int base=9+cyc;
        for(int r=0;r<5;r++) {
            for(int c=0;logo[r][c];c++) {
                int a=0;
                if(r==1&&c>=2&&c<=19)a=1;
                if(r==2&&c>=2&&c<=26)a=2;
                if(r==3&&c>=2&&c<=26)a=2;
                px(20+c,4+r,logo[r][c],a?base+r:(base)%7+1);
            }
        }
        int bar=(f%80)*36/80;
        txt(20,10,"[",7);
        for(int i=0;i<36;i++)px(21+i,10,i<bar?0xDB:' ',i<bar?9+2:0);
        txt(57,10,"]",7);
        txt(28,12,"Press any key to skip",8);
        dl(30000);
        if(kh()){kg();break;}
    }
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}
