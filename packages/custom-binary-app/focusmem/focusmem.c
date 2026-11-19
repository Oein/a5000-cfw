/*
 * focusmem — im.elf 의 ModelCamera 객체에서 MF 초점 거리를 "읽기 전용"으로 관찰하는 실험 도구.
 * 근거: re/notes/mf_distance.md (vtable B+0xe4b958, 보조 vtable +36 = B+0xe4b9ac, 거리 s32 @+148, near u8 @+152, inf u8 @+153)
 *
 * 안전 규칙:
 *  - /proc/<pid>/mem 은 O_RDONLY 로만 연다. 쓰기·ptrace 없음.
 *  - 스캔 대상은 익명 rw-p 영역(경로 없음 또는 [heap]/[stack]) 만. 파일/장치 매핑은 건너뛴다.
 *  - libc 없음(정적, 시스템 콜 직접) → 어떤 libc 에도 의존하지 않음.
 *
 * 사용:
 *   focusmem scan <pid> <libObj_base_hex>          후보 객체 주소 출력
 *   focusmem poll <pid> <addr_hex> <seconds>       100ms 마다 거리 읽어 바뀔 때 출력
 *   focusmem serve <pid> <app_pid> <out_file>      libObj 베이스를 maps 에서 찾고 스캔 후, 100ms 마다
 *                                                   "dist near inf\n" 을 out_file 에 원자적으로(rename) 기록.
 *                                                   app_pid 프로세스가 사라지거나 im.elf 읽기가 실패하면 종료.
 * 실측(2026-11-15, E 35mm F1.8): dist = mm(최단 300 에서 near=1), 무한대 = -1/inf=1.
 */
typedef unsigned int u32;
typedef int s32;
typedef unsigned char u8;

static long sys3(long n, long a, long b, long c) {
    register long r7 __asm__("r7") = n;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2) : "memory");
    return r0;
}
static long sys_pread(long fd, void *buf, long len, unsigned long long off) {
    /* ARM EABI pread64: r0 fd, r1 buf, r2 len, r3 pad, r4/r5 = 64-bit offset */
    register long r7 __asm__("r7") = 180;
    register long r0 __asm__("r0") = fd;
    register long r1 __asm__("r1") = (long)buf;
    register long r2 __asm__("r2") = len;
    register long r3 __asm__("r3") = 0;
    register long r4 __asm__("r4") = (long)(u32)off;
    register long r5 __asm__("r5") = (long)(u32)(off >> 32);
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5) : "memory");
    return r0;
}
#define SYS_exit 1
#define SYS_read 3
#define SYS_write 4
#define SYS_open 5
#define SYS_close 6
#define SYS_nanosleep 162

static void out(const char *s) { long n = 0; while (s[n]) n++; sys3(SYS_write, 1, (long)s, n); }
static void outhex(u32 v) {
    char b[11]; b[0] = '0'; b[1] = 'x';
    for (int i = 0; i < 8; i++) { int d = (v >> (28 - 4 * i)) & 15; b[2 + i] = d < 10 ? '0' + d : 'a' + d - 10; }
    b[10] = 0; out(b);
}
static void outdec(s32 v) {
    char b[12]; int i = 11; b[i] = 0; int neg = v < 0; u32 u = neg ? -(u32)v : (u32)v;
    do { b[--i] = '0' + u % 10; u /= 10; } while (u);
    if (neg) b[--i] = '-';
    out(b + i);
}
static u32 parsehex(const char *s) {
    u32 v = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    for (; *s; s++) {
        int c = *s, d;
        if (c >= '0' && c <= '9') d = c - '0'; else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10; else break;
        v = v * 16 + d;
    }
    return v;
}
static u32 parsedec(const char *s) { u32 v = 0; for (; *s >= '0' && *s <= '9'; s++) v = v * 10 + (*s - '0'); return v; }
static void cat(char *dst, const char *a) { while (*dst) dst++; while (*a) *dst++ = *a++; *dst = 0; }
static void die(const char *m) { out(m); out("\n"); sys3(SYS_exit, 1, 0, 0); for (;;) {} }

static char maps[64 * 1024];
static u32 chunk[16384]; /* 64KB */

static int open_mem(const char *pid) {
    char path[64]; path[0] = 0; cat(path, "/proc/"); cat(path, pid); cat(path, "/mem");
    long fd = sys3(SYS_open, (long)path, 0 /* O_RDONLY */, 0);
    if (fd < 0) die("open mem failed");
    return (int)fd;
}

static u32 found_addr;

