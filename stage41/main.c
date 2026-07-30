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


void stage41_entry(void) {
    kf(); clr(0);
    txt((COLS-20)/2,0,"Brainfuck 2 (Stage 41)",12);
    const char *progs[]={">+++++++++[<++++++++>-]<.>+++++++[<++++>-]<+.+++++++..+++."
                          ">>++++++++[<++++++>-]<.------------.>+++++++++[<-------->-]<+."
                          ">+++++++[<++++>-]<.>++++++++++[<--------->-]<-.>+++++[<+++++>-]<+.",
                          "++++[>++++<-]>[>+++++>+++++<<-]>>.>+>+>+<<<[->[->+>+<<]>>[-<<+>>]<<<]>>>.",
                          "+++++++++[>++++++++<-]>."};
    const char *pnames[]={"\"Hi!\"", "Squares", "ASCII N"};
    int pidx=(41/2)%3;
    const char *prog=progs[pidx];
    const char *pname=pnames[pidx];
    char tape[16];for(int i=0;i<16;i++)tape[i]=0;
    int ptr=0,pc=0,outc=0,wait=0,phase=0;
    char outbuf[32];for(int i=0;i<32;i++)outbuf[i]=0;
    for(int f=0;f<250;f++) {
        clr(0);
        txt((COLS-20)/2,0,"Brainfuck 2 (Stage 41)",12);
        txt(2,2,"BF Program:",12+2);txt(13,2,pname,12+4);
        txt(2,3,prog,7);
        txt(2,5,"[",12+1);
        for(int i=0;i<16;i++) {
            int val=(int)tape[i];
            uint8_t c=(val>=32&&val<127)?(uint8_t)val:'.';
            uint8_t clr=i==ptr?12+6:((val>0)?12+2:7);
            px(3+i,5,c,clr);
        }
        txt(2+16+1,5,"]",12+1);
        txt(2,7,"Output:",12+2);
        for(int i=0;i<outc&&i<30;i++) px(2+i,8,outbuf[i]?outbuf[i]:' ',12+3);
        if(wait==0) {
            char cmd=prog[pc];
            if(cmd=='+') tape[ptr]++;
            else if(cmd=='-') tape[ptr]--;
            else if(cmd=='>'&&ptr<15) ptr++;
            else if(cmd=='<'&&ptr>0) ptr--;
            else if(cmd=='.'&&outc<31) {outbuf[outc]=tape[ptr];outc++;}
            else if(cmd=='['&&tape[ptr]==0) {int d=1;while(d){pc++;if(prog[pc]=='[')d++;if(prog[pc]==']')d--;}}
            else if(cmd==']'&&tape[ptr]!=0) {int d=1;while(d){pc--;if(prog[pc]==']')d++;if(prog[pc]=='[')d--;}}
            pc++;if(!prog[pc]){pc=0;phase++;if(phase>2)break;}
            wait=3;
        } else wait--;
        txt(2,10,"ASCII map:",12+2);
        for(int i=0;i<16;i++) {
            int v=(int)tape[i];
            if(v>0&&v<16) {px(2+i*3,12,0xB0,12+v);}
            else if(v>=16) {px(2+i*3,12,0xDB,12+5);}
        }
        txt(2,14,"ptr={ptr}",12+2);pn(9,14,ptr,12+4);
        txt(2,16,"Commands: + - > < [ ] , .",8);
        dl(20000);
        if(kh()){kg();break;}
    }
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}
