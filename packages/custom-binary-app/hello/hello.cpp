// glibc 동적 링크 + 카메라 libsdcom/libstdc++ 로드 확인용 (SDCom 호출 없음)
#include <cstdio>
#include <string>
#include <gnu/libc-version.h>
int main() {
    std::string s = "hello from a5k glibc build";
    printf("%s, glibc %s\n", s.c_str(), gnu_get_libc_version());
    return 0;
}
