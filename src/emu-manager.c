/*
 * emu-manager.c — panicos-emu on-device GUI (SDL2).
 *
 * Fullscreen, gamepad-driven emulator install/maintenance app (the "Update Emulators" entry):
 *   - Manage: per-system checkboxes showing installed/missing cores + rom counts; pick targets,
 *     Apply fetches the missing ones and regenerates EmulationStation.
 *   - Update (sync with ROCKNIX), Install ALL cores (parity), Uninstall (keep ROMs).
 * Streams the installer's output live. Self-contained: embedded 8x8 font, SDL2 only.
 */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include "font8x8_basic.h"

#define SCREEN_W 640
#define SCREEN_H 480
#define CLONE   "/storage/.panicos-emu"
#define INSTALL CLONE "/bin/panicos-emu-install.sh"
#define UNINST  CLONE "/bin/panicos-emu-uninstall.sh"
#define SELFILE "/storage/emulators/retroarch/.enabled-systems"

enum { BTN_B=0, BTN_A=1, BTN_Y=2, BTN_X=3, BTN_SELECT=8, BTN_START=9,
       BTN_UP=13, BTN_DOWN=14, BTN_LEFT=15, BTN_RIGHT=16 };
enum { ST_MENU, ST_MANAGE, ST_RUNNING };

static SDL_Renderer *REN;
static SDL_Texture  *FONT;

/* ---------- font ---------- */
static void font_init(void){
    SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,128,64,32,SDL_PIXELFORMAT_RGBA8888);
    SDL_memset(s->pixels,0,(size_t)s->h*s->pitch);
    Uint32*px=(Uint32*)s->pixels; int pitch=s->pitch/4;
    for(int c=0;c<128;c++){int gx=(c%16)*8,gy=(c/16)*8;
        for(int row=0;row<8;row++){unsigned char b=(unsigned char)font8x8_basic[c][row];
            for(int col=0;col<8;col++) if(b&(1<<col)) px[(gy+row)*pitch+(gx+col)]=0xFFFFFFFFu;}}
    FONT=SDL_CreateTextureFromSurface(REN,s); SDL_SetTextureBlendMode(FONT,SDL_BLENDMODE_BLEND); SDL_FreeSurface(s);
}
static void draw_char(int x,int y,int sc,char ch,Uint8 r,Uint8 g,Uint8 b){
    unsigned char c=(unsigned char)ch; if(c>=128)c='?';
    SDL_SetTextureColorMod(FONT,r,g,b);
    SDL_Rect src={(c%16)*8,(c/16)*8,8,8},dst={x,y,8*sc,8*sc}; SDL_RenderCopy(REN,FONT,&src,&dst);
}
static void draw_text(int x,int y,int sc,const char*s,Uint8 r,Uint8 g,Uint8 b){
    int cx=x; for(;*s;s++){ if(*s=='\n'){y+=8*sc;cx=x;continue;} draw_char(cx,y,sc,*s,r,g,b); cx+=8*sc; }
}
static void fill(int x,int y,int w,int h,Uint8 r,Uint8 g,Uint8 b){ SDL_SetRenderDrawColor(REN,r,g,b,255); SDL_Rect q={x,y,w,h}; SDL_RenderFillRect(REN,&q); }

/* ---------- mini terminal (with ANSI/CSI stripping) ---------- */
#define TCOLS 40
#define TROWS 26
static char term[TROWS][TCOLS+1];
static int trow,tcol,in_esc;
static void term_clear(void){ for(int i=0;i<TROWS;i++){memset(term[i],' ',TCOLS);term[i][TCOLS]=0;} trow=tcol=0; in_esc=0; }
static void term_nl(void){ trow++; tcol=0; if(trow>=TROWS){ for(int i=0;i<TROWS-1;i++)memcpy(term[i],term[i+1],TCOLS+1); memset(term[TROWS-1],' ',TCOLS); term[TROWS-1][TCOLS]=0; trow=TROWS-1; } }
static void term_putc(char ch){
    if(in_esc){ if((unsigned char)ch>='@'&&(unsigned char)ch<='~') in_esc=0; return; } /* swallow CSI body */
    if(ch==27){ in_esc=1; return; }                  /* ESC -> start of an escape seq */
    if(ch=='\r'){ tcol=0; return; }
    if(ch=='\n'){ term_nl(); return; }
    if(ch=='\t'){ tcol=(tcol+4)&~3; if(tcol>=TCOLS)term_nl(); return; }
    if((unsigned char)ch<32) return;
    if(tcol>=TCOLS) term_nl();
    term[trow][tcol++]=ch;
}

