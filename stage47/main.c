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


static uint32_t lr=3655519;
static int lrn(void){lr=lr*1103515245+12345;return(lr>>16)&0x7FFF;}

void stage47_entry(void) {
    kf(); clr(0);
    txt((COLS-20)/2,0,"Game of Life (Stage 47)",13);
    uint8_t g[82*26]={0};
    for(int i=0;i<400;i++)g[(2+(lrn()%21))*82+1+(lrn()%78)]=1;

    for(int gen=0;gen<100;gen++) {
        uint8_t ng[82*26]={0};
        for(int y=2;y<24;y++)for(int x=1;x<80;x++) {
            int n=g[(y-1)*82+(x-1)]+g[(y-1)*82+x]+g[(y-1)*82+(x+1)]
                 +g[y*82+(x-1)]+g[y*82+(x+1)]
                 +g[(y+1)*82+(x-1)]+g[(y+1)*82+x]+g[(y+1)*82+(x+1)];
            if(g[y*82+x])ng[y*82+x]=(n==2||n==3)?1:0;
            else ng[y*82+x]=(n==3)?1:0;
        }
        for(int y=2;y<24;y++)for(int x=1;x<80;x++) {
            g[y*82+x]=ng[y*82+x];
            px(x,y,g[y*82+x]?0xDB:' ',g[y*82+x]?(13):0);
        }
        dl(20000);
        if(kh()){kg();break;}
    }
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}
