/* rhello: "hello Rigel" — native (bionic) client of im.elf's Rigel Window Server via
 * gralloc.diadem.so rgl* + libdihal ws_hal_*.  Emulator experiment only.
 * usage: rhello [name] [seconds] [usage] [alpha]                                      */
#include "bionic.h"
#include "font57.h"
typedef struct { int w, h, fmt, rsv, slot; } WSB;          /* WSImageBuffer, 20 B */
static int (*ws_alloc)(int,int,int,int,WSB*,int*);
static int (*ws_free)(WSB*);
static int (*ws_lock)(WSB*,int,int,int,int,int,void**);
static int (*ws_unlock)(WSB*);
static void (*regbufs)(WSB*);
static void *(*rglCreateWindow)(void*,const char*,int,int,unsigned,unsigned,unsigned char);
static int (*rglShowWindow)(void*,int);
static int (*rglSwapBuffers)(void*,int,short,short,short,short);
static int (*rglDestroyWindow)(void*);
static int (*rglGetWindowParam)(void*,void*);
static int (*rglSetWindowParam)(void*,void*);
static int (*rglDI_flushVDF)(void*); static int (*rglWaitUpdate)(void*);
static int g_mode;
static void *g_win; static WSB g_b[2]; static int g_nb;
static char lb[512];
char *getenv(const char*); static const char *getenvx(const char*n){ const char*v=getenv(n); return v?v:"0"; }
#define P(...) do{ int n_=snprintf(lb,sizeof lb,__VA_ARGS__); write(1,lb,n_);}while(0)
static void *sym(void *h, const char *n){ void *p=dlsym(h,n); if(!p){P("dlsym %s: %s\n",n,dlerror()); exit(2);} return p; }
static void cleanup(void){ if(g_win){ P("destroy=%d\n", rglDestroyWindow(g_win)); g_win=0;} while(g_nb>0){ g_nb--; P("free[%d]=%d\n",g_nb,ws_free(&g_b[g_nb])); } }
static void onsig(int s){ P("signal %d\n",s); cleanup(); exit(128+s); }
static unsigned *g_px; static int g_stride;
static void rect(int x,int y,int w,int h,unsigned c){ for(int j=y;j<y+h;j++) for(int i=x;i<x+w;i++) if(i>=0&&i<640&&j>=0&&j<480) g_px[j*g_stride+i]=c; }
static void text(int x,int y,int s,const char*t,unsigned c){
  for(;*t;t++,x+=6*s){ for(unsigned g=0;g<sizeof FONT/sizeof FONT[0];g++) if(FONT[g].c==*t){
      for(int r=0;r<7;r++) for(int k=0;k<5;k++) if(FONT[g].r[r][k]=='#') rect(x+k*s,y+r*s,s,s,c); } } }
