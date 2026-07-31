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


void stage100_entry(void) {
    kf(); clr(0);
    txt((COLS-24)/2,0,"Brainfuck Demo (Stage 100)",11);
    const char *progs[]={"+++++[>+++++<-]>+++++.",
                          "+++[>+++++<-]>[>+++++>+++++<<-]>>.",
                          "+++++++++[>++++++++>+++++++++++++>+++++<<<-]>-.>+.>..",
                          ">+>+>+<<<[->[->+>+<<]>>[-<<+>>]<<<]>>>.",
                          "+>+>[->>>+<<<]>>>[-<<<+<<<+>>>>]<<<[->+>+<<]>>[-<<+>>]>>>."};
    const char *pnames[]={"5×5=25", "3×5=15", "ASCII ABC", "Fibonacci", "Addition"};
    int pidx=(100/2)%5;
    const char *prog=progs[pidx];
    const char *pname=pnames[pidx];
    char tape[24];for(int i=0;i<24;i++)tape[i]=0;
    int ptr=12,pc=0,outc=0,wait=0;
    char output[20];for(int i=0;i<20;i++)output[i]=0;
    for(int f=0;f<300;f++) {
        clr(0);
        txt((COLS-24)/2,0,"Brainfuck Demo (Stage 100)",11);
        txt(2,2,"Program:",11+2);txt(10,2,pname,11+4);
        for(int i=0;prog[i];i++) {
            char c[2]={prog[i],0};
            txt(2+i,3,c,i==pc?11+6:7);
        }
        txt(2,5,"Tape:",11+2);
        for(int i=0;i<20;i++) {
            int hi=(i==ptr);
            px(2+i*3,6,hi?'[':' ',hi?11+4:0);
            pn(3+i*3,6,(int)tape[i],hi?11+6:7);
            px(2+i*3+7,6,hi?']':' ',hi?11+4:0);
        }
        txt(2,8,"Output:",11+2);
        for(int i=0;i<outc&&i<15;i++) px(2+i,9,output[i]?output[i]:' ',11+3);
        if(wait==0) {
            char cmd=prog[pc];
            if(cmd=='+') tape[ptr]++;
            else if(cmd=='-') tape[ptr]--;
            else if(cmd=='>'&&ptr<23) ptr++;
            else if(cmd=='<'&&ptr>0) ptr--;
            else if(cmd=='.'&&outc<19) {output[outc]=tape[ptr];outc++;}
            else if(cmd=='['&&tape[ptr]==0) {int d=1;while(d){pc++;if(prog[pc]=='[')d++;if(prog[pc]==']')d--;}}
            else if(cmd==']'&&tape[ptr]!=0) {int d=1;while(d){pc--;if(prog[pc]==']')d++;if(prog[pc]=='[')d--;}}
            pc++;if(!prog[pc])pc=0;
            wait=2;
        } else wait--;
        txt(2,11,"PC:",7);pn(6,11,pc,11+2);
        txt(2,11,"Cell:",7);pn(12,11,(int)tape[ptr],11+4);
        txt(2,13,"Instr:",7);
        if(prog[pc]){char ci[2]={prog[pc],0};px(9,13,ci[0],11+6);}
        txt(2,15,"Cells: 24 | Programs: 5 | Speed: 2fps",8);
        for(int i=0;i<f%40;i++)px(2+i,17,0xDB,11+(i%7));
        if(kh()){kg();break;}
    }
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}
