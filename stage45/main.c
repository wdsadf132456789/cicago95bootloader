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


void stage45_entry(void) {
    kf(); clr(0);
    txt((COLS-24)/2,0,"TypeScript Demo (Stage 45)",1);
    for(int f=0;f<120;f++) {
        clr(0);
        txt((COLS-24)/2,0,"TypeScript Demo (Stage 45)",1);
        txt(2,2,"interface Person {",1+2);
        txt(2,3,"  readonly name: string;",7);
        txt(2,4,"  age: number;",7);
        txt(2,5,"  city?: string;",7);
        txt(2,6,"}",1+2);
        txt(2,8,"const alice: Person = {",7);
        txt(2,9,"  name: 'Alice',",7);
        txt(2,10,"  age: 25,",7);
        txt(2,11,"  city: 'NYC'",7);
        txt(2,12,"};",7);
        for(int i=0;i<4;i++) {
            int y=14+i;
            txt(2,y,"type Result<T> = T | null;",1+i+1);
        }
        txt(2,19,"function identity<T>(arg: T): T {",1+4);
        txt(2,20,"  return arg;",7);
        txt(2,21,"}",1+4);
        txt(2,23,"const num = identity<number>(42);",8);
        dl(60000);
        if(kh()){kg();break;}
    }
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}
