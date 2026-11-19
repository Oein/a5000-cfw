#include <stdio.h>
#include <dlfcn.h>
int main(int c,char**v){ void*h=dlopen(c>1?v[1]:"libsdcom.so",RTLD_NOW); printf("hello glibc dl=%p %s\n",h,h?"":dlerror()); return 0;}
