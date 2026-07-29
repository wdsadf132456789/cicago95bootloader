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


static void pn(int x,int y,uint32_t v,uint8_t cl) {
    char b[12];int i=11;b[i]=0;
    if(v==0)b[--i]='0';
    else while(v>0){b[--i]='0'+(v%10);v/=10;}
    for(int j=0;b[i];i++,j++)px(x+j,y,b[i],cl);
}

void stage71_entry(void) {
    kf(); clr(0);
    txt((COLS-24)/2,0,"Collatz Conjecture (Stage 71)",12);
    uint32_t v=507;
    for(int i=0;i<200;i++) {
        pn(5,5+i/18*2,i,12+2);
        px(8,5+i/18*2,':',12+2);
        pn(10,5+i/18*2,v,12);
        if(v%2==0)v/=2;else v=v*3+1;
        if(v==1){txt(30,12,"Reached 1!",12+4);break;}
        dl(400700);
        if(kh()){kg();break;}
    }
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}
