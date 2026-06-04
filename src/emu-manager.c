/*
 * emu-manager.c — panicos-emu on-device GUI (SDL2).
 *
 * Fullscreen, gamepad-driven emulator install/maintenance app. A small screen
 * state machine over the proven shell engine (panicos-emu-install.sh):
 *
 *   Home      — status summary + tiles (Quick Setup / Library / Update / Settings / Help)
 *   Library   — per-system rows with honest ON/UPD/GET badges, filter tabs, scroll
 *   Sheet     — context action sheet for the selected system (Install/Remove/Change core/...)
 *   Confirm   — full-screen yes/no for destructive/expensive actions (Remove, Reinstall)
 *   QSetup    — Quick Setup summary + Install / Customize / Cancel
 *   Progress  — streamed mini-terminal + download bar + done footer
 *   Settings  — Install ALL cores / BIOS path / Re-render
 *   Help      — ROM paths, controls, repo URL
 *
 * State is read from the engine's `--status` / `--check-update`; actions shell out
 * to engine verbs and stream their output live. Self-contained: embedded 8x8 font,
 * SDL2 only.
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
#define CORES_DIR "/storage/emulators/retroarch/cores"
#define REPO_URL  "https://github.com/seajaysec/panicos-emu"
/* Verbs that sync the repo first, then run the engine. */
#define SYNC "cd " CLONE " && git pull --ff-only; bash bin/panicos-emu-install.sh "

enum { BTN_B=0, BTN_A=1, BTN_Y=2, BTN_X=3, BTN_SELECT=8, BTN_START=9,
       BTN_UP=13, BTN_DOWN=14, BTN_LEFT=15, BTN_RIGHT=16 };
enum { ST_HOME, ST_LIBRARY, ST_SHEET, ST_CONFIRM, ST_QSETUP, ST_PROGRESS, ST_SETTINGS, ST_HELP };

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
static void draw_text_c(int cx,int y,int sc,const char*s,Uint8 r,Uint8 g,Uint8 b){
    int w=(int)strlen(s)*8*sc; draw_text(cx-w/2,y,sc,s,r,g,b);
}
static void fill(int x,int y,int w,int h,Uint8 r,Uint8 g,Uint8 b){ SDL_SetRenderDrawColor(REN,r,g,b,255); SDL_Rect q={x,y,w,h}; SDL_RenderFillRect(REN,&q); }

/* ---------- mini terminal (with ANSI/CSI stripping) ---------- */
#define TCOLS 50
#define TROWS 22
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
static pid_t g_pid=-1; static int g_fd=-1,g_done=0,after_run=ST_HOME;
/* progress bar pulled out of the engine's "<cur> / <total>" download line */
static long g_cur=0,g_tot=0;
static void scan_progress(void){
    /* scan the visible terminal for a "<num> / <num>" pair (latest wins) */
    for(int r=TROWS-1;r>=0;r--){
        char*p=strstr(term[r]," / ");
        if(!p) continue;
        long a=0,b=0; char*s=p;
        while(s>term[r]&&(s[-1]>='0'&&s[-1]<='9')) s--;
        if(s==p) continue;
        a=strtol(s,NULL,10);
        b=strtol(p+3,NULL,10);
        if(b>0){ g_cur=a; g_tot=b; return; }
    }
}
static void cmd_start(const char*cmd,int ret){
    int p[2]; if(pipe(p)<0) return;
    pid_t pid=fork();
    if(pid==0){ dup2(p[1],1); dup2(p[1],2); close(p[0]); close(p[1]); setsid();
        setenv("TERM","dumb",1); setenv("NO_COLOR","1",1);
        execl("/bin/sh","sh","-c",cmd,(char*)NULL); _exit(127); }
    close(p[1]); fcntl(p[0],F_SETFL,O_NONBLOCK);
    g_fd=p[0]; g_pid=pid; g_done=0; after_run=ret; g_cur=g_tot=0; term_clear();
}
static void cmd_poll(void){
    if(g_fd>=0){ char b[2048]; ssize_t n; while((n=read(g_fd,b,sizeof b))>0) for(ssize_t i=0;i<n;i++) term_putc(b[i]); if(n==0){close(g_fd);g_fd=-1;} }
    if(g_pid>0){ int st; if(waitpid(g_pid,&st,WNOHANG)==g_pid) g_pid=-1; }
    scan_progress();
    if(g_fd<0&&g_pid<0&&!g_done){ g_done=1; }
}

