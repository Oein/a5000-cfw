/* keyprobe: event0 capture + ABT (arbiter) APP_INFO sender. glibc, camera /usr/lib/libosal_uipc.so.
 *  keyprobe cap <sec>                         : print EV_KEY etc. from /dev/input/event0
 *  keyprobe abt <cmd> <flag> <a7> <mode> <sec> : send ABT msg (queue 0x01490411, 436 B) then capture
 *     mode 0: keyinfo = {~0,~0,~0,~0,0x7FFFDFFF,0...} (resume path, all keys)   mode 1: keyinfo all 0
 *  keyprobe raw <qid> <size>                   : (debug) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/time.h>
#include <stdint.h>
int osal_valloc_msg_wait(unsigned qid, void **msg, unsigned size, int wait);
int osal_snd_msg(unsigned qid, void *msg);
int osal_free_msg(unsigned qid, void *msg);
struct iev { uint32_t sec, usec; uint16_t type, code; int32_t value; };
static double now(void){ struct timeval t; gettimeofday(&t,0); return t.tv_sec+t.tv_usec/1e6; }
static void cap(int secs){
  int fd=open("/dev/input/event0",O_RDONLY|O_NONBLOCK); if(fd<0){perror("event0");return;}
  printf("capture %d s\n",secs); fflush(stdout); double t0=now();
  while(now()-t0<secs){ struct pollfd p={fd,POLLIN,0}; if(poll(&p,1,200)<=0) continue;
    struct iev e; while(read(fd,&e,sizeof e)==(int)sizeof e){ if(e.type==0) continue;
      printf("%7.3f type=%u code=%u value=%d\n",now()-t0,e.type,e.code,e.value); fflush(stdout);} }
  close(fd); printf("capture end\n");
}
int main(int c,char**v){
  setvbuf(stdout,0,_IOLBF,0);
  if(c>=3&&!strcmp(v[1],"cap")){ cap(atoi(v[2])); return 0; }
  if(c>=7&&!strcmp(v[1],"abt")){
    int cmd=strtol(v[2],0,0), flag=strtoul(v[3],0,0), a7=strtol(v[4],0,0), mode=atoi(v[5]);
    uint8_t *m=0; int r=osal_valloc_msg_wait(0x01490411,(void**)&m,0x1B4,1);
    printf("valloc=%d m=%p\n",r,m); if(r||!m) return 1;
    ((int32_t*)m)[-1]=a7; memset(m,0,0x1B4);
    ((int32_t*)m)[0]=cmd; ((int32_t*)m)[1]=flag;
    uint32_t *ki=(uint32_t*)(m+0x188);
    if(mode==0){ ki[0]=ki[1]=ki[2]=ki[3]=0xFFFFFFFF; ki[4]=0x7FFFDFFF; }
    ((int32_t*)m)[0x1B0/4]=a7;
    r=osal_snd_msg(0x01490411,m); printf("ABT cmd=%d flag=0x%x a7=0x%x mode=%d snd=%d\n",cmd,flag,a7,mode,r);
    if(r) osal_free_msg(0x01490411,m);
    cap(atoi(v[6])); return 0; }
  printf("usage\n"); return 2; }