static u32 find_base(const char *pid) {
    char path[64]; path[0] = 0; cat(path, "/proc/"); cat(path, pid); cat(path, "/maps");
    long mf = sys3(SYS_open, (long)path, 0, 0);
    if (mf < 0) return 0;
    long total = 0, n;
    while ((n = sys3(SYS_read, mf, (long)(maps + total), sizeof(maps) - 1 - total)) > 0) total += n;
    maps[total] = 0;
    sys3(SYS_close, mf, 0, 0);
    char *line = maps;
    while (*line) {
        char *end = line; while (*end && *end != '\n') end++;
        char *p = line; while (p < end && *p != ' ') p++;
        int rx = p + 3 < end && p[1] == 'r' && p[3] == 'x';
        int obj = 0;
        for (char *q = p; q + 9 <= end; q++) {
            if (q[0] == 'l' && q[1] == 'i' && q[2] == 'b' && q[3] == 'O' && q[4] == 'b' && q[5] == 'j' && q[6] == '.' && q[7] == 's' && q[8] == 'o') { obj = 1; break; }
        }
        if (rx && obj) return parsehex(line);
        line = *end ? end + 1 : end;
    }
    return 0;
}

static void scan(const char *pid, u32 base) {
    u32 vt = base + 0xe4b958, vt2 = base + 0xe4b9ac;
    char path[64]; path[0] = 0; cat(path, "/proc/"); cat(path, pid); cat(path, "/maps");
    long mf = sys3(SYS_open, (long)path, 0, 0);
    if (mf < 0) die("open maps failed");
    long total = 0, n;
    while ((n = sys3(SYS_read, mf, (long)(maps + total), sizeof(maps) - 1 - total)) > 0) total += n;
    maps[total] = 0;
    sys3(SYS_close, mf, 0, 0);
    int fd = open_mem(pid);
    u32 scanned = 0; int hits = 0;
    char *line = maps;
    while (*line) {
        char *end = line; while (*end && *end != '\n') end++;
        /* "start-end perms offset dev inode   path" */
        u32 start = parsehex(line);
        char *p = line; while (*p != '-') p++; u32 stop = parsehex(p + 1);
        while (*p != ' ') p++; p++;
        int rw = p[0] == 'r' && p[1] == 'w' && p[3] == 'p';
        /* path = 6번째 필드 */
        char *q = p; int field = 1;
        while (q < end && field < 5) { while (q < end && *q != ' ') q++; while (q < end && *q == ' ') q++; field++; }
        while (q < end && *q != ' ') q++;           /* skip inode */
        while (q < end && *q == ' ') q++;
        int anon = (q == end) || (*q == '[' && (q[1] == 'h' || q[1] == 's'));   /* 없음 / [heap] / [stack] */
        if (rw && anon && stop > start && stop - start <= 64u * 1024 * 1024) {
            for (u32 a = start; a < stop; a += sizeof(chunk)) {
                u32 len = stop - a < sizeof(chunk) ? stop - a : sizeof(chunk);
                long r = sys_pread(fd, chunk, len, a);
                if (r <= 0) break;
                scanned += r;
                for (u32 i = 0; i + 10 < (u32)r / 4; i++) {
                    if (chunk[i] == vt && chunk[i + 9] == vt2) {
                        u8 o[8];
                        out("HIT "); outhex(a + i * 4);
                        if (sys_pread(fd, o, 6, a + i * 4 + 148) == 6) {
                            out(" dist="); outdec(*(s32 *)o);
                            out(" near="); outdec(o[4]);
                            out(" inf="); outdec(o[5]);
                        }
                        out("\n");
                        hits++;
                        if (!found_addr) found_addr = a + i * 4;
                    }
                }
            }
        }
        line = *end ? end + 1 : end;
    }
    out("scanned="); outdec((s32)(scanned / 1024)); out("KB hits="); outdec(hits); out("\n");
}

static void poll(const char *pid, u32 addr, u32 seconds) {
    int fd = open_mem(pid);
    u8 cur[8], last[8]; int have = 0;
    struct { long s, ns; } ts = { 0, 100 * 1000 * 1000 };
    for (u32 t = 0; t < seconds * 10; t++) {
        if (sys_pread(fd, cur, 6, addr + 148) != 6) die("pread failed");
        int same = have;
        for (int i = 0; i < 6 && same; i++) same = cur[i] == last[i];
        if (!same) {
            out("t="); outdec(t * 100); out("ms dist="); outdec(*(s32 *)cur);
            out(" near="); outdec(cur[4]); out(" inf="); outdec(cur[5]); out("\n");
            for (int i = 0; i < 6; i++) last[i] = cur[i];
            have = 1;
        }
        sys3(SYS_nanosleep, (long)&ts, 0, 0);
    }
    out("done\n");
}