/* ---------- system inventory (from --status) ---------- */
typedef struct {
    char name[32], full[40], def[24];
    int enabled, inst, total, roms;
    int update;           /* enabled && a suite update is available */
} sysrow_t;
static sysrow_t SYS[40]; static int NSYS=0;
static int g_update=0;    /* suite-wide: does --check-update say update:? */

/* GET=0, ON=1, UPD=2 */
static int row_state(const sysrow_t*s){
    if(s->enabled && s->inst>0){ return s->update ? 2 : 1; }
    return 0;
}

/* one-shot suite update check */
static void check_update(void){
    g_update=0;
    FILE*f=popen(INSTALL " --check-update 2>/dev/null","r"); if(!f) return;
    char line[128];
    if(fgets(line,sizeof line,f)) g_update=(strncmp(line,"update:",7)==0);
    pclose(f);
}

static void status_load(void){
    NSYS=0;
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
        s->def[0]=0; if(nf>=7) snprintf(s->def,sizeof s->def,"%s",fl[6]);
    }
    pclose(f);
}
/* full reload: status + suite update flag, applied per-row */
static void inventory_reload(void){
    check_update();
    status_load();
    for(int i=0;i<NSYS;i++) SYS[i].update = (g_update && SYS[i].enabled && SYS[i].inst>0);
}

/* ---------- installed-core discovery (for Change core sublist) ---------- *
 * The engine's systems.conf lists candidate cores per system (space-separated,
 * first is default). We read it and keep the ones whose <core>_libretro.so exists. */
static char CORE_LIST[12][24]; static int NCORES=0;
static void cores_for(const char*sysname){
    NCORES=0;
    FILE*f=fopen(CLONE "/systems.conf","r"); if(!f) return;
    char line[512];
    while(fgets(line,sizeof line,f)){
        if(line[0]=='#'||line[0]=='\n') continue;
        /* fields: name | full | cores | exts | platform */
        char*fl[5]; int nf=0; fl[nf++]=line;
        for(char*p=line;*p&&nf<5;p++) if(*p=='|'){*p=0; fl[nf++]=p+1;}
        if(nf<3) continue;
        char nm[32]; snprintf(nm,sizeof nm,"%s",fl[0]);
        /* trim spaces around name */
        char*a=nm; while(*a==' ')a++; char*z=a+strlen(a); while(z>a&&(z[-1]==' '||z[-1]=='\t'))*--z=0;
        if(strcmp(a,sysname)!=0) continue;
        /* tokenize core list */
        char*tok=strtok(fl[2]," \t");
        while(tok && NCORES<12){
            char path[256]; snprintf(path,sizeof path,"%s/%s_libretro.so",CORES_DIR,tok);
            if(access(path,F_OK)==0) snprintf(CORE_LIST[NCORES++],24,"%s",tok);
            tok=strtok(NULL," \t");
        }
        break;
    }
    fclose(f);
}

/* ---------- screen state ---------- */
static int screen=ST_HOME;
static int hsel=0;                 /* Home tile cursor */
static int lib_filter=0;           /* 0 All 1 Installed 2 Get 3 Updates */
static int lib_top=0, lib_sel=0;   /* Library scroll/cursor (index into NSYS) */
static int cur_sys=0;              /* system selected for the action sheet */

/* action sheet */
#define MAXSHEET 6
static char SHEET[MAXSHEET][16]; static int NSHEET=0, ssel=0;
static int sheet_change_core=0;    /* 1 = currently showing the Change-core sublist */
static int csel=0;                 /* cursor in core sublist */

/* confirm */
static char cf_title[64], cf_body[160], cf_cmd[256];

/* settings */
static int set_sel=0;

/* ---------- Home ---------- */
static const char *HOME[]={ "Quick Setup", "Library", "Update", "Settings", "Help / ROMs" };
#define NHOME 5

static void home_summary(int*systems,int*games){
    int sy=0,gm=0;
    for(int i=0;i<NSYS;i++){ if(SYS[i].enabled&&SYS[i].inst>0) sy++; gm+=SYS[i].roms; }
    *systems=sy; *games=gm;
}

