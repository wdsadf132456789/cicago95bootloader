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


static void bp(uint32_t f,uint32_t ms) {
    uint32_t d=1193182/f;
    __asm__ volatile("outb %%al,%%dx"::"a"((uint8_t)0xB6),"d"((uint16_t)0x43));
    __asm__ volatile("outb %%al,%%dx"::"a"((uint8_t)(d&0xFF)),"d"((uint16_t)0x42));
    __asm__ volatile("outb %%al,%%dx"::"a"((uint8_t)(d>>8)),"d"((uint16_t)0x42));
    uint8_t t; __asm__ volatile("inb %%dx,%0":"=a"(t):"d"((uint16_t)0x61));
    __asm__ volatile("outb %%al,%%dx"::"a"(t|3),"d"((uint16_t)0x61));
    dl(ms*8000);
    __asm__ volatile("inb %%dx,%0":"=a"(t):"d"((uint16_t)0x61));
    __asm__ volatile("outb %%al,%%dx"::"a"(t&0xFC),"d"((uint16_t)0x61));
}

void stage76_entry(void) {
    kf(); clr(0);
    txt((COLS-28)/2,12,"PC Speaker Melody (Stage 76)",2);
    uint16_t nm[]={523,659,784,1047,784,659,523,659,784,523,587,659,698,784,880,988,1047,784,523};
    uint16_t nd[]={100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,200,200,200};
    for(int i=0;i<19;i++) {
        bp(nm[i],nd[i]);
        if(kh()){kg();break;}
    }
    clr(0); txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}
