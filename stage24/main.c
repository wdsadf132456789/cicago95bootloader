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


void stage24_entry(void) {
    kf(); clr(0);
    txt((COLS-18)/2,0,"Fire Effect (Stage 24)",4);
    uint8_t fv[80*24];for(int i=0;i<80*24;i++)fv[i]=0;
    for(int t=0;t<300;t++) {
        for(int x=0;x<80;x++)fv[(23)*80+x]=(t%2)?(34):(0);
        for(int y=2;y<23;y++)for(int x=1;x<79;x++) {
            int v=fv[(y+1)*80+x];
            if(v>(2))v-=(2);
            else v=0;
            if(x>0){int av=fv[(y+1)*80+x-1];if(av>v)v=av;}
            if(x<79){int av=fv[(y+1)*80+x+1];if(av>v)v=av;}
            if(v>0)v-=(1);
            if(v<0)v=0;
            fv[y*80+x]=v;
            uint8_t cc=0;
            if(v>19)cc=4*16+4;
            else if(v>10)cc=4*16+((4+8)&0xF);
            else if(v>5)cc=((4+6)&0xF)*16+((4+6)&0xF);
            else if(v>2)cc=0x80+0x08;
            else cc=0;
            if(cc)px(x,y,0xDB,cc);else px(x,y,' ',0);
        }
        dl(10000);
        if(kh()){kg();break;}
    }
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}