/* ---------- Library filter ---------- */
static const char *FILTERS[]={ "All", "Installed", "Get", "Updates" };
static int lib_visible(int i){
    int st=row_state(&SYS[i]);
    switch(lib_filter){
        case 1: return st==1||st==2;     /* Installed (ON or UPD) */
        case 2: return st==0;            /* Get */
        case 3: return st==2;            /* Updates */
        default: return 1;               /* All */
    }
}
static int lib_count(void){ int n=0; for(int i=0;i<NSYS;i++) if(lib_visible(i)) n++; return n; }
/* map a visible-row index -> SYS index; -1 if none */
static int lib_index(int vis){ int n=0; for(int i=0;i<NSYS;i++){ if(lib_visible(i)){ if(n==vis) return i; n++; } } return -1; }
static void lib_clamp(void){
    int n=lib_count();
    if(n==0){ lib_sel=0; lib_top=0; return; }
    if(lib_sel<0) lib_sel=0; if(lib_sel>=n) lib_sel=n-1;
    const int VIS=9;
    if(lib_sel<lib_top) lib_top=lib_sel;
    if(lib_sel>=lib_top+VIS) lib_top=lib_sel-VIS+1;
    if(lib_top<0) lib_top=0;
}

/* ---------- action sheet builder ---------- */
static void sheet_build(int sysidx){
    cur_sys=sysidx; NSHEET=0; ssel=0; sheet_change_core=0;
    int st=row_state(&SYS[sysidx]);
    if(st==0){ /* GET */
        snprintf(SHEET[NSHEET++],16,"Install");
        snprintf(SHEET[NSHEET++],16,"Change core");
        snprintf(SHEET[NSHEET++],16,"Cancel");
    } else if(st==2){ /* UPD */
        snprintf(SHEET[NSHEET++],16,"Update");
        snprintf(SHEET[NSHEET++],16,"Change core");
        snprintf(SHEET[NSHEET++],16,"Remove");
        snprintf(SHEET[NSHEET++],16,"Cancel");
    } else { /* ON */
        snprintf(SHEET[NSHEET++],16,"Change core");
        snprintf(SHEET[NSHEET++],16,"Remove");
        snprintf(SHEET[NSHEET++],16,"Reinstall");
        snprintf(SHEET[NSHEET++],16,"Cancel");
    }
}

/* ---------- enter confirmations ---------- */
static void confirm_remove(void){
    sysrow_t*s=&SYS[cur_sys];
    snprintf(cf_title,sizeof cf_title,"Remove %s?",s->full);
    snprintf(cf_body,sizeof cf_body,"Removes the core. Keeps your %d ROMs.",s->roms);
    snprintf(cf_cmd,sizeof cf_cmd,SYNC "--remove %s",s->name);
    screen=ST_CONFIRM;
}
static void confirm_reinstall(void){
    sysrow_t*s=&SYS[cur_sys];
    snprintf(cf_title,sizeof cf_title,"Reinstall %s?",s->full);
    snprintf(cf_body,sizeof cf_body,"Already installed & up to date. Re-download anyway?");
    snprintf(cf_cmd,sizeof cf_cmd,SYNC "--install %s --force",s->name);
    screen=ST_CONFIRM;
}

/* ---------- act on a sheet selection ---------- */
static void sheet_act(void){
    sysrow_t*s=&SYS[cur_sys];
    const char*a=SHEET[ssel];
    char cmd[256];
    if(!strcmp(a,"Cancel")){ screen=ST_LIBRARY; return; }
    if(!strcmp(a,"Change core")){
        cores_for(s->name);
        sheet_change_core=1; csel=0; return;
    }
    if(!strcmp(a,"Install")){
        snprintf(cmd,sizeof cmd,SYNC "--install %s",s->name);
        cmd_start(cmd,ST_LIBRARY); screen=ST_PROGRESS; return;
    }
    if(!strcmp(a,"Update")){
        cmd_start(SYNC "--update",ST_LIBRARY); screen=ST_PROGRESS; return;
    }
    if(!strcmp(a,"Remove")){ confirm_remove(); return; }
    if(!strcmp(a,"Reinstall")){ confirm_reinstall(); return; }
}
static void core_act(void){
    if(NCORES==0){ sheet_change_core=0; return; }
    char cmd[256];
    snprintf(cmd,sizeof cmd,SYNC "--set-core %s %s",SYS[cur_sys].name,CORE_LIST[csel]);
    cmd_start(cmd,ST_LIBRARY); screen=ST_PROGRESS;
}

