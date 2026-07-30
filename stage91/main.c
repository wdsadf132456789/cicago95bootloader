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


void stage91_entry(void) {
    kf(); clr(0);
    txt((COLS-24)/2,0,"JavaScript Demo (Stage 91)",2);
    int boxes[6][4];
    for(int i=0;i<6;i++){boxes[i][0]=5+i*12;boxes[i][1]=4+i%3*6;boxes[i][2]=8;boxes[i][3]=4;}
    for(int f=0;f<200;f++) {
        clr(0);
        txt((COLS-24)/2,0,"JavaScript Demo (Stage 91)",2);
        txt(2,2,"// DOM-like animation",2+2);
        for(int i=0;i<6;i++) {
            int idx=(f+i)%6;
            boxes[idx][0]+=(i%3-1);
            boxes[idx][1]+=(i%2?1:-1);
            int bx=boxes[idx][0],by=boxes[idx][1];
            if(bx<1||bx>72)boxes[idx][0]=bx<1?5:69;
            if(by<2||by>21)boxes[idx][1]=by<2?4:20;
            bx=boxes[idx][0];by=boxes[idx][1];
            for(int dy=0;dy<boxes[idx][3];dy++)
                for(int dx=0;dx<boxes[idx][2];dx++)
                    px(bx+dx,by+dy,0xDB,(2+idx)%15+1);
        }
        txt(2,24,"for box of boxes { box.x+=vx; box.y+=vy; }",8);
        dl(30000);
        if(kh()){kg();break;}
    }
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}
