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


void stage62_entry(void) {
    kf(); clr(0);
    txt((COLS-14)/2,0,"Ada Demo (Stage 62)",3);
    for(int f=0;f<120;f++) {
        clr(0);
        txt((COLS-14)/2,0,"Ada Demo (Stage 62)",3);
        txt(2,2,"-- Strongly typed, safe, and readable",3+2);
        txt(2,4,"with Ada.Text_IO; use Ada.Text_IO;",7);
        txt(2,5,"procedure Main is",7);
        txt(2,6,"   type Weekday is (Mon, Tue, Wed, Thu, Fri);",7);
        txt(2,7,"   subtype Workday is Weekday range Mon..Fri;",7);
        txt(2,8,"   Count : Integer := 0;",7);
        txt(2,9,"begin",7);
        txt(2,10,"   for I in 1 .. 10 loop",7);
        txt(2,11,"      Count := Count + I;",7);
        txt(2,12,"   end loop;",7);
        txt(2,13,"   Put_Line(Integer'Image(Count));",7);
        txt(2,14,"end Main;",7);
        int n=0;for(int i=1;i<=f%10+1;i++)n+=i;
        txt(2,16,"Sum 1..",7);pn(9,16,f%10+1,3+2);txt(13+8,16,"=",7);pn(15+8,16,n,3+4);
        txt(2,18,"pragma Assert (Count > 0);",3+4);
        txt(2,20,"-- Ada: used in avionics & railways",8);
        for(int i=0;i<4;i++) {
            int y=22+i;
            txt(2,y,"type Arr is array(1..",7);pn(8,y,i+2,3);txt(2,y+6,") of Integer;",7);
        }
        dl(60000);
        if(kh()){kg();break;}
    }
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}
