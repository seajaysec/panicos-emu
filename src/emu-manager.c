/*
 * emu-manager.c — panicos-emu on-device GUI.
 *
 * A small fullscreen SDL2 app (the "Update Emulators" entry): a gamepad-driven menu that
 * runs the installer/uninstaller scripts and streams their output live. Self-contained —
 * embedded 8x8 bitmap font (no SDL_ttf, no font files), SDL2 only (present on PanicOS).
 *
 * Build (aarch64): see scripts/build-emu-manager.sh
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

/* H700 Gamepad button indices (from ROCKNIX "H700 Gamepad.cfg") */
enum { BTN_B=0, BTN_A=1, BTN_Y=2, BTN_X=3, BTN_SELECT=8, BTN_START=9,
       BTN_UP=13, BTN_DOWN=14, BTN_LEFT=15, BTN_RIGHT=16 };

static SDL_Renderer *REN;
static SDL_Texture  *FONT;          /* 16x8 glyph atlas, 128x64 px */

static void font_init(void){
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0,128,64,32,SDL_PIXELFORMAT_RGBA8888);
    SDL_memset(s->pixels,0,(size_t)s->h*s->pitch);
    Uint32 *px=(Uint32*)s->pixels; int pitch=s->pitch/4;
    for(int c=0;c<128;c++){
        int gx=(c%16)*8, gy=(c/16)*8;
        for(int row=0;row<8;row++){
            unsigned char bits=(unsigned char)font8x8_basic[c][row];
            for(int col=0;col<8;col++)
                if(bits&(1<<col)) px[(gy+row)*pitch+(gx+col)]=0xFFFFFFFFu;
        }
    }
    FONT=SDL_CreateTextureFromSurface(REN,s);
    SDL_SetTextureBlendMode(FONT,SDL_BLENDMODE_BLEND);
    SDL_FreeSurface(s);
}
static void draw_char(int x,int y,int sc,char ch,Uint8 r,Uint8 g,Uint8 b){
    unsigned char c=(unsigned char)ch; if(c>=128) c='?';
    SDL_SetTextureColorMod(FONT,r,g,b);
    SDL_Rect src={(c%16)*8,(c/16)*8,8,8}, dst={x,y,8*sc,8*sc};
    SDL_RenderCopy(REN,FONT,&src,&dst);
}
static void draw_text(int x,int y,int sc,const char*s,Uint8 r,Uint8 g,Uint8 b){
    int cx=x;
    for(;*s;s++){ if(*s=='\n'){y+=8*sc;cx=x;continue;} draw_char(cx,y,sc,*s,r,g,b); cx+=8*sc; }
}

/* ---- mini terminal grid for streamed command output ---- */
#define TCOLS 40
#define TROWS 26
static char term[TROWS][TCOLS+1];
static int  trow, tcol;
static void term_clear(void){ for(int i=0;i<TROWS;i++){memset(term[i],' ',TCOLS);term[i][TCOLS]=0;} trow=tcol=0; }
static void term_nl(void){ trow++; tcol=0; if(trow>=TROWS){ for(int i=0;i<TROWS-1;i++)memcpy(term[i],term[i+1],TCOLS+1); memset(term[TROWS-1],' ',TCOLS); term[TROWS-1][TCOLS]=0; trow=TROWS-1; } }
static void term_putc(char ch){
    if(ch=='\r'){ tcol=0; return; }
    if(ch=='\n'){ term_nl(); return; }
    if(ch=='\t'){ tcol=(tcol+4)&~3; if(tcol>=TCOLS) term_nl(); return; }
    if((unsigned char)ch<32) return;
    if(tcol>=TCOLS) term_nl();
    term[trow][tcol++]=ch;
}

/* ---- run a command and stream its output ---- */
static pid_t g_pid=-1; static int g_fd=-1, g_done=0;
static void cmd_start(const char*cmd){
    int p[2]; if(pipe(p)<0) return;
    pid_t pid=fork();
    if(pid==0){
        dup2(p[1],STDOUT_FILENO); dup2(p[1],STDERR_FILENO);
        close(p[0]); close(p[1]); setsid();
        setenv("TERM","dumb",1);
        execl("/bin/sh","sh","-c",cmd,(char*)NULL);
        _exit(127);
    }
    close(p[1]); fcntl(p[0],F_SETFL,O_NONBLOCK);
    g_fd=p[0]; g_pid=pid; g_done=0; term_clear();
}
static void cmd_poll(void){
    if(g_fd>=0){
        char buf[2048]; ssize_t n;
        while((n=read(g_fd,buf,sizeof buf))>0) for(ssize_t i=0;i<n;i++) term_putc(buf[i]);
        if(n==0){ close(g_fd); g_fd=-1; }
    }
    if(g_pid>0){ int st; if(waitpid(g_pid,&st,WNOHANG)==g_pid) g_pid=-1; }
    if(g_fd<0 && g_pid<0 && !g_done){ g_done=1; term_nl(); const char*m="--- finished. press A ---"; for(const char*q=m;*q;q++)term_putc(*q); }
}

