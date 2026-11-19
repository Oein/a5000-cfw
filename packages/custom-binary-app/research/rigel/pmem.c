/* pmem: inspect im.elf's Rigel WindowServer via /proc/<pid>/mem (emulator RE only, read-only)
 *  pmem <pid> rd <addr> <nwords>          raw words
 *  pmem <pid> ws <libObj_base>            dump WindowServer (global dword_EF5A34) + window list */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
static int fd;
static uint32_t rw(uint32_t a){ uint32_t v=0xdeadbeef; if(lseek(fd,a,SEEK_SET)==(off_t)-1||read(fd,&v,4)!=4) return 0xdeadbeef; return v; }
static void rb(uint32_t a,void*b,int n){ memset(b,0,n); lseek(fd,a,SEEK_SET); read(fd,b,n); }
static void win(uint32_t m){ uint8_t b[192]; rb(m,b,192);
  printf("    msg@%08x type=%d status=%d x,y=%d,%d w,h=%d,%d sw,sh=%d,%d order=%u f28=%08x f32=%08x u36=%u name='%.32s'\n",m,
   *(int*)(b+4),*(int*)(b+8),*(short*)(b+12),*(short*)(b+14),*(short*)(b+16),*(short*)(b+18),*(short*)(b+20),*(short*)(b+22),b[24],
   *(uint32_t*)(b+28),*(uint32_t*)(b+32),*(uint16_t*)(b+36),(char*)b+44);
  printf("      dirty=%d,%d,%d,%d reorder=%u front=%u a5=%u a6=%u nbuf=%u buf0={%d,%d,%d,%d,%d} buf1={%d,%d,%d,%d,%d} pid@0=%d +144=%u +148=%u\n",
   *(short*)(b+80),*(short*)(b+82),*(short*)(b+84),*(short*)(b+86),b[88],b[89],*(uint32_t*)(b+92),*(uint32_t*)(b+96),b[100],
   *(int*)(b+104),*(int*)(b+108),*(int*)(b+112),*(int*)(b+116),*(int*)(b+120),*(int*)(b+124),*(int*)(b+128),*(int*)(b+132),*(int*)(b+136),*(int*)(b+140),
   *(int*)(b+0),b[144],*(uint32_t*)(b+148)); }
int main(int c,char**v){
  char p[64]; snprintf(p,64,"/proc/%s/mem",v[1]); fd=open(p,!strcmp(v[2],"wr")?O_RDWR:O_RDONLY); if(fd<0){perror(p);return 1;}
  if(!strcmp(v[2],"rd")){ uint32_t a=strtoul(v[3],0,0); int n=atoi(v[4]); for(int i=0;i<n;i++){ if(i%8==0)printf("%s%08x:",i?"\n":"",a+i*4); printf(" %08x",rw(a+i*4)); } printf("\n"); return 0; }
  if(!strcmp(v[2],"wr")){ uint32_t a=strtoul(v[3],0,0),x=strtoul(v[4],0,0); lseek(fd,a,SEEK_SET); int r=write(fd,&x,4); printf("wr %08x=%08x -> %d now %08x\n",a,x,r,rw(a)); return 0; }
  if(!strcmp(v[2],"ws")){ uint32_t base=strtoul(v[3],0,0); uint32_t W=rw(base+0xEF5A34);
    printf("WindowServer@%08x vtbl=%08x w,h=%d,%d b36=%u b76..79=%08x ctx=%08x\n",W,rw(W),(short)rw(W+38),(short)rw(W+40),rw(W+36)&0xff,rw(W+76),rw(W+88));
    uint32_t ctx=rw(W+88); printf("  ctx+8(surface)=%08x  ctx: %08x %08x %08x %08x\n",rw(ctx+8),rw(ctx),rw(ctx+4),rw(ctx+8),rw(ctx+12));
    uint32_t h=rw(W+68),t=rw(W+72); printf("  list head=%08x tail=%08x\n",h,t);
    int k=0; for(uint32_t n=h;n&&n!=0xdeadbeef&&k<16;n=rw(n),k++){
      printf("  [%d] node=%08x next=%08x prev=%08x tex=%08x,%08x frontsel=%u b26=%u c28=%08x c32=%08x\n",k,n,rw(n),rw(n+4),rw(n+16),rw(n+20),rw(n+24)&0xff,(rw(n+24)>>16)&0xff,rw(n+28),rw(n+32));
      win(rw(n+12)); }
    return 0; }
  return 2; }