#define SYS_kill 37
#define SYS_rename 38

static void serve(const char *pid, u32 app_pid, const char *outfile) {
    u32 base = find_base(pid);
    if (!base) die("libObj base not found");
    int fd = open_mem(pid);
    /* 캐시: "<pid> <addr>" — 같은 im.elf 면 vtable 재확인 후 재사용(스캔 ~10s 생략) */
    char cache[128]; cache[0] = 0; cat(cache, outfile); cat(cache, ".addr");
    char cbuf[40]; long cf = sys3(SYS_open, (long)cache, 0, 0);
    if (cf >= 0) {
        long n = sys3(SYS_read, cf, (long)cbuf, sizeof(cbuf) - 1);
        sys3(SYS_close, cf, 0, 0);
        if (n > 0) {
            cbuf[n] = 0;
            u32 cpid = parsedec(cbuf), addr = 0;
            char *q = cbuf; while (*q && *q != ' ') q++;
            if (*q) addr = parsehex(q + 1);
            u32 hdr[10];
            if (cpid == parsedec(pid) && addr && sys_pread(fd, hdr, 40, addr) == 40
                    && hdr[0] == base + 0xe4b958 && hdr[9] == base + 0xe4b9ac) {
                found_addr = addr;
                out("cache hit\n");
            }
        }
    }
    if (!found_addr) {
        scan(pid, base);
        if (!found_addr) die("object not found");
        char line[40]; line[0] = 0; cat(line, pid); cat(line, " 0x");
        char hx[9]; for (int i = 0; i < 8; i++) { int d = (found_addr >> (28 - 4 * i)) & 15; hx[i] = d < 10 ? '0' + d : 'a' + d - 10; } hx[8] = 0;
        cat(line, hx); cat(line, "\n");
        long f = sys3(SYS_open, (long)cache, 01 | 0100 | 01000, 0644);
        if (f >= 0) { long n = 0; while (line[n]) n++; sys3(SYS_write, f, (long)line, n); sys3(SYS_close, f, 0, 0); }
    }
    char tmp[128]; tmp[0] = 0; cat(tmp, outfile); cat(tmp, ".tmp");
    u8 cur[8], last[8]; int have = 0;
    struct { long s, ns; } ts = { 0, 100 * 1000 * 1000 };
    for (;;) {
        if (sys3(SYS_kill, app_pid, 0, 0) != 0) break;            /* 앱 종료 → 같이 종료 */
        if (sys_pread(fd, cur, 6, found_addr + 148) != 6) break;  /* im.elf 사라짐 */
        int same = have;
        for (int i = 0; i < 6 && same; i++) same = cur[i] == last[i];
        if (!same) {
            char line[48]; line[0] = 0;
            char num[12]; int k = 11; num[k] = 0; s32 v = *(s32 *)cur; int neg = v < 0; u32 u = neg ? -(u32)v : (u32)v;
            do { num[--k] = '0' + u % 10; u /= 10; } while (u);
            if (neg) num[--k] = '-';
            cat(line, num + k); cat(line, cur[4] ? " 1" : " 0"); cat(line, cur[5] ? " 1\n" : " 0\n");
            long f = sys3(SYS_open, (long)tmp, 01 | 0100 | 01000 /* O_WRONLY|O_CREAT|O_TRUNC */, 0644);
            if (f >= 0) {
                long n = 0; while (line[n]) n++;
                sys3(SYS_write, f, (long)line, n);
                sys3(SYS_close, f, 0, 0);
                sys3(SYS_rename, (long)tmp, (long)outfile, 0);
            }
            for (int i = 0; i < 6; i++) last[i] = cur[i];
            have = 1;
        }
        sys3(SYS_nanosleep, (long)&ts, 0, 0);
    }
    out("serve end\n");
}

static int streq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

void main_c(long *sp) {
    int argc = (int)sp[0];
    char **argv = (char **)(sp + 1);
    if (argc >= 4 && streq(argv[1], "scan")) scan(argv[2], parsehex(argv[3]));
    else if (argc >= 5 && streq(argv[1], "serve")) serve(argv[2], parsedec(argv[3]), argv[4]);
    else if (argc >= 5 && streq(argv[1], "poll")) poll(argv[2], parsehex(argv[3]), parsedec(argv[4]));
    else out("usage: focusmem scan <pid> <base> | poll <pid> <addr> <sec>\n");
    sys3(SYS_exit, 0, 0, 0);
}

__asm__(".global _start\n_start:\n mov r0, sp\n bl main_c\n");
