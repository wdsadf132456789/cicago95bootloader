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


static void tset(uint32_t f) {
    uint32_t d=1193182/f;
    __asm__ volatile("outb %%al,%%dx"::"a"((uint8_t)0xB6),"d"((uint16_t)0x43));
    __asm__ volatile("outb %%al,%%dx"::"a"((uint8_t)(d&0xFF)),"d"((uint16_t)0x42));
    __asm__ volatile("outb %%al,%%dx"::"a"((uint8_t)(d>>8)),"d"((uint16_t)0x42));
}
static void ton(uint32_t f) { uint8_t t; tset(f);
    __asm__ volatile("inb %%dx,%0":"=a"(t):"d"((uint16_t)0x61));
    __asm__ volatile("outb %%al,%%dx"::"a"(t|3),"d"((uint16_t)0x61)); }
static void toff(void) { uint8_t t;
    __asm__ volatile("inb %%dx,%0":"=a"(t):"d"((uint16_t)0x61));
    __asm__ volatile("outb %%al,%%dx"::"a"(t&0xFC),"d"((uint16_t)0x61)); }

void stage69_entry(void) {
    kf(); clr(0);
    static const uint16_t nm[3][28]={{659,622,659,622,659,493,587,523,440,261,329,440,493,329,415,493,523},{659,659,698,783,783,698,659,587,523,523,587,659,659,587,659,659,698,783,783,698,659,587,523,523,587,659,587,523},{391,391,440,391,523,493,391,391,440,391,587,523,391,391,783,659,523,493,440,698,698,659,523,587,523}};
    static const uint16_t nd[3][28]={{160,160,160,160,160,160,160,160,320,160,160,160,320,160,160,160,320},{180,180,180,180,180,180,180,180,180,180,180,180,360,360,180,180,180,180,180,180,180,180,180,180,180,180,360,360},{180,180,360,360,360,540,180,180,360,360,360,540,180,180,360,360,360,360,540,180,180,360,360,360,540}};
    static const uint32_t tot[3]={3200,5760,8280};
    static const char *tt[3]={"Fur Elise","Ode to Joy","Happy Birthday"};
    static const char *at[3]={"Ludwig van Beethoven","Ludwig van Beethoven","The Hill Sisters"};
    static const char *stm[]={"Now playing","8-bit mono 22050 Hz","Buffering...","No skips detected","Repeat all on","Volume maxed"};
    int c0=10;
    int song=0,note=0,on=0;
    uint32_t nel=0,sel=0,rng=0xC0FFEE;
    int eq[6]={3,5,2,6,4,1};
    int bx=2,by=2,bw=76,bh=13;
    for(int f=0;f<700;f++) {
        if(nd[song][note]>0&&nel>=nd[song][note]) {
            toff(); on=0; note++; sel=0; nel=0;
            if(note>=28||nd[song][note]==0){note=0;song=(song+1)%3;}
        }
        if(!on&&nd[song][note]>0){ton(nm[song][note]);on=1;}
        clr(0);
        uint8_t fc0=c0+(f/8)%2;
        px(bx,by,0xDA,fc0);
        for(int i=1;i<bw-1;i++)px(bx+i,by,0xC4,fc0);
        px(bx+bw-1,by,0xBF,fc0);
        for(int r=1;r<bh-1;r++){px(bx,by+r,0xB3,c0+((bh-2-r)%8)+1);px(bx+bw-1,by+r,0xB3,c0+((bh-2-r)%8)+1);}
        px(bx,by+bh-1,0xC0,fc0);
        for(int i=1;i<bw-1;i++)px(bx+i,by+bh-1,0xC4,fc0);
        px(bx+bw-1,by+bh-1,0xD9,fc0);
        int ph=(f/10)%2;
        txt(4,3,ph?"\x10 NOW PLAYING":"\x0E NOW PLAYING",c0+3);
        int tx=bx+(bw-20)/2;
        txt(tx,4,tt[song],0x0F);
        txt(tx,5,at[song],0x07);
        for(int a=0;a<5;a++)for(int b=0;b<12;b++){
            int cl=((c0+(b*2+a*3)+(f/6))%14)+1;
            px(4+b,7+a,0xDB,cl);
        }
        txt(20,7,"EQUALIZER",c0+2);
        for(int i=0;i<6;i++){
            rng=rng*1103515245+12345;
            eq[i]+=((rng>>13)%5)-2;
            if(eq[i]<0)eq[i]=0;
            if(eq[i]>8)eq[i]=8;
        }
        for(int i=0;i<6;i++){
            for(int r=0;r<8;r++){
                char ch=(r<eq[i])?0xDB:' ';
                uint8_t cl=(r<eq[i])?(c0+((r*f/2)%5)+1):0;
                px(20+i*3,10-r,ch,cl);
            }
        }
        for(int i=0;i<bw-8;i++)px(bx+4+i,11,i%2?0xDB:0xB2,c0+((f/5)%6)+1);
        uint32_t pct=(sel*100)/(tot[song]?tot[song]:1);
        int bar=(int)(pct*60/100);
        for(int i=0;i<60;i++)px(4+i,13,i<bar?0xDB:' ',i<bar?c0+((f/4)%6)+1:0);
        int el=sel/1000,to=tot[song]/1000;
        pn(4,14,el/60,c0+2);px(6,14,':',c0+2);pn(7,14,el%60,c0+2);
        pn(64,14,to/60,c0+2);px(66,14,':',c0+2);pn(67,14,to%60,c0+2);
        txt(4,12,"VOL",c0+4);
        int vq=10+((f/4)%4);
        for(int i=0;i<12;i++)px(8+i,12,i<vq?0xDB:' ',i<vq?c0+((f/4)%6)+1:0);
        txt(26,12,ph?"<<  ||  >>":"<<  |   >>",c0+2);
        pn(66,12,song+1,c0+3);txt(68,12,"/3",c0+3);
        int mi=(f/40)%6;
        txt(bx+(bw-16)/2,15,stm[mi],c0+1);
        dl(40*8000);
        nel+=40;sel+=40;
        if(kh()){kg();toff();break;}
    }
    toff();
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}
