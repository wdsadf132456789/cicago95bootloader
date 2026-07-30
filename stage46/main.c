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


void stage46_entry(void) {
    kf(); clr(0);
    txt((COLS-20)/2,0,"Kotlin Demo (Stage 46)",2);
    for(int f=0;f<120;f++) {
        clr(0);
        txt((COLS-20)/2,0,"Kotlin Demo (Stage 46)",2);
        txt(2,2,"// Null safety + lambdas",2+2);
        txt(2,4,"val numbers = listOf(1, 2, 3, 4, 5)",7);
        txt(2,5,"val doubled = numbers.map { it * 2 }",7);
        txt(2,6,"val even = numbers.filter { it % 2 == 0 }",7);
        for(int i=0;i<5;i++) {
            int v=(i+1)*(f%6+2);
            pn(6+i*5,8,v,2+(i%7));
        }
        txt(2,10,"data class Person(val name: String, val age: Int)",2+4);
        txt(2,12,"val alice = Person(\"Alice\", 25)",7);
        txt(2,13,"val (name, age) = alice  // destructuring",7);
        txt(2,15,"val result: String? = null",2+2);
        txt(2,16,"println(result ?: \"default\")  // elvis op",7);
        txt(2,18,"fun Int.isEven() = this % 2 == 0  // extension",2+3);
        txt(2,20,"println(42.isEven())  // true",7);
        dl(60000);
        if(kh()){kg();break;}
    }
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}