/* ---------- run a command, stream output ---------- */
static pid_t g_pid=-1; static int g_fd=-1,g_done=0,after_run=ST_MENU;
static void cmd_start(const char*cmd,int ret){
    int p[2]; if(pipe(p)<0) return;
    pid_t pid=fork();
    if(pid==0){ dup2(p[1],1); dup2(p[1],2); close(p[0]); close(p[1]); setsid();
        setenv("TERM","dumb",1); setenv("NO_COLOR","1",1);
        execl("/bin/sh","sh","-c",cmd,(char*)NULL); _exit(127); }
    close(p[1]); fcntl(p[0],F_SETFL,O_NONBLOCK);
    g_fd=p[0]; g_pid=pid; g_done=0; after_run=ret; term_clear();
}
static void cmd_poll(void){
    if(g_fd>=0){ char b[2048]; ssize_t n; while((n=read(g_fd,b,sizeof b))>0) for(ssize_t i=0;i<n;i++) term_putc(b[i]); if(n==0){close(g_fd);g_fd=-1;} }
    if(g_pid>0){ int st; if(waitpid(g_pid,&st,WNOHANG)==g_pid) g_pid=-1; }
    if(g_fd<0&&g_pid<0&&!g_done){ g_done=1; term_nl(); const char*m="--- finished. press A ---"; for(const char*q=m;*q;q++)term_putc(*q); }
}

/* ---------- manage (per-system inventory) ---------- */
typedef struct { char name[32],full[40]; int enabled,inst,total,roms; } sysrow_t;
static sysrow_t SYS[40]; static int NSYS=0,msel=0,mtop=0;
static void manage_load(void){
    NSYS=0; msel=0; mtop=0;
    FILE*f=popen(INSTALL " --status 2>/dev/null","r"); if(!f) return;
    char line[256];
    while(NSYS<40 && fgets(line,sizeof line,f)){
        char*fl[7]; int nf=0; fl[nf++]=line;
        for(char*p=line;*p&&nf<7;p++) if(*p=='|'){*p=0; fl[nf++]=p+1;}
        if(nf<6) continue;
        char*nlc=strchr(fl[nf-1],'\n'); if(nlc)*nlc=0;
        sysrow_t*s=&SYS[NSYS++];
        snprintf(s->name,sizeof s->name,"%s",fl[0]);
        snprintf(s->full,sizeof s->full,"%s",fl[1]);
        s->enabled=atoi(fl[2]); s->inst=atoi(fl[3]); s->total=atoi(fl[4]); s->roms=atoi(fl[5]);
    }
    pclose(f);
}
static void manage_apply(void){
    FILE*f=fopen(SELFILE,"w");
    if(f){ for(int i=0;i<NSYS;i++) if(SYS[i].enabled) fprintf(f,"%s\n",SYS[i].name); fclose(f); }
    cmd_start("cd " CLONE " && git pull --ff-only; bash bin/panicos-emu-install.sh", ST_MANAGE);
}

/* ---------- main menu ---------- */
typedef struct { const char*label,*cmd; } item_t;
static item_t MENU[]={
    {"Manage emulators",            NULL /*special*/},
    {"Update (sync with ROCKNIX)",  "cd " CLONE " && git pull --ff-only; bash bin/panicos-emu-install.sh"},
    {"Install ALL cores (parity)",  "cd " CLONE " && git pull --ff-only; bash bin/panicos-emu-install.sh --all-cores"},
    {"Uninstall (keep ROMs)",       "bash " UNINST " --keep-roms"},
    {"Quit",                        NULL},
};
#define NMENU ((int)(sizeof(MENU)/sizeof(MENU[0])))

