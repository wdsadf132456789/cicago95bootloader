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


#define NR 5
struct rec {const char *n;const char *c;int a;};
static struct rec tab[NR]={{.n="Alice",.c="NYC",.a=25},{.n="Bob",.c="SF",.a=31},{.n="Carol",.c="LA",.a=22},{.n="Dave",.c="CHI",.a=38},{.n="Eve",.c="SEA",.a=29}};

void stage74_entry(void) {
    kf(); clr(0);
    txt((COLS-18)/2,0,"AWK Demo (Stage 74)",15);
    for(int p=0;p<6;p++) {
        clr(0);
        txt((COLS-18)/2,0,"AWK Demo (Stage 74)",15);
        txt(2,2,"Awk program: ",7);
        const char *progs[]={"{print $0}",              "{print $1, $3}",
                             "/[ou]/",                     "$3 > 30",
                             "{sum+=$3} {print sum}",  "{print $2}"};
        txt(16,2,progs[p],15+2);
        int sum=0;
        for(int i=0;i<NR;i++) {
            int y=6+i*2;
            txt(4,y,"$0:",7);txt(8,y,tab[i].n,7);txt(16,y,tab[i].c,7);pn(26,y,tab[i].a,7);
            int hi=0;
            if(p==0){}/* print all */
            else if(p==1){}/* print $1 $3 */
            else if(p==2){int m=0;for(int j=0;tab[i].n[j];j++)if(tab[i].n[j]=='o'||tab[i].n[j]=='u')m=1;hi=m;}
            else if(p==3){hi=(tab[i].a>30);}
            else if(p==4){sum+=tab[i].a;}
            else if(p==5){}/* print $2 */
            if(hi){txt(8,y,tab[i].n,15+4);txt(16,y,tab[i].c,15+4);pn(26,y,tab[i].a,15+4);}
            if(p==1){txt(4,y,"$1 $3:",7);txt(8,y,tab[i].n,15+2);pn(26,y,tab[i].a,15+2);}
            if(p==5){txt(4,y,"$2:",7);txt(16,y,tab[i].c,15+2);}
        }
        if(p==4){txt(4,16,"Sum age:",7);pn(15,16,sum,15+2);}
        if(p==1){txt(4,18,"(print name + age only)",8);}
        if(p==2){txt(4,18,"(pattern /[ou]/ in name)",8);}
        if(p==3){txt(4,18,"(filter: age > 30)",8);}
        if(p==5){txt(4,18,"(print cities only)",8);}
        dl(800000);
        if(kh()){kg();break;}
    }
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}
