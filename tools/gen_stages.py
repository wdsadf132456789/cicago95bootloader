#!/usr/bin/env python3
"""Generate stages 9-100 with unique behavior for each."""

import os, math

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(PROJECT, "build")

HEADER = '''#include <stdint.h>

#define VB ((volatile uint16_t *)0xB8000)
#define COLS 80
#define KBD_DATA 0x60
#define KBD_STAT 0x64

static inline uint8_t inb(uint16_t p) {{ uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"dN"(p)); return v; }}
static int kh(void) {{ return inb(KBD_STAT)&1; }}
static uint8_t kg(void) {{ return inb(KBD_DATA); }}
static void kf(void) {{ while(kh()) kg(); }}
static void wa(void) {{ while(!kh()); kg(); }}

static void px(int x,int y,char c,uint8_t cl) {{
    if((uint32_t)x>=COLS||(uint32_t)y>=25)return;
    VB[y*COLS+x]=(uint16_t)cl<<8|(uint8_t)c;
}}

static void clr(uint8_t bg) {{
    for(int i=0;i<25*COLS;i++) VB[i]=(uint16_t)bg<<8|' ';
}}

static void dl(uint32_t n) __attribute__((unused));
static void dl(uint32_t n) {{ for(volatile uint32_t i=0;i<n;i++) __asm__ volatile("pause"); }}

void *memset(void *s,int c,__SIZE_TYPE__ n) {{ for(__SIZE_TYPE__ i=0;i<n;i++) ((uint8_t *)s)[i]=c; return s; }}

static void txt(int x,int y,const char *s,uint8_t cl) {{
    for(int i=0;s[i];i++) px(x+i,y,s[i],cl);
}}

static void pn(int x,int y,uint32_t v,uint8_t cl) __attribute__((unused));
static void pn(int x,int y,uint32_t v,uint8_t cl) {{
    char b[12];int i=11;b[11]=0;
    do{{b[--i]='0'+v%10;v/=10;}}while(v);
    txt(x,y,b+i,cl);
}}

'''

# --- Template implementations ---

TEMPLATES = []

def t_bounce(n):
    chars = 'O@*#%&$!~.'
    c = chars[n % len(chars)]
    col = 0x01 + (n % 14)
    spd = 10 + (n % 20)
    msg = f"Bouncing Ball Demo (Stage {n})"
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-30)/2,0,"{msg}",{col});
    int x=1,y=1,dx=1,dy=1;
    for(int i=0;i<500;i++) {{
        px(x,y,' ',0);
        x+=dx;y+=dy;
        if(x<=0||x>=COLS-1)dx=-dx;
        if(y<=0||y>=23)dy=-dy;
        px(x,y,'{c}',{col});
        if(kh()){{kg();break;}}
        dl({spd}000);
    }}
    clr(0); txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_bounce)

