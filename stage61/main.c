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


void stage61_entry(void) {
    kf(); clr(0);
    txt((COLS-10)/2,0,"R Demo (Stage 61)",2);
    for(int f=0;f<120;f++) {
        clr(0);
        txt((COLS-10)/2,0,"R Demo (Stage 61)",2);
        txt(2,2,"# Statistical computing",2+2);
        txt(2,4,"data <- c(1, 4, 6, 8, 10, 15, 21)",7);
        txt(2,5,"mean(data)",7);
        txt(2,6,"sd(data)",7);
        txt(2,7,"summary(data)",7);
        for(int i=0;i<7;i++) {
            int v=(i+1)*(f%6+1);
            pn(4+i*5,9,v,2+(i%7));
        }
        int m=0;for(int i=0;i<7;i++)m+=(i+1)*(f%6+1);
        m/=7;
        txt(2,11,"mean = ",7);pn(8,11,m,2+4);
        txt(2,13,"lm(y ~ x, data=df)",2+2);
        txt(2,14,"t.test(group1, group2)",7);
        txt(2,16,"library(ggplot2)",7);
        txt(2,17,"ggplot(df, aes(x, y)) + geom_point()",7);
        for(int i=0;i<5;i++) {
            int y=19+i;
            txt(2,y,"|",7);
            for(int d=0;d<(i+1)*(f%3+1);d++)px(3+d,y,0xDB,2+(d%6));
        }
        txt(2,24,"# R: data science since '93",8);
        dl(60000);
        if(kh()){kg();break;}
    }
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}
