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


void stage40_entry(void) {
    kf(); clr(0);
    txt((COLS-24)/2,0,"Brainfuck Demo (Stage 40)",11);
    char tape[16];for(int i=0;i<16;i++)tape[i]=0;
    int ptr=0;
    const char *prog="+++++[>+++++<-]>+++++.";
    for(int f=0;f<200;f++) {
        clr(0);
        txt((COLS-24)/2,0,"Brainfuck Demo (Stage 40)",11);
        txt(2,2,"Program:",11+2);
        txt(2,3,prog,7);
        txt(2,5,"Tape:",11+2);
        for(int i=0;i<16;i++) {
            int hi=(i==ptr);
            txt(3+i*4,6,"[",hi?11+4:7);
            pn(4+i*4,6,(int)tape[i],hi?11+6:7);
            txt(3+i*4+8,6,"]",hi?11+4:7);
        }
        txt(2,8,"Ptr:",7);pn(7,8,ptr,11+2);
        if(f%5==0&&f<160) {
            int step=f/5;
            int pc=step%17;
            if(pc<14&&tape[ptr]<255)tape[ptr]+=(pc<5?1:0);
            if(pc>=5&&pc<10&&ptr<15)ptr++;
            if(pc==10&&ptr>0)ptr--;
        }
        txt(2,10,"Accumulator:",7);pn(14,10,(int)tape[ptr],11+4);
        int n=f%24;
        for(int i=0;i<n;i++)px(30+(i%20),12+(i/20),0xB0,11+i%7+1);
        txt(2,14,"BF commands: + - > < [ ] , .",8);
        dl(40000);
        if(kh()){kg();break;}
    }
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}