static void draw(int frame, unsigned alpha){
  unsigned A=alpha<<24;
  rect(0,0,640,480,0x00000000);                    /* fully transparent background */
  rect(40,40,560,140,A|0x00402000);                /* dark blue-ish panel (ABGR) */
  rect(40,40,560,6,A|0x000041D3);                  /* sony orange #D34100 bar */
  text(60,62,4,"HELLO RIGEL",A|0x00FFFFFF);
  text(60,110,2,"NATIVE UX_DEBUG_CLIENT",A|0x0000FFFF);
  char b[32]; snprintf(b,sizeof b,"FRAME %d",frame); text(60,140,2,b,A|0x0000FF00);
  rect(40,220,120,120,0xFF0000FF); rect(180,220,120,120,0xFF00FF00); rect(320,220,120,120,0xFFFF0000);
  rect(460,220,140,120,0x800000FF);                /* half-alpha red: shows alpha semantics */
  text(470,350,2,"A80",0xFFFFFFFF);
}
int main(int argc,char**argv){
  const char *name = argc>1?argv[1]:"UX_DEBUG_CLIENT";
  int secs = argc>2?atoi(argv[2]):10;
  int usage = argc>3?(int)strtol(argv[3],0,0):0x33;
  unsigned alpha = argc>4?(unsigned)strtol(argv[4],0,0):0xFF;
  g_mode = argc>5?(int)strtol(argv[5],0,0):0; /* bit0 flushVDF after swap, bit1 waitUpdate after swap */
  signal(11,onsig); signal(6,onsig); signal(15,onsig); signal(2,onsig);
  void *hd=dlopen("/system/lib/libdihal.so",RTLD_NOW); if(!hd){P("dlopen dihal: %s\n",dlerror());return 1;}
  void *hg=dlopen("/system/lib/hw/gralloc.diadem.so",RTLD_NOW); if(!hg){P("dlopen gralloc: %s\n",dlerror());return 1;}
  ws_alloc=sym(hd,"ws_hal_alloc"); ws_free=sym(hd,"ws_hal_free"); ws_lock=sym(hd,"ws_hal_lock"); ws_unlock=sym(hd,"ws_hal_unlock");
  regbufs=sym(hg,"_Z15registerBuffersP13WSImageBuffer");
  rglCreateWindow=sym(hg,"rglCreateWindow"); rglShowWindow=sym(hg,"rglShowWindow");
  rglSwapBuffers=sym(hg,"rglSwapBuffers"); rglDestroyWindow=sym(hg,"rglDestroyWindow");
  rglGetWindowParam=sym(hg,"rglGetWindowParam"); rglDI_flushVDF=sym(hg,"rglDI_flushVDF"); rglWaitUpdate=sym(hg,"rglWaitUpdate"); rglSetWindowParam=sym(hg,"rglSetWindowParam");
  P("pid %d name=%s usage=0x%x alpha=0x%x\n",getpid(),name,usage,alpha);
  for(int i=0;i<2;i++){ int st=0; memset(&g_b[i],0,sizeof g_b[i]); g_b[i].slot=-1;
    int r=ws_alloc(640,480,1,usage,&g_b[i],&st); P("ws_hal_alloc[%d]=%d stride=%d buf={%d,%d,%d,%d,slot %d}\n",i,r,st,g_b[i].w,g_b[i].h,g_b[i].fmt,g_b[i].rsv,g_b[i].slot);
    if(r){cleanup();return 3;} g_nb++; g_stride=st; }
  if(argc>6 && !strcmp(argv[6],"dry")){ cleanup(); P("dry ok\n"); return 0; }
  for(int i=0;i<2;i++){ void*va=0; int r=ws_lock(&g_b[i],usage,0,0,640,480,&va); P("lock[%d]=%d va=%p\n",i,r,va); if(r||!va){cleanup();return 4;}
    g_px=va; draw(i,alpha); ws_unlock(&g_b[i]); }
  regbufs(g_b);
  g_win=rglCreateWindow(0,name,640,480,0,0,2); P("rglCreateWindow=%p\n",g_win); if(!g_win){cleanup();return 5;}
  unsigned char wp[64]; memset(wp,0,64); int gr=rglGetWindowParam(g_win,wp); P("getParam=%d:",gr); for(int i=0;i<32;i++)P(" %02x",wp[i]); P("\n");
  P("show=%d\n",rglShowWindow(g_win,1));
  if(g_mode&4){ /* raise: RglWindowParam+16 = z-order index (0 = list head = drawn last = top) */
    memset(wp,0,64); rglGetWindowParam(g_win,wp); wp[16]=0;
    if(g_mode&8){ unsigned u36=(unsigned)strtol(getenvx("U36"),0,0); wp[28]=u36&0xff; wp[29]=u36>>8; }
    P("setParam(order=0)=%d\n",rglSetWindowParam(g_win,wp));
    memset(wp,0,64); rglGetWindowParam(g_win,wp); P("getParam:"); for(int i=0;i<32;i++)P(" %02x",wp[i]); P("\n"); }
  for(int t=0;t<secs*2;t++){ int f=t&1; void*va=0;
    if(!ws_lock(&g_b[f],usage,0,0,640,480,&va)&&va){ g_px=va; draw(t,alpha); ws_unlock(&g_b[f]); }
    int r=rglSwapBuffers(g_win,f,0,0,640,480); int fv=-9,wu=-9; if(g_mode&1) fv=rglDI_flushVDF(g_win); if(g_mode&2) wu=rglWaitUpdate(g_win);
    if(t<4||t%10==0) P("swap(%d)=%d flush=%d wait=%d\n",f,r,fv,wu); usleep(500000); }
  cleanup(); P("done\n"); return 0; }
