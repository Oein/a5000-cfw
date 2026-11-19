/*
 * a5000 읽기 전용 프로브
 *   probe fb            : /dev/fb0 의 화면 정보(FBIOGET_VSCREENINFO/FSCREENINFO) 출력 — 쓰기 없음
 *   probe keys <sec>    : /dev/input/event0 키 이벤트를 <sec>초 동안 출력 — 읽기만
 * 빌드: zig cc -target arm-linux-musleabi -mcpu=cortex_a9 -static -Os -o PROBE probe.c
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

/* 커널 3.0 32bit ARM 의 struct input_event (16B) */
struct ev32 {
    unsigned int sec, usec;
    unsigned short type, code;
    int value;
};

static int do_fb(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) { printf("open %s: %s\n", path, strerror(errno)); return 1; }
    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    memset(&v, 0, sizeof v);
    memset(&f, 0, sizeof f);
    if (ioctl(fd, FBIOGET_FSCREENINFO, &f) == 0)
        printf("fix: id=%.16s smem_len=%u line_length=%u type=%u visual=%u\n",
               f.id, f.smem_len, f.line_length, f.type, f.visual);
    else
        printf("FSCREENINFO: %s\n", strerror(errno));
    if (ioctl(fd, FBIOGET_VSCREENINFO, &v) == 0)
        printf("var: %ux%u virt=%ux%u off=%u,%u bpp=%u R%u/%u G%u/%u B%u/%u A%u/%u\n",
               v.xres, v.yres, v.xres_virtual, v.yres_virtual, v.xoffset, v.yoffset,
               v.bits_per_pixel, v.red.offset, v.red.length, v.green.offset, v.green.length,
               v.blue.offset, v.blue.length, v.transp.offset, v.transp.length);
    else
        printf("VSCREENINFO: %s\n", strerror(errno));
    close(fd);
    return 0;
}

static int do_keys(const char *path, int seconds)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) { printf("open %s: %s\n", path, strerror(errno)); return 1; }
    char name[64] = {0};
    if (ioctl(fd, EVIOCGNAME(sizeof name - 1), name) >= 0) printf("device: %s\n", name);
    time_t end = time(NULL) + seconds;
    printf("reading %d s ... press buttons\n", seconds);
    fflush(stdout);
    while (time(NULL) < end) {
        struct pollfd p = { fd, POLLIN, 0 };
        if (poll(&p, 1, 500) <= 0) continue;
        struct ev32 e[16];
        ssize_t n = read(fd, e, sizeof e);
        for (int i = 0; i < n / (ssize_t)sizeof(struct ev32); i++) {
            if (e[i].type == 0) continue; /* EV_SYN 생략 */
            printf("t=%u.%06u type=%u code=%u value=%d\n", e[i].sec, e[i].usec, e[i].type, e[i].code, e[i].value);
        }
        fflush(stdout);
    }
    close(fd);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "fb")) return do_fb(argc >= 3 ? argv[2] : "/dev/fb0");
    if (argc >= 2 && !strcmp(argv[1], "keys")) return do_keys(argc >= 4 ? argv[3] : "/dev/input/event0", argc >= 3 ? atoi(argv[2]) : 10);
    printf("usage: probe fb [dev] | probe keys <sec> [dev]\n");
    return 2;
}