/* ===================================================================== */
/*  RENDERING                                                            */
/* ===================================================================== */

static void draw_badge(int x,int y,int st){
    Uint8 r,g,b; const char*t;
    if(st==1){ t="ON"; r=70;g=180;b=90; }
    else if(st==2){ t="UPD"; r=210;g=180;b=60; }
    else { t="GET"; r=90;g=90;b=105; }
    int w=(int)strlen(t)*16+10;
    fill(x,y-2,w,22,r,g,b);
    draw_text(x+5,y+1,2,t,20,20,28);
}

static void render_home(void){
    int sy,gm; home_summary(&sy,&gm);
    draw_text(28,24,3,"panicos-emu",110,200,255);
    char sub[80]; snprintf(sub,sizeof sub,"%d systems  %d games",sy,gm);
    draw_text(32,66,2,sub,170,175,190);
    if(g_update) draw_text(32,90,2,"update available",230,200,90);

    for(int i=0;i<NHOME;i++){ int y=132+i*56;
        if(i==hsel) fill(24,y-8,SCREEN_W-48,46,36,78,140);
        draw_text(44,y,2,HOME[i],230,230,235);
        if(i==2 && g_update) draw_text(44+(int)strlen(HOME[i])*16+24,y,2,"*",230,200,90);
    }
    draw_text(28,SCREEN_H-24,1,"D-PAD: MOVE   A: SELECT   B: QUIT",110,110,135);
}

static void render_library(void){
    draw_text(14,10,2,"Library",110,200,255);
    /* filter tabs */
    int tx=14, ty=40;
    for(int i=0;i<4;i++){
        int w=(int)strlen(FILTERS[i])*8+12;
        if(i==lib_filter) fill(tx,ty-2,w,18,36,78,140);
        draw_text(tx+6,ty,1,FILTERS[i],i==lib_filter?235:140,i==lib_filter?235:140,150);
        tx+=w+6;
    }
    int n=lib_count();
    if(n==0){ draw_text(20,120,2,"No systems in this filter.",150,150,160); }
    const int VIS=9;
    for(int r=0;r<VIS;r++){
        int vis=lib_top+r; if(vis>=n) break;
        int i=lib_index(vis); if(i<0) break;
        sysrow_t*s=&SYS[i]; int st=row_state(s);
        int y=70+r*40;
        if(vis==lib_sel) fill(8,y-6,SCREEN_W-16,38,30,40,70);
        draw_badge(14,y,st);
        draw_text(74,y,2,s->full,225,225,232);
        /* second line: core or hint, + game count */
        char info[80];
        if(st==0){
            if(s->roms>0) snprintf(info,sizeof info,"not installed - you have %d games",s->roms);
            else          snprintf(info,sizeof info,"not installed");
        } else {
            snprintf(info,sizeof info,"core: %s   %d games",s->def[0]?s->def:"-",s->roms);
        }
        draw_text(74,y+18,1,info,150,155,170);
    }
    draw_text(12,SCREEN_H-24,1,"D-PAD: MOVE  A: ACTIONS  X: FILTER  B: BACK",110,110,135);
}

static void render_sheet(void){
    /* dim background by re-rendering library first, then overlay panel */
    render_library();
    int pw=360, ph=NSHEET*40+70, px=(SCREEN_W-pw)/2, py=(SCREEN_H-ph)/2;
    sysrow_t*s=&SYS[cur_sys];
    if(!sheet_change_core){
        fill(px,py,pw,ph,22,26,40);
        fill(px,py,pw,4,80,140,200);
        draw_text_c(SCREEN_W/2,py+14,2,s->full,110,200,255);
        for(int i=0;i<NSHEET;i++){ int y=py+50+i*40;
            int danger=(!strcmp(SHEET[i],"Remove"));
            if(i==ssel) fill(px+12,y-6,pw-24,34,36,78,140);
            draw_text(px+28,y,2,SHEET[i],danger?230:230,danger?120:230,danger?120:235);
        }
        draw_text_c(SCREEN_W/2,py+ph-18,1,"A: SELECT   B: BACK",150,150,165);
    } else {
        /* Change-core sublist */
        int cph=(NCORES>0?NCORES:1)*32+74;
        int cpx=(SCREEN_W-pw)/2, cpy=(SCREEN_H-cph)/2;
        fill(cpx,cpy,pw,cph,22,26,40);
        fill(cpx,cpy,pw,4,80,140,200);
        draw_text_c(SCREEN_W/2,cpy+14,2,"Change core",110,200,255);
        if(NCORES==0){
            draw_text_c(SCREEN_W/2,cpy+50,1,"No installed cores for this system.",200,160,160);
        } else for(int i=0;i<NCORES;i++){ int y=cpy+48+i*32;
            if(i==csel) fill(cpx+12,y-5,pw-24,28,36,78,140);
            int isdef=(strcmp(CORE_LIST[i],s->def)==0);
            char lbl[40]; snprintf(lbl,sizeof lbl,"%s%s",CORE_LIST[i],isdef?"  (current)":"");
            draw_text(cpx+28,y,2,lbl,225,225,232);
        }
        draw_text_c(SCREEN_W/2,cpy+cph-18,1,"A: SET   B: BACK",150,150,165);
    }
}

