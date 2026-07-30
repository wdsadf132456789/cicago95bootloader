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


void stage53_entry(void) {
    kf(); clr(0);
    txt((COLS-22)/2,0,"Fortran Demo (Stage 53)",9);
    for(int f=0;f<120;f++) {
        clr(0);
        txt((COLS-22)/2,0,"Fortran Demo (Stage 53)",9);
        txt(2,2,"! Scientific computing since 1957",9+2);
        txt(2,4,"program main",7);
        txt(2,5,"  implicit none",7);
        txt(2,6,"  integer :: i, n = 10",7);
        txt(2,7,"  real :: matrix(3,3)",7);
        txt(2,8,"  matrix = reshape([1,2,3,4,5,6,7,8,9], [3,3])",7);
        for(int r=0;r<3;r++) {
            for(int c=0;c<3;c++) {
                int v=(r+c+1)*(f%4+1);
                pn(6+c*6,10+r,v,9+(r*3+c)%7+1);
            }
        }
        txt(2,14,"  do i = 1, n",9+4);
        txt(2,15,"    print *, 'i = ', i",7);
        txt(2,16,"  end do",9+4);
        int fn=1;for(int i=1;i<=f%8+1;i++)fn*=i;
        txt(2,18,"  ! Factorial:",8);
        pn(14,18,f%8+1,9+2);txt(2,18,"=",7);pn(17,18,fn,9+4);
        txt(2,20,"end program main",7);
        dl(60000);
        if(kh()){kg();break;}
    }
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}