int main(void){
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_JOYSTICK)!=0){ fprintf(stderr,"SDL_Init: %s\n",SDL_GetError()); return 1; }
    SDL_Window*win=SDL_CreateWindow("panicos-emu",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,SCREEN_W,SCREEN_H,SDL_WINDOW_FULLSCREEN_DESKTOP);
    REN=SDL_CreateRenderer(win,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC); if(!REN)REN=SDL_CreateRenderer(win,-1,0);
    SDL_RenderSetLogicalSize(REN,SCREEN_W,SCREEN_H); SDL_ShowCursor(SDL_DISABLE);
    font_init();
    SDL_Joystick*js=SDL_NumJoysticks()>0?SDL_JoystickOpen(0):NULL;

    int sel=0,state=ST_MENU,running=1;
    const int VIS=11;                 /* manage rows visible */
    while(running){
        SDL_Event e;
        while(SDL_PollEvent(&e)){
            int btn=-1; SDL_Keycode k=0;
            if(e.type==SDL_QUIT){ running=0; continue; }
            if(e.type==SDL_JOYDEVICEADDED&&!js){ js=SDL_JoystickOpen(0); continue; }
            if(e.type==SDL_JOYBUTTONDOWN) btn=e.jbutton.button;
            else if(e.type==SDL_KEYDOWN) k=e.key.keysym.sym;
            else if(e.type==SDL_JOYHATMOTION){ if(e.jhat.value&SDL_HAT_UP)k=SDLK_UP; else if(e.jhat.value&SDL_HAT_DOWN)k=SDLK_DOWN; }
            else continue;
            int up   = btn==BTN_UP   || k==SDLK_UP;
            int down = btn==BTN_DOWN || k==SDLK_DOWN;
            int acc  = btn==BTN_A    || k==SDLK_RETURN;
            int bk   = btn==BTN_B    || btn==BTN_SELECT || k==SDLK_ESCAPE;

            if(state==ST_MENU){
                if(up)   sel=(sel-1+NMENU)%NMENU;
                if(down) sel=(sel+1)%NMENU;
                if(bk)   running=0;
                if(acc){
                    if(sel==0){ manage_load(); state=ST_MANAGE; }
                    else if(!MENU[sel].cmd) running=0;        /* Quit */
                    else { cmd_start(MENU[sel].cmd,ST_MENU); state=ST_RUNNING; }
                }
            } else if(state==ST_MANAGE){
                if(up   && NSYS){ msel=(msel-1+NSYS)%NSYS; }
                if(down && NSYS){ msel=(msel+1)%NSYS; }
                if(msel<mtop) mtop=msel; if(msel>=mtop+VIS) mtop=msel-VIS+1;
                if(acc && NSYS) SYS[msel].enabled=!SYS[msel].enabled;     /* A toggle */
                if(btn==BTN_Y) for(int i=0;i<NSYS;i++) SYS[i].enabled=1;  /* Y enable all */
                if(btn==BTN_X) for(int i=0;i<NSYS;i++) if(SYS[i].inst<SYS[i].total) SYS[i].enabled=1; /* X enable missing */
                if(btn==BTN_START){ manage_apply(); state=ST_RUNNING; }   /* apply */
                if(bk){ state=ST_MENU; }                                  /* back, no apply */
            } else { /* RUNNING */
                if((acc||bk) && g_done){ state=after_run; if(state==ST_MANAGE) manage_load(); }
            }
        }
        if(state==ST_RUNNING) cmd_poll();

        SDL_SetRenderDrawColor(REN,12,12,20,255); SDL_RenderClear(REN);
        if(state==ST_MENU){
            draw_text(40,28,3,"panicos-emu",110,200,255);
            draw_text(44,72,1,"EMULATOR MANAGER",110,120,150);
            for(int i=0;i<NMENU;i++){ int y=124+i*44;
                if(i==sel) fill(28,y-8,SCREEN_W-56,38,36,78,140);
                draw_text(48,y,2,MENU[i].label,230,230,235); }
            draw_text(40,SCREEN_H-26,1,"D-PAD: MOVE   A: SELECT   B: QUIT",110,110,135);
        } else if(state==ST_MANAGE){
            draw_text(16,12,2,"Manage Emulators",110,200,255);
            for(int r=0;r<VIS && (mtop+r)<NSYS;r++){
                int i=mtop+r,y=52+r*34; sysrow_t*s=&SYS[i];
                if(i==msel) fill(10,y-5,SCREEN_W-20,32,36,78,140);
                Uint8 cr=200,cg=200,cb=205;
                if(s->inst==0){cr=210;cg=110;cb=110;} else if(s->inst<s->total){cr=220;cg=200;cb=120;} else {cr=150;cg=210;cb=160;}
                char row[80];
                snprintf(row,sizeof row,"%s %-18.18s %d/%d %5dr",s->enabled?"[x]":"[ ]",s->full,s->inst,s->total,s->roms);
                draw_text(16,y,2,row,cr,cg,cb);
            }
            draw_text(12,SCREEN_H-44,1,"A:TOGGLE  Y:ALL  X:MISSING  START:APPLY  B:BACK",110,110,135);
            draw_text(12,SCREEN_H-26,1,"green=installed  yellow=partial  red=missing",90,90,110);
        } else { /* RUNNING */
            draw_text(12,8,2,g_done?"DONE":"WORKING...",110,200,255);
            for(int r=0;r<TROWS;r++) draw_text(12,40+r*16,2,term[r],170,175,185);
            draw_text(12,SCREEN_H-20,1,g_done?"A: BACK":"PLEASE WAIT...",110,110,135);
        }
        SDL_RenderPresent(REN); SDL_Delay(16);
    }
    if(js)SDL_JoystickClose(js);
    SDL_DestroyTexture(FONT); SDL_DestroyRenderer(REN); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