static void render_confirm(void){
    fill(40,150,SCREEN_W-80,180,22,26,40);
    fill(40,150,SCREEN_W-80,4,200,120,80);
    draw_text_c(SCREEN_W/2,180,2,cf_title,235,200,150);
    /* wrap body at ~46 chars */
    char body[160]; snprintf(body,sizeof body,"%s",cf_body);
    int x=70,y=224,maxx=SCREEN_W-70;
    char word[48]; int wl=0;
    for(char*p=body;;p++){
        if(*p==' '||*p==0){
            word[wl]=0;
            int ww=(wl)*8;
            if(x+ww>maxx){ x=70; y+=18; }
            draw_text(x,y,1,word,210,210,220); x+=ww+8; wl=0;
            if(*p==0) break;
        } else if(wl<47){ word[wl++]=*p; }
    }
    draw_text_c(SCREEN_W/2,300,2,"[A] Yes    [B] No",235,235,240);
}

static const char *QS_CHIPS=
    "Game Boy/Color  NES  SNES  GBA  Genesis  Master System\n"
    "Game Gear  ColecoVision  Neo Geo Pocket  PC Engine\n"
    "WonderSwan  N64 (some games heavy)";
static void render_qsetup(void){
    draw_text(28,22,3,"Quick Setup",110,200,255);
    draw_text(30,72,1,"Installs the recommended emulator set:",180,185,200);
    draw_text(36,96,1,QS_CHIPS,200,205,215);
    draw_text(30,160,1,"Nintendo DS is excluded (too slow on this handheld).",170,150,150);
    draw_text(30,182,1,"~1.5 GB download - a few minutes - needs Wi-Fi.",200,200,150);

    static const char*acts[]={ "Install", "Customize in Library", "Cancel" };
    for(int i=0;i<3;i++){ int y=240+i*52;
        if(i==hsel) fill(24,y-8,SCREEN_W-48,42,36,78,140);
        draw_text(44,y,2,acts[i],230,230,235);
    }
    draw_text(28,SCREEN_H-24,1,"A: SELECT   B: BACK",110,110,135);
}

static void render_progress(void){
    draw_text(12,8,2,g_done?"DONE":"Working...",110,200,255);
    draw_text(12,30,1,g_done?"":"Don't power off the device.",210,180,120);
    int ty=52;
    /* download bar when the engine emits a byte pair */
    if(g_tot>0){
        int bx=12,bw=SCREEN_W-24,bh=16;
        fill(bx,ty,bw,bh,40,44,60);
        double frac=(double)g_cur/(double)g_tot; if(frac>1)frac=1; if(frac<0)frac=0;
        fill(bx,ty,(int)(bw*frac),bh,80,170,110);
        char pct[24]; snprintf(pct,sizeof pct,"%d%%",(int)(frac*100));
        draw_text(bx+bw/2-12,ty,1,pct,235,235,240);
        ty+=24;
    }
    int rows=(SCREEN_H-ty-30)/14; if(rows>TROWS) rows=TROWS;
    for(int r=0;r<rows;r++) draw_text(12,ty+r*14,1,term[r],170,175,185);
    if(g_done) draw_text(12,SCREEN_H-22,1,"Restart EmulationStation to see your systems. A: back",120,160,120);
    else       draw_text(12,SCREEN_H-22,1,"Please wait...",110,110,135);
}