def t_colors(n):
    msg = f"Color Test Pattern (Stage {n})"
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-30)/2,0,"{msg}",0x0F);
    for(int y=0;y<20;y++)
        for(int x=0;x<80;x++)
            px(x,y+2,0xDB,(x/5)+(y*4)%16);
    txt((COLS-20)/2,23,"Press any key...",8);
    wa();
}}
'''

TEMPLATES.append(t_colors)

def t_count(n):
    msg = f"Counting Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-25)/2,0,"{msg}",{col});
    for(int i=1;i<=999;i++) {{
        int v=i;
        for(int x=0;x<9;x++) px(36+x,12,' ',7);
        int p=44;
        if(v>=100){{px(p-3,12,'0'+v/100,{col});v%=100;}}
        if(i>=10){{px(p-2,12,'0'+v/10,{col});v%=10;}}
        px(p-1,12,'0'+v,{col});
        dl({2000+(n%5)}000);
        if(kh()){{kg();break;}}
    }}
    clr(0); txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_count)

def t_scroll(n):
    texts = [
        "Welcome to Chicago-95 BrainFS!",
        "64-bit Long Mode Bare Metal OS",
        "Stage " + str(n) + " - Scrolling Text Demo",
        "The quick brown fox jumps over the lazy dog",
        "Hello from Stage " + str(n) + "!",
        "This is a scrolling text demo...",
        "Chicago-95: Where the past meets the future",
        "Booting the revolution, one sector at a time",
        "ASCII art is the true art form",
        "Stage " + str(n) + " reporting for duty!"
    ]
    t = texts[n % len(texts)]
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    for(int i=0;i<80+35;i++) {{
        for(int x=0;x<80;x++) px(x,12,' ',7);
        for(int j=0;"{t}"[j]&&i+j<80;j++)
            px(i+j,12,"{t}"[j],{col});
        dl({3000+(n%10)}00);
        if(kh()){{kg();break;}}
    }}
    clr(0); txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_scroll)

def t_beep(n):
    notes = [
        (523,100),(659,100),(784,100),(1047,100),
        (784,100),(659,100),(523,100),(659,100),
        (784,100),(523,100),(587,100),(659,100),
        (698,100),(784,100),(880,100),(988,100),
        (1047,200),(784,200),(523,200)
    ]
    note_code = "bp(" + ",".join(f"{f},{d}" for f,d in notes) + ");"
    col = 1 + (n % 15)
    msg = f"PC Speaker Melody (Stage {n})"
    return HEADER.format() + f'''
static void bp(uint32_t f,uint32_t ms) {{
    uint32_t d=1193182/f;
    __asm__ volatile("outb %%al,%%dx"::"a"((uint8_t)0xB6),"d"((uint16_t)0x43));
    __asm__ volatile("outb %%al,%%dx"::"a"((uint8_t)(d&0xFF)),"d"((uint16_t)0x42));
    __asm__ volatile("outb %%al,%%dx"::"a"((uint8_t)(d>>8)),"d"((uint16_t)0x42));
    uint8_t t; __asm__ volatile("inb %%dx,%0":"=a"(t):"d"((uint16_t)0x61));
    __asm__ volatile("outb %%al,%%dx"::"a"(t|3),"d"((uint16_t)0x61));
    dl(ms*8000);
    __asm__ volatile("inb %%dx,%0":"=a"(t):"d"((uint16_t)0x61));
    __asm__ volatile("outb %%al,%%dx"::"a"(t&0xFC),"d"((uint16_t)0x61));
}}

void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-28)/2,12,"{msg}",{col});
    uint16_t nm[]={{{','.join(str(f) for f,d in notes)}}};
    uint16_t nd[]={{{','.join(str(d) for f,d in notes)}}};
    for(int i=0;i<{len(notes)};i++) {{
        bp(nm[i],nd[i]);
        if(kh()){{kg();break;}}
    }}
    clr(0); txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_beep)

def t_stars(n):
    col = 1 + (n % 15)
    count = 50 + (n * 3) % 100
    msg = f"Star Field (Stage {n})"
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-20)/2,0,"{msg}",{col});
    uint32_t r={n*12345+6789};
    for(int f=0;f<200;f++) {{
        for(int i=0;i<5;i++) {{
            r=r*1103515245+12345;
            int x=(r>>16)%80,y=((r>>8)%22)+1;
            px(x,y,0xDB,0x08);
        }}
        {{{{if(kh()){{kg();break;}}}}}}
        dl({2000+(n%10)}00);
        for(int i=0;i<3;i++) {{
            r=r*1103515245+12345;
            int x=(r>>16)%80,y=((r>>8)%22)+1;
            px(x,y,' ',0);
        }}
    }}
    clr(0); txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_stars)

