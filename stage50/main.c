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


void stage50_entry(void) {
    kf(); clr(0);
    txt((COLS-18)/2,0,"Forth Demo (Stage 50)",6);
    int stack[8];int sp=0;
    for(int f=0;f<120;f++) {
        clr(0);
        txt((COLS-18)/2,0,"Forth Demo (Stage 50)",6);
        txt(2,2,"\\ Stack-based, minimal, beautiful",6+2);
        txt(2,4,": square  dup * ;",7);
        txt(2,5,": fib     dup 1 > if 1- dup fib swap 1- fib + then ;",7);
        txt(2,7,"5 3 + 2 * .  \\ prints 16",7);
        if(f%3==0&&sp<8){stack[sp]=f%32;sp++;}
        if(f%5==0&&sp>0)sp--;
        txt(2,9,"Stack:",6+4);
        for(int i=0;i<sp;i++) {
            pn(8+i*5,10,stack[i],6+(i%6)+1);
        }
        txt(2,12,": stars ( n -- ) 0 do 42 emit loop ;",7);
        int ns=f%16;
        txt(2,14,"10 stars => ",7);
        for(int i=0;i<ns;i++)px(14+i,14,0x2A,6+(i%7));
        txt(2,16,": count  10 0 do i . cr loop ;",7);
        txt(2,18,"( The stack is the way )",8);
        dl(60000);
        if(kh()){kg();break;}
    }
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}