static const char *SET_ITEMS[]={ "Install ALL cores (parity)", "BIOS folder: /storage/roms/bios", "Re-render config" };
#define NSET 3
static void render_settings(void){
    draw_text(28,22,3,"Settings",110,200,255);
    for(int i=0;i<NSET;i++){ int y=120+i*60;
        if(i==set_sel) fill(24,y-8,SCREEN_W-48,46,36,78,140);
        draw_text(44,y,2,SET_ITEMS[i],i==1?170:230,i==1?175:230,i==1?185:235);
    }
    draw_text(28,SCREEN_H-24,1,"A: SELECT   B: BACK",110,110,135);
}

static void render_help(void){
    draw_text(28,22,3,"Help / ROMs",110,200,255);
    draw_text(30,76,1,"Drop ROMs into:",200,205,215);
    draw_text(36,98,1,"/storage/roms/<system>/   (e.g. /storage/roms/snes/)",180,190,210);
    draw_text(30,136,1,"Then run Quick Setup or install that system in Library.",180,185,200);

    draw_text(30,180,1,"Controls:",200,205,215);
    draw_text(36,202,1,"D-Pad / Stick : move",180,185,200);
    draw_text(36,222,1,"A / Start     : select / confirm",180,185,200);
    draw_text(36,242,1,"B / Select    : back / cancel",180,185,200);
    draw_text(36,262,1,"X (Library)   : cycle filter",180,185,200);

    draw_text(30,306,1,"Repo:",200,205,215);
    draw_text(36,328,1,REPO_URL,140,180,220);
    draw_text(28,SCREEN_H-24,1,"B: BACK",110,110,135);
}

/* ===================================================================== */
/*  INPUT                                                                */
/* ===================================================================== */

static void on_home(int up,int down,int acc,int bk,int*running){
    if(up)   hsel=(hsel-1+NHOME)%NHOME;
    if(down) hsel=(hsel+1)%NHOME;
    if(bk){ *running=0; return; }
    if(acc){
        switch(hsel){
            case 0: hsel=0; screen=ST_QSETUP; break;            /* Quick Setup */
            case 1: lib_filter=0; lib_sel=lib_top=0; inventory_reload(); lib_clamp(); screen=ST_LIBRARY; break;
            case 2: cmd_start(SYNC "--update",ST_HOME); screen=ST_PROGRESS; break;
            case 3: set_sel=0; screen=ST_SETTINGS; break;
            case 4: screen=ST_HELP; break;
        }
    }
}
static void on_library(int up,int down,int acc,int bk,int xbtn){
    if(up)   { lib_sel--; lib_clamp(); }
    if(down) { lib_sel++; lib_clamp(); }
    if(xbtn) { lib_filter=(lib_filter+1)%4; lib_sel=lib_top=0; lib_clamp(); }
    if(bk)   { screen=ST_HOME; return; }
    if(acc && lib_count()>0){
        int i=lib_index(lib_sel); if(i>=0){ sheet_build(i); screen=ST_SHEET; }
    }
}
static void on_sheet(int up,int down,int acc,int bk){
    if(!sheet_change_core){
        if(up)   ssel=(ssel-1+NSHEET)%NSHEET;
        if(down) ssel=(ssel+1)%NSHEET;
        if(bk)   { screen=ST_LIBRARY; return; }
        if(acc)  sheet_act();
    } else {
        int n=NCORES>0?NCORES:1;
        if(up)   csel=(csel-1+n)%n;
        if(down) csel=(csel+1)%n;
        if(bk)   { sheet_change_core=0; return; }   /* back to the sheet */
        if(acc)  core_act();
    }
}
static void on_confirm(int acc,int bk){
    if(acc){ cmd_start(cf_cmd,ST_LIBRARY); screen=ST_PROGRESS; return; }
    if(bk){ screen=ST_SHEET; return; }
}
static void on_qsetup(int up,int down,int acc,int bk){
    if(up)   hsel=(hsel-1+3)%3;
    if(down) hsel=(hsel+1)%3;
    if(bk){ screen=ST_HOME; return; }
    if(acc){
        switch(hsel){
            case 0: cmd_start(SYNC "--quick-setup",ST_HOME); screen=ST_PROGRESS; break;
            case 1: lib_filter=0; lib_sel=lib_top=0; inventory_reload(); lib_clamp(); screen=ST_LIBRARY; break;
            case 2: screen=ST_HOME; break;
        }
    }
}
static void on_progress(int acc,int bk){
    if((acc||bk)&&g_done){
        screen=after_run;
        inventory_reload();   /* refresh state after any action */
        if(screen==ST_LIBRARY) lib_clamp();
    }
}
static void on_settings(int up,int down,int acc,int bk){
    if(up)   set_sel=(set_sel-1+NSET)%NSET;
    if(down) set_sel=(set_sel+1)%NSET;
    if(bk){ screen=ST_HOME; return; }
    if(acc){
        switch(set_sel){
            case 0: cmd_start(SYNC "--all-cores",ST_HOME); screen=ST_PROGRESS; break;
            case 1: break; /* info only */
            case 2: cmd_start(SYNC "--render-only",ST_HOME); screen=ST_PROGRESS; break;
        }
    }
}