def t_border(n):
    chars = ['*', '#', '@', '&', '%', '$', '!', '~', '+', '=']
    c = chars[n % len(chars)]
    col = 1 + (n % 15)
    spd = 5 + (n % 15)
    msg = f"Border Animation (Stage {n})"
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-28)/2,0,"{msg}",{col});
    for(int f=0;f<100;f++) {{
        int o=f%80;
        for(int x=0;x<80;x++) {{ px(x,1,' ',0); px(x,23,' ',0); }}
        for(int y=2;y<23;y++) {{ px(0,y,' ',0); px(79,y,' ',0); }}
        px(o,1,'{c}',{col});
        px(79-o,23,'{c}',{col});
        px(o,23,'{c}',{col});
        px(79-o,1,'{c}',{col});
        px(0,2+o%21,'{c}',{col});
        px(79,2+(o+10)%21,'{c}',{col});
        dl({spd}0000);
        if(kh()){{kg();break;}}
    }}
    clr(0); txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_border)

def t_wave(n):
    col = 1 + (n % 15)
    msg = f"Sine Wave (Stage {n})"
    amp = 5 + (n % 8)
    freq = 2 + (n % 6)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-18)/2,0,"{msg}",{col});
    for(int f=0;f<200;f++) {{
        for(int x=0;x<80;x++)px(x,12,' ',7);
        for(int x=0;x<80;x++) {{
            int y=12+({amp}*(((x+{freq}*f)%{8+amp})%({8+amp})-{4+amp/2}))/6;
            if(y>=2&&y<=22) px(x,y,0xDB,{col});
        }}
        dl(5000);
        if(kh()){{kg();break;}}
    }}
    clr(0); txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_wave)

def t_bars(n):
    col = 1 + (n % 15)
    msg = f"Progress Bars (Stage {n})"
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-22)/2,0,"{msg}",{col});
    for(int p=0;p<=100;p++) {{
        for(int b=0;b<5;b++) {{
            int w=p*(60-(b*8))/100;
            int y=5+b*3;
            for(int x=0;x<60;x++)px(10+x,y,' ',7);
            for(int x=0;x<w;x++)px(10+x,y,0xDB,{col}+b);
        }}
        dl({1500+(n%5)}00);
        if(kh()){{kg();break;}}
    }}
    clr(0); txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_bars)

def t_fib(n):
    msg = f"Fibonacci Sequence (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-26)/2,0,"{msg}",{col});
    uint32_t a=0,b=1;
    for(int i=0;i<60;i++) {{
        pn(10,5+i/10*2,i,{col}+2);
        px(13,5+i/10*2,':',{col});
        pn(15,5+i/10*2,a,{col});
        uint32_t nxt=a+b;
        if(nxt<a){{txt(10,22,"Overflow!",4);break;}}
        a=b;b=nxt;
        dl({4000+(n%8)}00);
        if(kh()){{kg();break;}}
    }}
    clr(0); txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_fib)

def t_primes(n):
    msg = f"Prime Numbers (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
static int ip(int v) {{
    if(v<2)return 0;
    for(int i=2;i*i<=v;i++)if(v%i==0)return 0;
    return 1;
}}

