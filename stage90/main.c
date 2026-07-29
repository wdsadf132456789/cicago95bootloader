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


void stage90_entry(void) {
    kf(); clr(0);
    txt((COLS-18)/2,0,"Binary Clock (Stage 90)",1);
    for(int f=0;f<200;f++) {
        int b[]={f/3600%24,(f/60)%60,f%60};
        for(int i=0;i<3;i++) {
            for(int y=0;y<6;y++) {
                int bit=(b[i]>>(5-y))&1;
                for(int x=0;x<3;x++)
                    px(10+i*25+x,5+y,bit?0xDB:' ',bit?(1+i*4):0);
            }
        }
        for(int i=0;i<3;i++) {
            txt(10+i*25,12,":",1);
            int v=b[i];
            px(16+i*25,12,'0'+(v/10)%10,1);
            px(19+i*25,12,'0'+v%10,1);
            txt(10+i*25,13,"-----",1);
        }
        txt(10,14,"H",1);txt(35,14,"M",1);txt(60,14,"S",1);
        dl(50000);
        if(kh()){kg();break;}
    }
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}