int main(void){
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_JOYSTICK)!=0){ fprintf(stderr,"SDL_Init: %s\n",SDL_GetError()); return 1; }
    SDL_Window*win=SDL_CreateWindow("panicos-emu",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,SCREEN_W,SCREEN_H,SDL_WINDOW_FULLSCREEN_DESKTOP);
    REN=SDL_CreateRenderer(win,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC); if(!REN)REN=SDL_CreateRenderer(win,-1,0);
    SDL_RenderSetLogicalSize(REN,SCREEN_W,SCREEN_H); SDL_ShowCursor(SDL_DISABLE);
    font_init();
    SDL_Joystick*js=SDL_NumJoysticks()>0?SDL_JoystickOpen(0):NULL;
    term_clear();
    inventory_reload();   /* prime Home summary + update badge */

    int running=1;
    while(running){
        SDL_Event e;
        while(SDL_PollEvent(&e)){
            int btn=-1; SDL_Keycode k=0;
            if(e.type==SDL_QUIT){ running=0; continue; }
            if(e.type==SDL_JOYDEVICEADDED&&!js){ js=SDL_JoystickOpen(0); continue; }
            if(e.type==SDL_JOYBUTTONDOWN) btn=e.jbutton.button;
            else if(e.type==SDL_KEYDOWN) k=e.key.keysym.sym;
            else if(e.type==SDL_JOYHATMOTION){
                if(e.jhat.value&SDL_HAT_UP)k=SDLK_UP;
                else if(e.jhat.value&SDL_HAT_DOWN)k=SDLK_DOWN;
                else if(e.jhat.value&SDL_HAT_LEFT)k=SDLK_LEFT;
                else if(e.jhat.value&SDL_HAT_RIGHT)k=SDLK_RIGHT;
            }
            else continue;

            int up    = btn==BTN_UP    || k==SDLK_UP;
            int down  = btn==BTN_DOWN  || k==SDLK_DOWN;
            int acc   = btn==BTN_A     || btn==BTN_START || k==SDLK_RETURN;
            int bk    = btn==BTN_B     || btn==BTN_SELECT|| k==SDLK_ESCAPE;
            int xbtn  = btn==BTN_X     || k==SDLK_x;

            switch(screen){
                case ST_HOME:     on_home(up,down,acc,bk,&running); break;
                case ST_LIBRARY:  on_library(up,down,acc,bk,xbtn); break;
                case ST_SHEET:    on_sheet(up,down,acc,bk); break;
                case ST_CONFIRM:  on_confirm(acc,bk); break;
                case ST_QSETUP:   on_qsetup(up,down,acc,bk); break;
                case ST_PROGRESS: on_progress(acc,bk); break;
                case ST_SETTINGS: on_settings(up,down,acc,bk); break;
                case ST_HELP:     if(bk) screen=ST_HOME; break;
            }
        }
        if(screen==ST_PROGRESS) cmd_poll();

        SDL_SetRenderDrawColor(REN,12,12,20,255); SDL_RenderClear(REN);
        switch(screen){
            case ST_HOME:     render_home(); break;
            case ST_LIBRARY:  render_library(); break;
            case ST_SHEET:    render_sheet(); break;
            case ST_CONFIRM:  render_confirm(); break;
            case ST_QSETUP:   render_qsetup(); break;
            case ST_PROGRESS: render_progress(); break;
            case ST_SETTINGS: render_settings(); break;
            case ST_HELP:     render_help(); break;
        }
        SDL_RenderPresent(REN); SDL_Delay(16);
    }
    if(js)SDL_JoystickClose(js);
    SDL_DestroyTexture(FONT); SDL_DestroyRenderer(REN); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