void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-22)/2,0,"{msg}",{col});
    int cnt=0,v=2;
    while(cnt<80) {{
        if(ip(v)) {{
            pn(5+(cnt%8)*9,3+(cnt/8)*2,v,{col}+(cnt%7));
            cnt++;
        }}
        v++;
        if(cnt%5==0)dl(20000);
        if(kh()){{kg();break;}}
    }}
    txt((COLS-20)/2,23,"Press any key...",8);
    wa();
}}
'''

TEMPLATES.append(t_primes)

def t_pong(n):
    col = 1 + (n % 15)
    msg = f"Solo Pong (Stage {n})"
    spd = 8 + (n % 12)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-16)/2,0,"{msg}",{col});
    int bx=40,by=12,bdx=1,bdy=1;
    int py=12;
    for(int f=0;f<500;f++) {{
        for(int i=0;i<80;i++){{px(i,1,' ',0);px(i,24,' ',0);}}
        px(1,py,0xDB,{col});px(1,py+1,0xDB,{col});px(1,py+2,0xDB,{col});
        bx+=bdx;by+=bdy;
        if(by<=2||by>=23)bdy=-bdy;
        if(bx<=2){{bdx=-bdx;if(by>=py-1&&by<=py+3){{}}else{{txt(30,12,"GAME OVER",4);wa();clr(0);txt((COLS-20)/2,12,"Score: 0",7);goto scr;}}}}
        if(bx>=78)bdx=-bdx;
        px(bx,by,0xDB,{col}+3);
        if(kh()){{uint8_t k=kg();if(!(k&0x80)){{if(k==0x48&&py>2)py--;if(k==0x50&&py<21)py++;}}}}
        dl({spd*1000});
        if(kh()){{kg();break;}}
    }}
    clr(0);
scr:
    txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_pong)

def t_maze(n):
    msg = f"Maze Generator (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
static uint32_t rnd={n*12345};
static int rn(void){{rnd=rnd*1103515245+12345;return(rnd>>16)&0x7FFF;}}

void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-24)/2,0,"{msg}",{col});
    uint8_t m[80*24];for(int i=0;i<80*24;i++)m[i]=1;
    int sx=2,sy=2;m[sy*80+sx]=0;
    int dx[]={{1,-1,0,0}},dy[]={{0,0,1,-1}};
    for(int c=0;c<500;c++) {{
        int d=rn()%4;
        int nx=sx+dx[d]*2,ny=sy+dy[d]*2;
        if(nx>0&&nx<79&&ny>1&&ny<23&&m[ny*80+nx]) {{
            m[(sy+dy[d])*80+sx+dx[d]]=0;
            m[ny*80+nx]=0;
            sx=nx;sy=ny;
        }} else {{
            int ok=0;
            for(int t=0;t<20;t++) {{
                d=rn()%4;
                nx=sx+dx[d]*2;ny=sy+dy[d]*2;
                if(nx>0&&nx<79&&ny>1&&ny<23&&m[ny*80+nx]){{ok=1;break;}}
            }}
            if(!ok){{sx=2+rn()%38*2;sy=2+rn()%10*2;}}
        }}
        for(int y=2;y<23;y++)for(int x=0;x<80;x++)
            px(x,y,m[y*80+x]?0xDB:' ',(m[y*80+x]?{col}:0));
        dl(5000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_maze)

def t_spiral(n):
    msg = f"Spiral Pattern (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-22)/2,0,"{msg}",{col});
    for(int f=0;f<300;f++) {{
        for(int x=0;x<80;x++)for(int y=2;y<24;y++)px(x,y,' ',0);
        for(int i=0;i<f;i++) {{
            float a=i*0.2f;
            int r=i/12+1;
            int x=40+({1+(n%4)}*r+(int)(a*{2+(n%3)}))%({1+(n%3)*10})-5;
            int y=13+({1+((n+1)%4)}*r/2+(int)(a*{1+((n+2)%3)}))%({2+(n%4)*2})-{1+(n%4)};
            if(x>=0&&x<80&&y>=2&&y<24)px(x,y,0xDB,{col}+(i%7));
        }}
        dl(8000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_spiral)

def t_snow(n):
    msg = f"Falling Snow (Stage {n})"
    col = 7 + (n % 8)
    count = 30 + (n % 40)
    return HEADER.format() + f'''
static uint32_t sr={n*55555};
static int srn(void){{sr=sr*1103515245+12345;return(sr>>16)&0x7FFF;}}

