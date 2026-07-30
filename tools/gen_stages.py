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

def t_php(n):
    msg = f"PHP Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-16)/2,0,"{msg}",{col});
    const char *rows[]={{"$row[0]='Alice'; $row[1]=25; $row[2]='NYC';",
                          "$row[0]='Bob';   $row[1]=31; $row[2]='SF';",
                          "$row[0]='Carol'; $row[1]=22; $row[2]='LA';",
                          "$row[0]='Dave';  $row[1]=38; $row[2]='CHI';",
                          "$row[0]='Eve';   $row[1]=29; $row[2]='SEA';"}};
    for(int f=0;f<120;f++) {{
        clr(0);
        txt((COLS-16)/2,0,"{msg}",{col});
        txt(2,2,"<?php",{col}+2);
        txt(2,4,"$data = [",7);
        for(int i=0;i<5;i++) {{
            int y=6+i*2;
            txt(4,y,rows[i],f%2?7:{col}+4);
            if(i==f%5){{txt(4,y,rows[i],{col}+6);}}
        }}
        txt(2,17,"];",7);
        txt(2,19,"echo '<table>'",7);
        txt(2,21,"foreach($data as $r):",7);
        txt(2,22,"  echo '<tr>...</tr>';",7);
        txt(2,23,"endforeach;",7);
        int stage=f%6;
        txt(2,19+(stage>2),stage<3?"/* BUILDING TABLE */":"/* RENDERING HTML */",{col}+2);
        dl(50000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_php)

def t_js(n):
    msg = f"JavaScript Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-24)/2,0,"{msg}",{col});
    int boxes[6][4];
    for(int i=0;i<6;i++){{boxes[i][0]=5+i*12;boxes[i][1]=4+i%3*6;boxes[i][2]=8;boxes[i][3]=4;}}
    for(int f=0;f<200;f++) {{
        clr(0);
        txt((COLS-24)/2,0,"{msg}",{col});
        txt(2,2,"// DOM-like animation",{col}+2);
        for(int i=0;i<6;i++) {{
            int idx=(f+i)%6;
            boxes[idx][0]+=(i%3-1);
            boxes[idx][1]+=(i%2?1:-1);
            int bx=boxes[idx][0],by=boxes[idx][1];
            if(bx<1||bx>72)boxes[idx][0]=bx<1?5:69;
            if(by<2||by>21)boxes[idx][1]=by<2?4:20;
            bx=boxes[idx][0];by=boxes[idx][1];
            for(int dy=0;dy<boxes[idx][3];dy++)
                for(int dx=0;dx<boxes[idx][2];dx++)
                    px(bx+dx,by+dy,0xDB,({col}+idx)%15+1);
        }}
        txt(2,24,"for box of boxes {{ box.x+=vx; box.y+=vy; }}",8);
        dl(30000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_js)

def t_ruby(n):
    msg = f"Ruby Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-16)/2,0,"{msg}",{col});
    for(int f=0;f<150;f++) {{
        clr(0);
        txt((COLS-16)/2,0,"{msg}",{col});
        txt(2,2,"5.times do |i|",{col}+2);
        for(int i=0;i<5;i++) {{
            int y=5+i*3;
            int n=i+1+(f%(5-i));
            txt(2,y,"  puts",8);
            for(int d=0;d<n;d++)px(10+d,y,0x2A,{col}+(i+d)%7+1);
            pn(16+n,y,n,{col}+2);
            txt(2,y+1,"  yield i*2",{col}+4);
            pn(16,y+1,i*2,{col}+4);
        }}
        txt(2,22,"end",{col}+2);
        txt(2,23,"# => iterates 5 times with block",8);
        dl(60000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_ruby)

def t_python(n):
    msg = f"Python Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-20)/2,0,"{msg}",{col});
    for(int f=0;f<150;f++) {{
        clr(0);
        txt((COLS-20)/2,0,"{msg}",{col});
        txt(2,2,"# list comprehension",{col}+2);
        for(int i=0;i<8;i++) {{
            int y=4+i*2;
            txt(2,y,"result = [x*2 for x in range(10)]",7);
            pn(2,y+1,i*2,{col}+3);
            px(16,y+1,'|',7);
            int n=1+(i+f)%10;
            for(int d=0;d<n;d++)px(18+d,y+1,0xFE,{col}+d%6+1);
        }}
        txt(2,22,"# => [0, 2, 4, 6, 8, 10, 12, 14, 16, 18]",8);
        dl(50000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_python)

def t_rust(n):
    msg = f"Rust Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-16)/2,0,"{msg}",{col});
    for(int f=0;f<120;f++) {{
        clr(0);
        txt((COLS-16)/2,0,"{msg}",{col});
        txt(2,2,"let v = vec![1,2,3,4,5];",{col}+2);
        txt(2,4,"let doubled: Vec<i32> =",7);
        txt(2,5,"    v.iter().map(|x| x * 2).collect();",7);
        for(int i=0;i<5;i++) {{
            int val=(i+1)*(1+f%6);
            pn(6+i*5,8,val,{col}+(i%7));
            px(10+i*5,8,',',7);
        }}
        txt(2,10,"let owned = v.clone(); // ownership",{col}+4);
        txt(2,12,"match doubled[0] {{",{col}+2);
        txt(2,13,"  2 => println!(\\"first is 2\\"),",7);
        txt(2,14,"  _ => (),",7);
        txt(2,15,"}}",{col}+2);
        txt(2,17,"fn greet(name: &str) -> String {{",{col}+3);
        txt(2,18,"    format!(\\\"Hello {{}}\\", name)",7);
        txt(2,19,"}}",{col}+3);
        dl(60000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_rust)

def t_go(n):
    msg = f"Go Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-14)/2,0,"{msg}",{col});
    for(int f=0;f<150;f++) {{
        clr(0);
        txt((COLS-14)/2,0,"{msg}",{col});
        txt(2,2,"package main",{col}+2);
        txt(2,4,"func main() {{",7);
        txt(2,5,"  ch := make(chan int)",7);
        txt(2,6,"  go func() {{",{col}+4);
        txt(2,7,"    for i := 0; i < 5; i++ {{",7);
        txt(2,8,"      ch <- i * 2",7);
        txt(2,9,"    }}",7);
        txt(2,10,"    close(ch)",7);
        txt(2,11,"  }}()",{col}+4);
        for(int i=0;i<5;i++) {{
            int y=13+i;
            int v=i*(2+f%4);
            pn(6,y,v,{col}+(i%6)+1);
            for(int d=0;d<v;d++)px(10+d,y,0xFE,{col}+(d%7));
        }}
        txt(2,20,"  for v := range ch {{",{col}+2);
        txt(2,21,"    fmt.Println(v)",7);
        txt(2,22,"  }}",{col}+2);
        dl(50000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_go)

def t_lisp(n):
    msg = f"Lisp Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-16)/2,0,"{msg}",{col});
    for(int f=0;f<120;f++) {{
        clr(0);
        txt((COLS-16)/2,0,"{msg}",{col});
        txt(2,2,";; Parentheses: the final frontier",{col}+2);
        txt(2,4,"(defun fib (n)",7);
        txt(2,5,"  (if (<= n 1)",7);
        txt(2,6,"      n",7);
        txt(2,7,"      (+ (fib (- n 1))",7);
        txt(2,8,"         (fib (- n 2)))))",7);
        for(int i=0;i<6;i++) {{
            int y=10+i*2;
            txt(2,y,"(mapcar (lambda (x) (* x 2))",{col}+(i%3)+1);
            txt(2,y+1,"        '(",7);
            for(int d=0;d<=i;d++){{pn(12+d*4,y+1,(d+1)*2,{col}+d+1);if(d<i)px(15+d*4,y+1,' ',7);}}
            txt(2,y+1+8,"))",{col}+(i%3)+1);
        }}
        txt(2,24,";; (loop for x from 0 to 10 collect x)",8);
        dl(70000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_lisp)

def t_sql(n):
    msg = f"SQL Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-14)/2,0,"{msg}",{col});
    const char *data[]={{"1|Alice|25|NYC","2|Bob|31|SF","3|Carol|22|LA","4|Dave|38|CHI","5|Eve|29|SEA"}};
    for(int f=0;f<120;f++) {{
        clr(0);
        txt((COLS-14)/2,0,"{msg}",{col});
        txt(2,2,"SELECT users.name, orders.total",{col}+2);
        txt(2,3,"FROM users",{col}+2);
        txt(2,4,"JOIN orders ON users.id = orders.user_id",{col}+4);
        txt(2,5,"WHERE users.age > 25",7);
        txt(2,6,"ORDER BY orders.total DESC;",7);
        txt(2,8,"+----+-------+-----+-------+",{col}+1);
        txt(2,9,"| id | name  | age | city  |",{col}+3);
        txt(2,10,"+----+-------+-----+-------+",{col}+1);
        for(int i=0;i<5;i++) {{
            int hi=(f%5)==i;
            txt(2,11+i,data[i],hi?{col}+6:7);
        }}
        txt(2,16,"+----+-------+-----+-------+",{col}+1);
        int cnt=0;for(int i=0;i<5;i++)if(i*10+20<f)cnt++;
        txt(2,18,"Rows returned:",7);
        pn(14,18,cnt,{col}+2);
        dl(60000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_sql)

def t_haskell(n):
    msg = f"Haskell Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-20)/2,0,"{msg}",{col});
    for(int f=0;f<120;f++) {{
        clr(0);
        txt((COLS-20)/2,0,"{msg}",{col});
        txt(2,2,"-- Pure functional programming",{col}+2);
        txt(2,4,"fibs = 0 : 1 : zipWith (+) fibs (tail fibs)",7);
        txt(2,6,"fmap (+1) (Just 5)  -- Just 6",7);
        txt(2,8,"pure (*2) <*> [1,2,3] -- [2,4,6]",7);
        for(int i=0;i<6;i++) {{
            int y=10+i*2;
            int a=i;
            txt(2,y,"let x = ",7);pn(9,y,a,{col}+2);
            txt(2,y+1,"let y = fmap (*",7);pn(9,y+1,f%5+1,{col}+3);txt(2,y+1+8,") x",7);
        }}
        txt(2,23,"main = putStrLn \\\"Hello, Haskell!\\\"",8);
        txt(2,24,"-- Result: ",8);pn(12,24,f%10,{col}+4);
        dl(60000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_haskell)

def t_brainfuck(n):
    msg = f"Brainfuck Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-24)/2,0,"{msg}",{col});
    const char *progs[]={{"+++++[>+++++<-]>+++++.",
                          "+++[>+++++<-]>[>+++++>+++++<<-]>>.",
                          "+++++++++[>++++++++>+++++++++++++>+++++<<<-]>-.>+.>..",
                          ">+>+>+<<<[->[->+>+<<]>>[-<<+>>]<<<]>>>.",
                          "+>+>[->>>+<<<]>>>[-<<<+<<<+>>>>]<<<[->+>+<<]>>[-<<+>>]>>>."}};
    const char *pnames[]={{"5×5=25", "3×5=15", "ASCII ABC", "Fibonacci", "Addition"}};
    int pidx=({n}/2)%5;
    const char *prog=progs[pidx];
    const char *pname=pnames[pidx];
    char tape[24];for(int i=0;i<24;i++)tape[i]=0;
    int ptr=12,pc=0,outc=0,wait=0;
    char output[20];for(int i=0;i<20;i++)output[i]=0;
    for(int f=0;f<300;f++) {{
        clr(0);
        txt((COLS-24)/2,0,"{msg}",{col});
        txt(2,2,"Program:",{col}+2);txt(10,2,pname,{col}+4);
        for(int i=0;prog[i];i++) {{
            char c[2]={{prog[i],0}};
            txt(2+i,3,c,i==pc?{col}+6:7);
        }}
        txt(2,5,"Tape:",{col}+2);
        for(int i=0;i<20;i++) {{
            int hi=(i==ptr);
            px(2+i*3,6,hi?'[':' ',hi?{col}+4:0);
            pn(3+i*3,6,(int)tape[i],hi?{col}+6:7);
            px(2+i*3+7,6,hi?']':' ',hi?{col}+4:0);
        }}
        txt(2,8,"Output:",{col}+2);
        for(int i=0;i<outc&&i<15;i++) px(2+i,9,output[i]?output[i]:' ',{col}+3);
        if(wait==0) {{
            char cmd=prog[pc];
            if(cmd=='+') tape[ptr]++;
            else if(cmd=='-') tape[ptr]--;
            else if(cmd=='>'&&ptr<23) ptr++;
            else if(cmd=='<'&&ptr>0) ptr--;
            else if(cmd=='.'&&outc<19) {{output[outc]=tape[ptr];outc++;}}
            else if(cmd=='['&&tape[ptr]==0) {{int d=1;while(d){{pc++;if(prog[pc]=='[')d++;if(prog[pc]==']')d--;}}}}
            else if(cmd==']'&&tape[ptr]!=0) {{int d=1;while(d){{pc--;if(prog[pc]==']')d++;if(prog[pc]=='[')d--;}}}}
            pc++;if(!prog[pc])pc=0;
            wait=2;
        }} else wait--;
        txt(2,11,"PC:",7);pn(6,11,pc,{col}+2);
        txt(2,11,"Cell:",7);pn(12,11,(int)tape[ptr],{col}+4);
        txt(2,13,"Instr:",7);
        if(prog[pc]){{char ci[2]={{prog[pc],0}};px(9,13,ci[0],{col}+6);}}
        txt(2,15,"Cells: 24 | Programs: 5 | Speed: 2fps",8);
        for(int i=0;i<f%40;i++)px(2+i,17,0xDB,{col}+(i%7));
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_brainfuck)

def t_brainfuck2(n):
    msg = f"Brainfuck 2 (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-20)/2,0,"{msg}",{col});
    const char *progs[]={{">+++++++++[<++++++++>-]<.>+++++++[<++++>-]<+.+++++++..+++."
                          ">>++++++++[<++++++>-]<.------------.>+++++++++[<-------->-]<+."
                          ">+++++++[<++++>-]<.>++++++++++[<--------->-]<-.>+++++[<+++++>-]<+.",
                          "++++[>++++<-]>[>+++++>+++++<<-]>>.>+>+>+<<<[->[->+>+<<]>>[-<<+>>]<<<]>>>.",
                          "+++++++++[>++++++++<-]>."}};
    const char *pnames[]={{"\\"Hi!\\"", "Squares", "ASCII N"}};
    int pidx=({n}/2)%3;
    const char *prog=progs[pidx];
    const char *pname=pnames[pidx];
    char tape[16];for(int i=0;i<16;i++)tape[i]=0;
    int ptr=0,pc=0,outc=0,wait=0,phase=0;
    char outbuf[32];for(int i=0;i<32;i++)outbuf[i]=0;
    for(int f=0;f<250;f++) {{
        clr(0);
        txt((COLS-20)/2,0,"{msg}",{col});
        txt(2,2,"BF Program:",{col}+2);txt(13,2,pname,{col}+4);
        txt(2,3,prog,7);
        txt(2,5,"[",{col}+1);
        for(int i=0;i<16;i++) {{
            int val=(int)tape[i];
            uint8_t c=(val>=32&&val<127)?(uint8_t)val:'.';
            uint8_t clr=i==ptr?{col}+6:((val>0)?{col}+2:7);
            px(3+i,5,c,clr);
        }}
        txt(2+16+1,5,"]",{col}+1);
        txt(2,7,"Output:",{col}+2);
        for(int i=0;i<outc&&i<30;i++) px(2+i,8,outbuf[i]?outbuf[i]:' ',{col}+3);
        if(wait==0) {{
            char cmd=prog[pc];
            if(cmd=='+') tape[ptr]++;
            else if(cmd=='-') tape[ptr]--;
            else if(cmd=='>'&&ptr<15) ptr++;
            else if(cmd=='<'&&ptr>0) ptr--;
            else if(cmd=='.'&&outc<31) {{outbuf[outc]=tape[ptr];outc++;}}
            else if(cmd=='['&&tape[ptr]==0) {{int d=1;while(d){{pc++;if(prog[pc]=='[')d++;if(prog[pc]==']')d--;}}}}
            else if(cmd==']'&&tape[ptr]!=0) {{int d=1;while(d){{pc--;if(prog[pc]==']')d++;if(prog[pc]=='[')d--;}}}}
            pc++;if(!prog[pc]){{pc=0;phase++;if(phase>2)break;}}
            wait=3;
        }} else wait--;
        txt(2,10,"ASCII map:",{col}+2);
        for(int i=0;i<16;i++) {{
            int v=(int)tape[i];
            if(v>0&&v<16) {{px(2+i*3,12,0xB0,{col}+v);}}
            else if(v>=16) {{px(2+i*3,12,0xDB,{col}+5);}}
        }}
        txt(2,14,"ptr={{ptr}}",{col}+2);pn(9,14,ptr,{col}+4);
        txt(2,16,"Commands: + - > < [ ] , .",8);
        dl(20000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_brainfuck2)

def t_lua(n):
    msg = f"Lua Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-14)/2,0,"{msg}",{col});
    for(int f=0;f<120;f++) {{
        clr(0);
        txt((COLS-14)/2,0,"{msg}",{col});
        txt(2,2,"-- Tables are everything!",{col}+2);
        txt(2,4,"local t = {{name=\\"Alice\\", age=25}}",7);
        txt(2,5,"t.city = \\"NYC\\"",7);
        txt(2,6,"print(t.name, t.age)",7);
        txt(2,8,"local function fib(n)",{col}+4);
        txt(2,9,"  if n <= 1 then return n end",7);
        txt(2,10,"  return fib(n-1) + fib(n-2)",7);
        txt(2,11,"end",{col}+4);
        for(int i=0;i<6;i++) {{
            int y=13+i;
            int v=i*f%10;
            txt(2,y,"fib(",7);pn(6,y,i,{col}+2);txt(2,y+8,")=",7);pn(11,y,v,{col}+4);
        }}
        txt(2,21,"for k,v in pairs(t) do print(k,v) end",8);
        dl(60000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_lua)

def t_cpp(n):
    msg = f"C++ Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-16)/2,0,"{msg}",{col});
    for(int f=0;f<120;f++) {{
        clr(0);
        txt((COLS-16)/2,0,"{msg}",{col});
        txt(2,2,"template<typename T>",{col}+2);
        txt(2,3,"class Vector {{",{col}+2);
        txt(2,4,"  T* data; size_t len;",7);
        txt(2,5,"public:",{col}+4);
        txt(2,6,"  Vector() : data(nullptr), len(0) {{}}",7);
        txt(2,7,"  void push_back(const T& val) {{...}}",7);
        txt(2,8,"  T& operator[](size_t i) {{ return data[i]; }}",7);
        txt(2,9,"}};",{col}+2);
        txt(2,11,"Vector<int> v;",7);
        txt(2,12,"v.push_back(42);",7);
        for(int i=0;i<6;i++) {{
            int y=14+i;
            txt(2,y,"v[",7);pn(4,y,i,{col});txt(2,y+6,"]=",7);pn(9,y,(i+1)*(f%5+1),{col}+3);
        }}
        txt(2,22,"auto result = v | views::filter(...)",8);
        dl(60000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_cpp)

def t_bash(n):
    msg = f"Bash Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-16)/2,0,"{msg}",{col});
    for(int f=0;f<150;f++) {{
        clr(0);
        txt((COLS-16)/2,0,"{msg}",{col});
        txt(2,2,"#!/bin/bash",{col}+2);
        txt(2,4,"for file in *.txt; do",7);
        txt(2,5,"  echo \\"Processing $file...\\"",7);
        txt(2,6,"  grep 'error' \\"$file\\" | wc -l",7);
        txt(2,7,"done",7);
        txt(2,9,"ls -la | awk '{{print $9, $5}}'",7);
        txt(2,11,"PIPELINE:",{col}+4);
        const char *stages[]={{"ls","grep","sort","uniq","wc"}};
        for(int i=0;i<5;i++) {{
            int c=i==(f/6)%5?{col}+6:8;
            txt(4+i*13,13,stages[i],c);
            if(i<4)txt(16+i*13,13,"|",7);
        }}
        txt(2,15,"$ find /home -name \\"*.conf\\" 2>/dev/null",8);
        txt(2,17,"$  ",7);pn(5,17,f%1000,{col}+2);txt(2,18," exit code",8);
        for(int i=0;i<5;i++) {{
            pn(4+i*15,20,i*f%256,{col}+i+1);
        }}
        dl(50000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_bash)

def t_perl(n):
    msg = f"Perl Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-16)/2,0,"{msg}",{col});
    for(int f=0;f<120;f++) {{
        clr(0);
        txt((COLS-16)/2,0,"{msg}",{col});
        txt(2,2,"#!/usr/bin/perl -w",{col}+2);
        txt(2,4,"my @array = (1..10);",7);
        txt(2,5,"my %hash = (foo => 42, bar => 99);",7);
        txt(2,6,"print map {{ $_ * 2 }} @array;",7);
        for(int i=0;i<8;i++) {{
            int v=(i+1)*(f%6+1);
            pn(4+i*5,8,v,{col}+(i%7));
        }}
        txt(2,10,"s/foo/bar/g if /regex/",{col}+4);
        txt(2,12,"sub greet {{",7);
        txt(2,13,"  my ($name) = @_;",7);
        txt(2,14,"  return \\"Hello, $name!\\";",7);
        txt(2,15,"}}",7);
        txt(2,17,"print greet('World');",{col}+2);
        txt(2,19,"TMTOWTDI:",8);
        txt(2,20,"There's More Than One Way To Do It",{col}+3);
        dl(60000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_perl)

def t_typescript(n):
    msg = f"TypeScript Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-24)/2,0,"{msg}",{col});
    for(int f=0;f<120;f++) {{
        clr(0);
        txt((COLS-24)/2,0,"{msg}",{col});
        txt(2,2,"interface Person {{",{col}+2);
        txt(2,3,"  readonly name: string;",7);
        txt(2,4,"  age: number;",7);
        txt(2,5,"  city?: string;",7);
        txt(2,6,"}}",{col}+2);
        txt(2,8,"const alice: Person = {{",7);
        txt(2,9,"  name: 'Alice',",7);
        txt(2,10,"  age: 25,",7);
        txt(2,11,"  city: 'NYC'",7);
        txt(2,12,"}};",7);
        for(int i=0;i<4;i++) {{
            int y=14+i;
            txt(2,y,"type Result<T> = T | null;",{col}+i+1);
        }}
        txt(2,19,"function identity<T>(arg: T): T {{",{col}+4);
        txt(2,20,"  return arg;",7);
        txt(2,21,"}}",{col}+4);
        txt(2,23,"const num = identity<number>(42);",8);
        dl(60000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_typescript)

def t_kotlin(n):
    msg = f"Kotlin Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-20)/2,0,"{msg}",{col});
    for(int f=0;f<120;f++) {{
        clr(0);
        txt((COLS-20)/2,0,"{msg}",{col});
        txt(2,2,"// Null safety + lambdas",{col}+2);
        txt(2,4,"val numbers = listOf(1, 2, 3, 4, 5)",7);
        txt(2,5,"val doubled = numbers.map {{ it * 2 }}",7);
        txt(2,6,"val even = numbers.filter {{ it % 2 == 0 }}",7);
        for(int i=0;i<5;i++) {{
            int v=(i+1)*(f%6+2);
            pn(6+i*5,8,v,{col}+(i%7));
        }}
        txt(2,10,"data class Person(val name: String, val age: Int)",{col}+4);
        txt(2,12,"val alice = Person(\\"Alice\\", 25)",7);
        txt(2,13,"val (name, age) = alice  // destructuring",7);
        txt(2,15,"val result: String? = null",{col}+2);
        txt(2,16,"println(result ?: \\"default\\")  // elvis op",7);
        txt(2,18,"fun Int.isEven() = this % 2 == 0  // extension",{col}+3);
        txt(2,20,"println(42.isEven())  // true",7);
        dl(60000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_kotlin)

def t_swift(n):
    msg = f"Swift Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-20)/2,0,"{msg}",{col});
    for(int f=0;f<120;f++) {{
        clr(0);
        txt((COLS-20)/2,0,"{msg}",{col});
        txt(2,2,"// Protocol-oriented programming",{col}+2);
        txt(2,4,"protocol Greetable {{",7);
        txt(2,5,"  var name: String {{ get }}",7);
        txt(2,6,"  func greet() -> String",7);
        txt(2,7,"}}",7);
        txt(2,9,"struct Person: Greetable {{",{col}+4);
        txt(2,10,"  let name: String",7);
        txt(2,11,"  func greet() -> String {{",7);
        txt(2,12,"    return \\"Hi, \\(name)!\\"",7);
        txt(2,13,"  }}",7);
        txt(2,14,"}}",{col}+4);
        for(int i=0;i<5;i++) {{
            int y=16+i;
            txt(2,y,"let x = Optional(",7);pn(8,y,i*2,{col}+2);txt(2,y+8,")",7);
        }}
        txt(2,22,"let alice = Person(name: \\"Alice\\")",8);
        txt(2,23,"print(alice.greet())  // Hi, Alice!",8);
        dl(60000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_swift)

def t_dart(n):
    msg = f"Dart Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-20)/2,0,"{msg}",{col});
    for(int f=0;f<120;f++) {{
        clr(0);
        txt((COLS-20)/2,0,"{msg}",{col});
        txt(2,2,"// Async/await + named params",{col}+2);
        txt(2,4,"Future<void> fetchData() async {{",7);
        txt(2,5,"  var response = await http.get(url);",7);
        txt(2,6,"  print(response.body);",7);
        txt(2,7,"}}",7);
        txt(2,9,"void main() {{",{col}+4);
        txt(2,10,"  var list = [1, 2, 3, 4, 5];",7);
        txt(2,11,"  var mapped = list.map((e) => e * 2);",7);
        for(int i=0;i<5;i++) {{
            int v=(i+1)*(f%8+1);
            pn(6+i*6,13,v,{col}+(i%7));
        }}
        txt(2,15,"  named({{required int x, int y = 0}})",{col}+2);
        txt(2,17,"  var result = named(x: 42, y: 10);",7);
        txt(2,19,"  runApp(MyApp());",7);
        txt(2,20,"}}",{col}+4);
        txt(2,22,"class MyApp extends StatelessWidget {{...}}",8);
        dl(60000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_dart)

def t_csharp(n):
    msg = f"C# Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-16)/2,0,"{msg}",{col});
    for(int f=0;f<120;f++) {{
        clr(0);
        txt((COLS-16)/2,0,"{msg}",{col});
        txt(2,2,"using System.Linq;",{col}+2);
        txt(2,4,"class Program {{",7);
        txt(2,5,"  static void Main() {{",7);
        txt(2,6,"    var nums = new[] {{1,2,3,4,5}};",7);
        txt(2,7,"    var evens = nums.Where(n => n % 2 == 0);",7);
        txt(2,8,"    var squared = nums.Select(n => n * n);",7);
        for(int i=0;i<5;i++) {{
            int v=(i+1)*(i+1);
            int y=10+i*2;
            pn(6,y,v,{col}+(i%7));
            px(12,y,'=',7);
            pn(14,y,i+1,{col}+2);txt(2,y+6,"^2",7);
        }}
        txt(2,21,"    Console.WriteLine(evens.Count());",7);
        txt(2,22,"  }}",7);
        txt(2,23,"}}",{col}+2);
        dl(60000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_csharp)

def t_forth(n):
    msg = f"Forth Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-18)/2,0,"{msg}",{col});
    int stack[8];int sp=0;
    for(int f=0;f<120;f++) {{
        clr(0);
        txt((COLS-18)/2,0,"{msg}",{col});
        txt(2,2,"\\\\ Stack-based, minimal, beautiful",{col}+2);
        txt(2,4,": square  dup * ;",7);
        txt(2,5,": fib     dup 1 > if 1- dup fib swap 1- fib + then ;",7);
        txt(2,7,"5 3 + 2 * .  \\\\ prints 16",7);
        if(f%3==0&&sp<8){{stack[sp]=f%32;sp++;}}
        if(f%5==0&&sp>0)sp--;
        txt(2,9,"Stack:",{col}+4);
        for(int i=0;i<sp;i++) {{
            pn(8+i*5,10,stack[i],{col}+(i%6)+1);
        }}
        txt(2,12,": stars ( n -- ) 0 do 42 emit loop ;",7);
        int ns=f%16;
        txt(2,14,"10 stars => ",7);
        for(int i=0;i<ns;i++)px(14+i,14,0x2A,{col}+(i%7));
        txt(2,16,": count  10 0 do i . cr loop ;",7);
        txt(2,18,"( The stack is the way )",8);
        dl(60000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_forth)

def t_prolog(n):
    msg = f"Prolog Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-20)/2,0,"{msg}",{col});
    for(int f=0;f<120;f++) {{
        clr(0);
        txt((COLS-20)/2,0,"{msg}",{col});
        txt(2,2,"%% Logic / declarative programming",{col}+2);
        txt(2,4,"parent(alice, bob).",7);
        txt(2,5,"parent(bob, carol).",7);
        txt(2,6,"grandparent(X, Z) :- parent(X, Y), parent(Y, Z).",7);
        for(int i=0;i<4;i++) {{
            int y=8+i*2;
            txt(2,y,"?- ancestor(",7);
            pn(14,y,i+1,{col}+(i%3)+2);
            txt(2,y+4,", X).",7);
        }}
        txt(2,17,"%% Query results:",{col}+4);
        txt(2,18,"X = alice ;",{col}+(f%3));
        txt(2,19,"X = bob ;",{col}+((f+1)%3));
        txt(2,20,"X = carol ;",{col}+((f+2)%3));
        txt(2,21,"false.",8);
        txt(2,23,"%% Backtracking finds all solutions!",8);
        dl(60000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_prolog)

def t_cobol(n):
    msg = f"COBOL Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-20)/2,0,"{msg}",{col});
    for(int f=0;f<120;f++) {{
        clr(0);
        txt((COLS-20)/2,0,"{msg}",{col});
        txt(2,2,"       IDENTIFICATION DIVISION.",{col}+2);
        txt(2,3,"       PROGRAM-ID. HELLO.",{col}+2);
        txt(2,4,"       DATA DIVISION.",7);
        txt(2,5,"       WORKING-STORAGE SECTION.",7);
        txt(2,6,"       01 WS-COUNT PIC 9(3) VALUE 0.",7);
        txt(2,7,"       PROCEDURE DIVISION.",{col}+4);
        txt(2,8,"           PERFORM VARYING WS-COUNT",7);
        txt(2,9,"             FROM 1 BY 1 UNTIL WS-COUNT > 10",7);
        txt(2,10,"             DISPLAY 'COUNT: ' WS-COUNT",7);
        txt(2,11,"           END-PERFORM",7);
        txt(2,12,"           STOP RUN.",{col}+4);
        int n=f%10+1;
        txt(2,14,"COUNT: ",{col}+2);pn(8,14,n,{col}+4);
        txt(2,16,"01 WS-TABLE.",{col}+2);
        txt(2,17,"   05 WS-ENTRY OCCURS 5 TIMES PIC X(3).",7);
        for(int i=0;i<5;i++) {{
            int y=19+i;
            txt(2,y,"WS-ENTRY(",7);pn(11,y,i+1,{col});txt(2,y+6,")=",7);pn(15,y,(i+1)*(f%5+1),{col}+3);
        }}
        txt(2,24,"        (COBOL: still running the world)",8);
        dl(70000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_cobol)

def t_fortran(n):
    msg = f"Fortran Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-22)/2,0,"{msg}",{col});
    for(int f=0;f<120;f++) {{
        clr(0);
        txt((COLS-22)/2,0,"{msg}",{col});
        txt(2,2,"! Scientific computing since 1957",{col}+2);
        txt(2,4,"program main",7);
        txt(2,5,"  implicit none",7);
        txt(2,6,"  integer :: i, n = 10",7);
        txt(2,7,"  real :: matrix(3,3)",7);
        txt(2,8,"  matrix = reshape([1,2,3,4,5,6,7,8,9], [3,3])",7);
        for(int r=0;r<3;r++) {{
            for(int c=0;c<3;c++) {{
                int v=(r+c+1)*(f%4+1);
                pn(6+c*6,10+r,v,{col}+(r*3+c)%7+1);
            }}
        }}
        txt(2,14,"  do i = 1, n",{col}+4);
        txt(2,15,"    print *, 'i = ', i",7);
        txt(2,16,"  end do",{col}+4);
        int fn=1;for(int i=1;i<=f%8+1;i++)fn*=i;
        txt(2,18,"  ! Factorial:",8);
        pn(14,18,f%8+1,{col}+2);txt(2,18,"=",7);pn(17,18,fn,{col}+4);
        txt(2,20,"end program main",7);
        dl(60000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_fortran)

def t_julia(n):
    msg = f"Julia Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-20)/2,0,"{msg}",{col});
    for(int f=0;f<120;f++) {{
        clr(0);
        txt((COLS-20)/2,0,"{msg}",{col});
        txt(2,2,"# Multiple dispatch + speed of C",{col}+2);
        txt(2,4,"using LinearAlgebra",7);
        txt(2,5,"A = [1 2; 3 4]",7);
        txt(2,6,"b = [5, 6]",7);
        txt(2,7,"x = A \\\\ b  # solves linear system",7);
        for(int i=0;i<4;i++) {{
            int y=9+i;
            int v=i*(f%10+1);
            pn(4+i*6,y,v,{col}+(i%7));
        }}
        txt(2,14,"f(x) = x^2 + 2x - 1",{col}+2);
        txt(2,15,"@show f(10)",7);
        int r=100+f%200;
        txt(2,17,"f(10) = ",7);pn(9,17,r,{col}+4);
        txt(2,19,"using Plots",7);
        txt(2,20,"plot(A[:,1], A[:,2])",7);
        txt(2,22,"# Julia: walks like Python, runs like C",8);
        dl(60000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_julia)

def t_zig(n):
    msg = f"Zig Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-16)/2,0,"{msg}",{col});
    for(int f=0;f<120;f++) {{
        clr(0);
        txt((COLS-16)/2,0,"{msg}",{col});
        txt(2,2,"// comptime metaprogramming",{col}+2);
        txt(2,4,"const std = @import(\\"std\\");",7);
        txt(2,5,"fn max(comptime T: type, a: T, b: T) T {{",7);
        txt(2,6,"    return if (a > b) a else b;",7);
        txt(2,7,"}}",7);
        txt(2,9,"const result = max(u8, 42, 100);",{col}+4);
        txt(2,10,"// comptime eval at compile time",8);
        for(int i=0;i<6;i++) {{
            int y=12+i;
            int a=i*7,b=(i+1)*(f%5+2);
            pn(4,y,a,{col});txt(2,y+6,",",7);pn(9,y,b,{col}+2);
            txt(2,y+4,"max=",7);pn(5,y+13,a>b?a:b,{col}+4);
        }}
        txt(2,20,"pub fn main() void {{",{col}+2);
        txt(2,21,"    std.debug.print(\\"Hello, Zig!\\n\\", .{{}});",7);
        txt(2,22,"}}",{col}+2);
        dl(60000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_zig)

def t_apl(n):
    msg = f"APL Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-16)/2,0,"{msg}",{col});
    for(int f=0;f<120;f++) {{
        clr(0);
        txt((COLS-16)/2,0,"{msg}",{col});
        txt(2,2,"â One line = an entire program",{col}+2);
        txt(2,4,"âxââ·10         â numbers 1 to 10",7);
        txt(2,5,"âxÃ2             â doubled",7);
        txt(2,6,"â+/x              â sum of vector",7);
        txt(2,7,"ââ©3 3ââ¬9        â 3x3 matrix",7);
        txt(2,9,"ââ¬âÂ¨ x          â square root each",7);
        for(int i=0;i<8;i++) {{
            int y=11+i;
            int v=(i+1)*(f%6+1);
            txt(2,y,"ââ¬[",7);
            px(6,y,0xFE,{col}+(i%7));
            txt(2,y+2,"]",7);
            pn(9,y,v,{col}+2);
        }}
        txt(2,21,"â (â£Ã·Ã·)  â¨rÃ·r  âÂ² ",8);
        txt(2,22,"â (iota rho iota) â nonsense",8);
        txt(2,24,"â APL: write once, read never",8);
        dl(60000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_apl)

def t_erlang(n):
    msg = f"Erlang Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-20)/2,0,"{msg}",{col});
    for(int f=0;f<120;f++) {{
        clr(0);
        txt((COLS-20)/2,0,"{msg}",{col});
        txt(2,2,"%% Actor model concurrency",{col}+2);
        txt(2,4,"-module(hello).",7);
        txt(2,5,"-export([start/0, loop/0]).",7);
        txt(2,7,"start() ->",{col}+4);
        txt(2,8,"    Pid = spawn(fun loop/0),",7);
        txt(2,9,"    Pid ! {{hello, world}}.",7);
        txt(2,11,"loop() ->",{col}+4);
        txt(2,12,"    receive",7);
        txt(2,13,"        {{hello, Msg}} -> io:format(\\"~s~n\\", [Msg])",7);
        txt(2,14,"    end,",7);
        txt(2,15,"    loop().",{col}+4);
        for(int i=0;i<5;i++) {{
            int y=17+i;
            int n=i*(f%4+1);
            txt(2,y,"Pid ! {{data, ",7);pn(14,y,n,{col}+2);txt(2,y+4,"}}",7);
        }}
        txt(2,23,"%% \\"Let it crash\\" philosophy",8);
        dl(60000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_erlang)

def t_elixir(n):
    msg = f"Elixir Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-20)/2,0,"{msg}",{col});
    for(int f=0;f<120;f++) {{
        clr(0);
        txt((COLS-20)/2,0,"{msg}",{col});
        txt(2,2,"# Elixir: Erlang VM with Ruby syntax",{col}+2);
        txt(2,4,"defmodule Math do",7);
        txt(2,5,"  def fib(0), do: 0",7);
        txt(2,6,"  def fib(1), do: 1",7);
        txt(2,7,"  def fib(n), do: fib(n-1) + fib(n-2)",7);
        txt(2,8,"end",7);
        txt(2,10,"[1,2,3,4,5]",{col}+4);
        txt(2,11,"|> Enum.map(&(&1 * 2))",7);
        txt(2,12,"|> Enum.filter(&(&1 > 4))",7);
        txt(2,13,"|> IO.inspect()",7);
        for(int i=0;i<5;i++) {{
            int v=(i+1)*(f%6+1);
            pn(6+i*5,15,v,{col}+(i%7));
        }}
        txt(2,17,"case result do",7);
        txt(2,18,"  {{:ok, val}} -> IO.puts(val)",7);
        txt(2,19,"  {{:error, _}} -> IO.puts(\\"error\\")",7);
        txt(2,20,"end",7);
        txt(2,22,"# Pipe operator |> is magical",8);
        dl(60000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_elixir)

def t_clojure(n):
    msg = f"Clojure Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-22)/2,0,"{msg}",{col});
    for(int f=0;f<120;f++) {{
        clr(0);
        txt((COLS-22)/2,0,"{msg}",{col});
        txt(2,2,";; Lisp on the JVM",{col}+2);
        txt(2,4,"(ns demo.core)",7);
        txt(2,5,"(:require [clojure.string :as str])",7);
        txt(2,7,"(defn fib [n]",{col}+4);
        txt(2,8,"  (loop [a 0 b 1 n n]",7);
        txt(2,9,"    (if (zero? n) a",7);
        txt(2,10,"        (recur b (+ a b) (dec n)))))",7);
        for(int i=0;i<6;i++) {{
            int y=12+i;
            txt(2,y,"(->> (range 10)",{col}+(i%3)+1);
            int n=i*f%10;
            pn(8,y+1,n,{col}+4);
        }}
        txt(2,19,"(map #(* % 2) [1 2 3 4 5])",7);
        txt(2,21,"(filter even? (range 20))",7);
        txt(2,23,";; Persistent data structures",8);
        dl(60000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_clojure)

def t_vhdl(n):
    msg = f"VHDL Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-18)/2,0,"{msg}",{col});
    for(int f=0;f<120;f++) {{
        clr(0);
        txt((COLS-18)/2,0,"{msg}",{col});
        txt(2,2,"-- Hardware Description Language",{col}+2);
        txt(2,4,"entity counter is",7);
        txt(2,5,"  port (clk, rst : in std_logic;",7);
        txt(2,6,"        count : out std_logic_vector(7 downto 0));",7);
        txt(2,7,"end counter;",7);
        txt(2,9,"architecture arch of counter is",{col}+4);
        txt(2,10,"  signal tmp : unsigned(7 downto 0);",7);
        txt(2,11,"begin",7);
        txt(2,12,"  process(clk)",7);
        txt(2,13,"  begin",7);
        txt(2,14,"    if rising_edge(clk) then",7);
        txt(2,15,"      tmp <= tmp + 1;",7);
        txt(2,16,"    end if;",7);
        txt(2,17,"  end process;",7);
        txt(2,18,"  count <= std_logic_vector(tmp);",7);
        txt(2,19,"end arch;",{col}+4);
        int n=f%256;
        txt(2,21,"Count:",7);pn(8,21,n,{col}+4);
        txt(2,23,"-- Synthesizable VHDL",8);
        dl(60000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_vhdl)

def t_r(n):
    msg = f"R Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-10)/2,0,"{msg}",{col});
    for(int f=0;f<120;f++) {{
        clr(0);
        txt((COLS-10)/2,0,"{msg}",{col});
        txt(2,2,"# Statistical computing",{col}+2);
        txt(2,4,"data <- c(1, 4, 6, 8, 10, 15, 21)",7);
        txt(2,5,"mean(data)",7);
        txt(2,6,"sd(data)",7);
        txt(2,7,"summary(data)",7);
        for(int i=0;i<7;i++) {{
            int v=(i+1)*(f%6+1);
            pn(4+i*5,9,v,{col}+(i%7));
        }}
        int m=0;for(int i=0;i<7;i++)m+=(i+1)*(f%6+1);
        m/=7;
        txt(2,11,"mean = ",7);pn(8,11,m,{col}+4);
        txt(2,13,"lm(y ~ x, data=df)",{col}+2);
        txt(2,14,"t.test(group1, group2)",7);
        txt(2,16,"library(ggplot2)",7);
        txt(2,17,"ggplot(df, aes(x, y)) + geom_point()",7);
        for(int i=0;i<5;i++) {{
            int y=19+i;
            txt(2,y,"|",7);
            for(int d=0;d<(i+1)*(f%3+1);d++)px(3+d,y,0xDB,{col}+(d%6));
        }}
        txt(2,24,"# R: data science since '93",8);
        dl(60000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_r)

def t_ada(n):
    msg = f"Ada Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-14)/2,0,"{msg}",{col});
    for(int f=0;f<120;f++) {{
        clr(0);
        txt((COLS-14)/2,0,"{msg}",{col});
        txt(2,2,"-- Strongly typed, safe, and readable",{col}+2);
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
        txt(2,16,"Sum 1..",7);pn(9,16,f%10+1,{col}+2);txt(13+8,16,"=",7);pn(15+8,16,n,{col}+4);
        txt(2,18,"pragma Assert (Count > 0);",{col}+4);
        txt(2,20,"-- Ada: used in avionics & railways",8);
        for(int i=0;i<4;i++) {{
            int y=22+i;
            txt(2,y,"type Arr is array(1..",7);pn(8,y,i+2,{col});txt(2,y+6,") of Integer;",7);
        }}
        dl(60000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_ada)

def t_logo(n):
    msg = f"Logo Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-18)/2,0,"{msg}",{col});
    for(int f=0;f<150;f++) {{
        clr(0);
        txt((COLS-18)/2,0,"{msg}",{col});
        txt(2,2,"; Turtle graphics for children",{col}+2);
        txt(2,4,"TO SQUARE :SIZE",7);
        txt(2,5,"  REPEAT 4 [FD :SIZE RT 90]",7);
        txt(2,6,"END",7);
        txt(2,8,"TO SPIRAL :SIZE",{col}+4);
        txt(2,9,"  IF :SIZE > 100 [STOP]",7);
        txt(2,10,"  FD :SIZE RT 90",7);
        txt(2,11,"  SPIRAL :SIZE + 5",7);
        txt(2,12,"END",{col}+4);
        txt(2,14,"SPIRAL 10",7);
        int cx=35,cy=12;
        int x=cx,y=cy,size=5+f%4;
        for(int i=0;i<f%20+3;i++) {{
            px(x/2,y,0x2A,{col}+(i%7));
            x+=size;y+=(i%2?size:-size);
            px(x/2,y,0x2A,{col}+(i%7));
        }}
        txt(2,22,"; The first educational language",8);
        txt(2,23,"; Seymour Papert, MIT 1967",8);
        dl(50000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_logo)

def t_smalltalk(n):
    msg = f"Smalltalk Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-24)/2,0,"{msg}",{col});
    for(int f=0;f<120;f++) {{
        clr(0);
        txt((COLS-24)/2,0,"{msg}",{col});
        txt(2,2,"\\" Everything is an object \\"",{col}+2);
        txt(2,4,"| numbers |",7);
        txt(2,5,"numbers := OrderedCollection new.",7);
        txt(2,6,"numbers add: 42.",7);
        txt(2,7,"numbers add: 99.",7);
        txt(2,8,"numbers do: [ :n | Transcript show: n printString ]",7);
        for(int i=0;i<6;i++) {{
            int y=10+i;
            int v=(i+1)*(f%5+1);
            txt(2,y,"n := ",7);pn(6,y,v,{col}+2);
        }}
        txt(2,17,"3 timesRepeat: [ Transcript show: 'Hello' ]",{col}+4);
        txt(2,19,"a := 5 factorial.",7);
        txt(2,20,"b := #(1 2 3) collect: [ :e | e * 2 ].",7);
        txt(2,22,"\\" Smalltalk inspired OOP & MVC \\"",8);
        dl(60000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_smalltalk)

def t_assembly(n):
    msg = f"Assembly Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-24)/2,0,"{msg}",{col});
    for(int f=0;f<120;f++) {{
        clr(0);
        txt((COLS-24)/2,0,"{msg}",{col});
        txt(2,2,"; x86-64 assembly",{col}+2);
        txt(2,4,"section .text",7);
        txt(2,5,"global _start",7);
        txt(2,6,"_start:",7);
        txt(2,7,"    mov rax, 1        ; sys_write",7);
        txt(2,8,"    mov rdi, 1        ; stdout",7);
        txt(2,9,"    mov rsi, msg",7);
        txt(2,10,"    mov rdx, len",7);
        txt(2,11,"    syscall",7);
        txt(2,12,"    mov rax, 60       ; sys_exit",7);
        txt(2,13,"    xor rdi, rdi",7);
        txt(2,14,"    syscall",7);
        txt(2,16,"section .data",{col}+4);
        txt(2,17,"msg: db \\"Hello, World!\\", 10",7);
        int n=f%24;
        for(int i=0;i<n;i++)px(2+i,19,0xDB,{col}+(i%7));
        txt(2,21,"REGISTERS:",{col}+2);
        txt(2,22,"RAX=",7);pn(6,22,f%65536,{col}+4);
        txt(2,23,"RBX=",7);pn(6,23,f/256,{col}+3);
        dl(60000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_assembly)

def t_arch(n):
    msg = f"Arch Linux Demo (Stage {n})"
    col = 1 + (n % 15)
    return HEADER.format() + f'''
void stage{n}_entry(void) {{
    kf(); clr(0);
    txt((COLS-24)/2,0,"{msg}",{col});
    for(int f=0;f<150;f++) {{
        clr(0);
        txt((COLS-24)/2,0,"{msg}",{col});
        px(35,2,0x03,{col}+6);
        px(36,2,'r',{col}+6);
        px(37,2,'c',{col}+6);
        px(38,2,'h',{col}+6);
        txt(2,4,"$ sudo pacman -Syu",{col}+2);
        txt(2,5,":: Synchronizing package databases...",7);
        txt(2,6," core is up to date",7);
        txt(2,7," extra is up to date",7);
        txt(2,8," community is up to date",7);
        txt(2,9,":: Starting full system upgrade...",7);
        txt(2,10,":: Replace linux with linux-lts? [Y/n]",7);
        int n=f%20;
        for(int i=0;i<n;i++) {{
            if(i<5) {{txt(2,12+i,"[✓] package-",7);pn(5,12+i,i,{col});}}
        }}
        if(f<100) {{
            txt(2,14,"  downloading packages...",{col}+2);
            for(int i=0;i<f/5;i++)px(4+i,15,0xDB,{col}+2);
        }} else {{
            txt(2,14,"$ sudo pacman -S base-devel",{col}+2);
            txt(2,15,"  resolving dependencies...",7);
            txt(2,16,"  looking for conflicting packages...",7);
        }}
        txt(2,18,"$ yay -S opencode-git",{col}+4);
        txt(2,19,"  :: Proceed with installation? [Y/n]",8);
        txt(2,21,"$ neofetch",{col}+2);
        txt(2,22,"  OS: Arch Linux x86_64",{col}+f%7+1);
        txt(2,23,"  Kernel: 6.14.1-arch1-1",{col}+f%5+1);
        dl(50000);
        if(kh()){{kg();break;}}
    }}
    clr(0);txt((COLS-20)/2,12,"Press any key...",7);
    wa();
}}
'''

TEMPLATES.append(t_arch)

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
