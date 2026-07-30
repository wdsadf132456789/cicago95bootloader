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


void stage66_entry(void) {
    kf(); clr(0);
    txt((COLS-24)/2,0,"Arch Linux Demo (Stage 66)",7);
    for(int f=0;f<150;f++) {
        clr(0);
        txt((COLS-24)/2,0,"Arch Linux Demo (Stage 66)",7);
        px(35,2,0x03,7+6);
        px(36,2,'r',7+6);
        px(37,2,'c',7+6);
        px(38,2,'h',7+6);
        txt(2,4,"$ sudo pacman -Syu",7+2);
        txt(2,5,":: Synchronizing package databases...",7);
        txt(2,6," core is up to date",7);
        txt(2,7," extra is up to date",7);
        txt(2,8," community is up to date",7);
        txt(2,9,":: Starting full system upgrade...",7);
        txt(2,10,":: Replace linux with linux-lts? [Y/n]",7);
        int n=f%20;
        for(int i=0;i<n;i++) {
            if(i<5) {txt(2,12+i,"[✓] package-",7);pn(5,12+i,i,7);}
        }
        if(f<100) {
            txt(2,14,"  downloading packages...",7+2);
            for(int i=0;i<f/5;i++)px(4+i,15,0xDB,7+2);
        } else {
            txt(2,14,"$ sudo pacman -S base-devel",7+2);
            txt(2,15,"  resolving dependencies...",7);
            txt(2,16,"  looking for conflicting packages...",7);
        }
        txt(2,18,"$ yay -S opencode-git",7+4);
        txt(2,19,"  :: Proceed with installation? [Y/n]",8);
        txt(2,21,"$ neofetch",7+2);
        txt(2,22,"  OS: Arch Linux x86_64",7+f%7+1);
        txt(2,23,"  Kernel: 6.14.1-arch1-1",7+f%5+1);
        dl(50000);
        if(kh()){kg();break;}
    }
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}