void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-20)/2,0,"{msg}",{col});
    int sx[{count}],sy[{count}];
    for(int i=0;i<{count};i++){{sx[i]=srn()%80;sy[i]=srn()%22+2;}}

    for(int f=0;f<300;f++) {{
        for(int i=0;i<{count};i++) {{
            px(sx[i],sy[i],' ',0);
            sy[i]++;if(sy[i]>=24){{sy[i]=2;sx[i]=srn()%80;}}
            if(srn()%3==0)sx[i]+=(srn()%3)-1;
            if(sx[i]<0)sx[i]=79;
            if(sx[i]>=80)sx[i]=0;
            px(sx[i],sy[i],'.',{col});
        }}
        dl(15000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_snow)

def t_fire(n):
    msg = f"Fire Effect (Stage {n})"
    col = 4 + (n % 8)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-18)/2,0,"{msg}",{col});
    uint8_t fv[80*24];for(int i=0;i<80*24;i++)fv[i]=0;
    for(int t=0;t<300;t++) {{
        for(int x=0;x<80;x++)fv[(23)*80+x]=(t%2)?({30+(n%20)}):(0);
        for(int y=2;y<23;y++)for(int x=1;x<79;x++) {{
            int v=fv[(y+1)*80+x];
            if(v>({2+(n%4)}))v-=({2+(n%4)});
            else v=0;
            if(x>0){{int av=fv[(y+1)*80+x-1];if(av>v)v=av;}}
            if(x<79){{int av=fv[(y+1)*80+x+1];if(av>v)v=av;}}
            if(v>0)v-=({1+(n%3)});
            if(v<0)v=0;
            fv[y*80+x]=v;
            uint8_t cc=0;
            if(v>{15+(n%10)})cc={col}*16+{col};
            else if(v>{10+(n%8)})cc={col}*16+(({col}+8)&0xF);
            else if(v>{5+(n%6)})cc=(({col}+6)&0xF)*16+(({col}+6)&0xF);
            else if(v>2)cc=0x80+0x08;
            else cc=0;
            if(cc)px(x,y,0xDB,cc);else px(x,y,' ',0);
        }}
        dl(10000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_fire)

def t_matrix(n):
    msg = f"Digital Rain (Stage {n})"
    col = 0x0A + (n % 5)
    return HEADER.format() + f'''
static uint32_t mr={n*33333};
static int mrn(void){{mr=mr*1103515245+12345;return(mr>>16)&0x7FFF;}}

void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-20)/2,0,"{msg}",{col});
    int pos[80],spd[80],len[80];
    for(int i=0;i<80;i++){{pos[i]=mrn()%24;spd[i]=1+(mrn()%4);len[i]=3+(mrn()%10);}}
    const char ch[]={{0x41,0x4B,0x51,0x30,0x39,0x7C,0x24,0x25,0x23,0x40}};

    for(int f=0;f<200;f++) {{
        for(int x=0;x<80;x++) {{
            if(f%spd[x]==0) {{
                if(pos[x]>0&&pos[x]<=24)px(x,pos[x]-1,' ',0);
                if(pos[x]>=0&&pos[x]<24)px(x,pos[x],ch[mrn()%10],{col});
                pos[x]++;
                if(pos[x]>=24+len[x]){{pos[x]=0;spd[x]=1+(mrn()%4);len[x]=3+(mrn()%10);}}
            }}
        }}
        dl(8000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_matrix)

def t_life(n):
    msg = f"Game of Life (Stage {n})"
    col = 2 + (n % 12)
    return HEADER.format() + f'''
static uint32_t lr={n*77777};
static int lrn(void){{lr=lr*1103515245+12345;return(lr>>16)&0x7FFF;}}

void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-20)/2,0,"{msg}",{col});
    uint8_t g[82*26]={{0}};
    for(int i=0;i<400;i++)g[(2+(lrn()%21))*82+1+(lrn()%78)]=1;

    for(int gen=0;gen<100;gen++) {{
        uint8_t ng[82*26]={{0}};
        for(int y=2;y<24;y++)for(int x=1;x<80;x++) {{
            int n=g[(y-1)*82+(x-1)]+g[(y-1)*82+x]+g[(y-1)*82+(x+1)]
                 +g[y*82+(x-1)]+g[y*82+(x+1)]
                 +g[(y+1)*82+(x-1)]+g[(y+1)*82+x]+g[(y+1)*82+(x+1)];
            if(g[y*82+x])ng[y*82+x]=(n==2||n==3)?1:0;
            else ng[y*82+x]=(n==3)?1:0;
        }}
        for(int y=2;y<24;y++)for(int x=1;x<80;x++) {{
            g[y*82+x]=ng[y*82+x];
            px(x,y,g[y*82+x]?0xDB:' ',g[y*82+x]?({col}):0);
        }}
        dl(20000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_life)

def t_clock(n):
    msg = f"Binary Clock (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-18)/2,0,"{msg}",{col});
    for(int f=0;f<200;f++) {{
        int b[]={{f/3600%24,(f/60)%60,f%60}};
        for(int i=0;i<3;i++) {{
            for(int y=0;y<6;y++) {{
                int bit=(b[i]>>(5-y))&1;
                for(int x=0;x<3;x++)
                    px(10+i*25+x,5+y,bit?0xDB:' ',bit?({col}+i*4):0);
            }}
        }}
        for(int i=0;i<3;i++) {{
            txt(10+i*25,12,":",{col});
            int v=b[i];
            px(16+i*25,12,'0'+(v/10)%10,{col});
            px(19+i*25,12,'0'+v%10,{col});
            txt(10+i*25,13,"-----",{col});
        }}
        txt(10,14,"H",{col});txt(35,14,"M",{col});txt(60,14,"S",{col});
        dl(50000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_clock)

def t_noise(n):
    msg = f"Perlin-ish Noise (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
static uint32_t nr={n*99999};
static int nrn(void){{nr=nr*1103515245+12345;return(nr>>16)&0x7FFF;}}

void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-24)/2,0,"{msg}",{col});
    for(int f=0;f<200;f++) {{
        for(int y=2;y<24;y++)for(int x=0;x<80;x++) {{
            int v=nrn()%({8+(n%16)});
            px(x,y,v>2?0xDB:' ',v>2?({col}+v%8):0);
        }}
        dl(15000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_noise)

def t_collatz(n):
    msg = f"Collatz Conjecture (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-24)/2,0,"{msg}",{col});
    uint32_t v={10+n*7};
    for(int i=0;i<200;i++) {{
        pn(5,5+i/18*2,i,{col}+2);
        px(8,5+i/18*2,':',{col}+2);
        pn(10,5+i/18*2,v,{col});
        if(v%2==0)v/=2;else v=v*3+1;
        if(v==1){{txt(30,12,"Reached 1!",{col}+4);break;}}
        dl({4000+(n%8)}00);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_collatz)

def t_awk(n):
    msg = f"AWK Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
#define NR 5
struct rec {{const char *n;const char *c;int a;}};
static struct rec tab[NR]={{{{.n="Alice",.c="NYC",.a=25}},{{.n="Bob",.c="SF",.a=31}},{{.n="Carol",.c="LA",.a=22}},{{.n="Dave",.c="CHI",.a=38}},{{.n="Eve",.c="SEA",.a=29}}}};

void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-18)/2,0,"{msg}",{col});
    for(int p=0;p<6;p++) {{
        clr(0);
        txt((COLS-18)/2,0,"{msg}",{col});
        txt(2,2,"Awk program: ",7);
        const char *progs[]={{"{{print $0}}",              "{{print $1, $3}}",
                             "/[ou]/",                     "$3 > 30",
                             "{{sum+=$3}} {{print sum}}",  "{{print $2}}"}};
        txt(16,2,progs[p],{col}+2);
        int sum=0;
        for(int i=0;i<NR;i++) {{
            int y=6+i*2;
            txt(4,y,"$0:",7);txt(8,y,tab[i].n,7);txt(16,y,tab[i].c,7);pn(26,y,tab[i].a,7);
            int hi=0;
            if(p==0){{}}/* print all */
            else if(p==1){{}}/* print $1 $3 */
            else if(p==2){{int m=0;for(int j=0;tab[i].n[j];j++)if(tab[i].n[j]=='o'||tab[i].n[j]=='u')m=1;hi=m;}}
            else if(p==3){{hi=(tab[i].a>30);}}
            else if(p==4){{sum+=tab[i].a;}}
            else if(p==5){{}}/* print $2 */
            if(hi){{txt(8,y,tab[i].n,{col}+4);txt(16,y,tab[i].c,{col}+4);pn(26,y,tab[i].a,{col}+4);}}
            if(p==1){{txt(4,y,"$1 $3:",7);txt(8,y,tab[i].n,{col}+2);pn(26,y,tab[i].a,{col}+2);}}
            if(p==5){{txt(4,y,"$2:",7);txt(16,y,tab[i].c,{col}+2);}}
        }}
        if(p==4){{txt(4,16,"Sum age:",7);pn(15,16,sum,{col}+2);}}
        if(p==1){{txt(4,18,"(print name + age only)",8);}}
        if(p==2){{txt(4,18,"(pattern /[ou]/ in name)",8);}}
        if(p==3){{txt(4,18,"(filter: age > 30)",8);}}
        if(p==5){{txt(4,18,"(print cities only)",8);}}
        dl(800000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_awk)

# Assign templates to stages 9-100
def assign_templates():
    assignments = {}
    for stage in range(9, 101):
        idx = (stage - 9) % len(TEMPLATES)
        variations = (stage - 9) // len(TEMPLATES)
        # Each stage gets its template with its own number baked in
        assignments[stage] = TEMPLATES[idx]
    return assignments

def generate():
    assignments = assign_templates()
    for n in range(9, 101):
        fn = assignments[n]
        code = fn(n)
        d = os.path.join(PROJECT, f"stage{n}")
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, "main.c"), "w") as f:
            f.write(code)
        print(f"  Stage {n}: {fn.__name__[2:]}")

    # Generate Makefile fragments
    mf_lines = []
    mf_lines.append("# Auto-generated by gen_stages.py")
    mf_lines.append("STAGE_COUNT := 100")
    mf_lines.append("")
    all_bins = []
    all_dirs_parts = []
    for n in range(9, 101):
        sv = f"STAGE{n}"
        all_bins.append(f"$({sv}_BIN)")
        all_dirs_parts.append(f"$(BUILD)/$({sv})")
        mf_lines.append(f"{sv} := stage{n}")
        mf_lines.append(f"{sv}_SRCS := stage{n}/main.c")
        mf_lines.append(f"{sv}_OBJS := $(BUILD)/stage{n}/main.o")
        mf_lines.append(f"{sv}_BIN := $(BUILD)/stage{n}.bin")
        mf_lines.append("")

    mf_lines.append("# All generated stage binaries")
    mf_lines.append(f"STAGE_BINS := {' '.join(all_bins)}")
    mf_lines.append("")
    mf_lines.append("# All generated stage build dirs")
    mf_lines.append(f"STAGE_DIRS := \\")
    for part in all_dirs_parts:
        mf_lines.append(f"\t\t{part} \\")
    mf_lines[-1] = mf_lines[-1].rstrip(" \\")
    mf_lines.append("")

    for n in range(9, 101):
        mf_lines.append(f"$(BUILD)/stage{n}/%.o: stage{n}/%.c")
        mf_lines.append(f"\t@mkdir -p $(@D)")
        mf_lines.append(f"\t$(CC) $(CFLAGS_64) $< -o $@")
        mf_lines.append("")
        mf_lines.append(f"$(BUILD)/stage{n}.bin: $(STAGE{n}_OBJS)")
        mf_lines.append(f"\t$(LD) $(LDFLAGS_64_STAGE{n}) $^ -o $@")
        mf_lines.append("")

    os.makedirs(BUILD, exist_ok=True)
    with open(os.path.join(BUILD, "stages.mk"), "w") as f:
        f.write("\n".join(mf_lines))
        f.write("\n")

    ldf_lines = []
    ldf_lines.append("# Auto-generated LDFLAGS for stages 9-100")
    for n in range(9, 101):
        addr = 0x30000 + (n - 5) * 0x10000
        ldf_lines.append(
            f"LDFLAGS_64_STAGE{n} = -Ttext 0x{addr:X} --oformat binary "
            f"-e stage{n}_entry"
        )
    with open(os.path.join(BUILD, "ldflags.mk"), "w") as f:
        f.write("\n".join(ldf_lines))
        f.write("\n")

    print(f"\nGenerated {92} unique stages (9-100)")

if __name__ == "__main__":
    generate()