typedef struct { const char*label; const char*cmd; } item_t;
static item_t MENU[]={
    {"Update emulators",            "cd /storage/.panicos-emu && git pull --ff-only; bash bin/panicos-emu-install.sh"},
    {"Install ALL cores (parity)",  "cd /storage/.panicos-emu && git pull --ff-only; bash bin/panicos-emu-install.sh --all-cores"},
    {"Re-render config (no net)",   "bash /storage/.panicos-emu/bin/panicos-emu-install.sh --render-only"},
    {"Uninstall (keep ROMs)",       "bash /storage/.panicos-emu/bin/panicos-emu-uninstall.sh --keep-roms"},
    {"Quit",                        NULL},
};
#define NMENU ((int)(sizeof(MENU)/sizeof(MENU[0])))

int main(void){
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_JOYSTICK)!=0){ fprintf(stderr,"SDL_Init: %s\n",SDL_GetError()); return 1; }
    SDL_Window *win=SDL_CreateWindow("panicos-emu",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,
                                     SCREEN_W,SCREEN_H,SDL_WINDOW_FULLSCREEN_DESKTOP);
    REN=SDL_CreateRenderer(win,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    if(!REN) REN=SDL_CreateRenderer(win,-1,0);
    SDL_RenderSetLogicalSize(REN,SCREEN_W,SCREEN_H);
    SDL_ShowCursor(SDL_DISABLE);
    font_init();
    SDL_Joystick *js = SDL_NumJoysticks()>0 ? SDL_JoystickOpen(0) : NULL;

    int sel=0, state=0 /*0=menu 1=running*/, running=1;
    while(running){
        SDL_Event e;
        while(SDL_PollEvent(&e)){
            if(e.type==SDL_QUIT){ running=0; }
            else if(e.type==SDL_JOYDEVICEADDED && !js){ js=SDL_JoystickOpen(0); }
            else if(state==0){
                int up=0,down=0,go=0;
                if(e.type==SDL_JOYBUTTONDOWN){
                    int btn=e.jbutton.button;
                    if(btn==BTN_UP)up=1; else if(btn==BTN_DOWN)down=1;
                    else if(btn==BTN_A||btn==BTN_START)go=1;
                    else if(btn==BTN_B||btn==BTN_SELECT)running=0;
                } else if(e.type==SDL_JOYHATMOTION){
                    if(e.jhat.value&SDL_HAT_UP)up=1; if(e.jhat.value&SDL_HAT_DOWN)down=1;
                } else if(e.type==SDL_KEYDOWN){
                    SDL_Keycode k=e.key.keysym.sym;
                    if(k==SDLK_UP)up=1; else if(k==SDLK_DOWN)down=1;
                    else if(k==SDLK_RETURN)go=1; else if(k==SDLK_ESCAPE)running=0;
                }
                if(up)   sel=(sel-1+NMENU)%NMENU;
                if(down) sel=(sel+1)%NMENU;
                if(go){ if(!MENU[sel].cmd) running=0; else { cmd_start(MENU[sel].cmd); state=1; } }
            } else { /* running: only accept "back" once finished */
                int back=(e.type==SDL_JOYBUTTONDOWN && (e.jbutton.button==BTN_A||e.jbutton.button==BTN_START||e.jbutton.button==BTN_B))
                       ||(e.type==SDL_KEYDOWN && (e.key.keysym.sym==SDLK_RETURN||e.key.keysym.sym==SDLK_ESCAPE));
                if(back && g_done){ state=0; }
            }
        }
        if(state==1) cmd_poll();

        SDL_SetRenderDrawColor(REN,12,12,20,255); SDL_RenderClear(REN);
        if(state==0){
            draw_text(40,28,3,"panicos-emu",110,200,255);
            draw_text(44,72,1,"EMULATOR MANAGER",110,120,150);
            for(int i=0;i<NMENU;i++){
                int y=128+i*44;
                if(i==sel){ SDL_SetRenderDrawColor(REN,36,78,140,255); SDL_Rect hr={28,y-8,SCREEN_W-56,38}; SDL_RenderFillRect(REN,&hr); }
                draw_text(48,y,2,MENU[i].label, 230,230,235);
            }
            draw_text(40,SCREEN_H-28,1,"D-PAD: MOVE   A: SELECT   B/SELECT: QUIT",110,110,135);
        } else {
            draw_text(12,8,2,g_done?"DONE":"WORKING...",110,200,255);
            for(int r=0;r<TROWS;r++) draw_text(12,40+r*16,2,term[r],170,175,185);
            draw_text(12,SCREEN_H-20,1,g_done?"A: BACK TO MENU":"PLEASE WAIT...",110,110,135);
        }
        SDL_RenderPresent(REN);
        SDL_Delay(16);
    }
    if(js) SDL_JoystickClose(js);
    SDL_DestroyTexture(FONT); SDL_DestroyRenderer(REN); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
