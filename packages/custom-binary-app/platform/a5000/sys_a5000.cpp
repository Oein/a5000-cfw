// a5000 system services: Clock (gettimeofday), Log (file + stderr), Storage (flat
// key=int file on tmpfs), and the shooting Custom Key store (text conf under /setting).
// All generic libc; no engine deps.
#include "uicore/platform.h"
#include "uicore/keymap_shoot.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <time.h>

using namespace uicore;

namespace {

class SysClock : public Clock {
public:
    long nowMs() { struct timeval t; gettimeofday(&t, 0); return (long)t.tv_sec * 1000 + t.tv_usec / 1000; }
};

class FileLog : public Log {
public:
    FileLog(const char *path) : f_(0) { if (path && *path) f_ = fopen(path, "a"); }
    ~FileLog() { if (f_) fclose(f_); }
    void line(const char *s) {
        struct timeval tv; gettimeofday(&tv, 0);
        struct tm tmv; localtime_r(&tv.tv_sec, &tmv);
        char ts[32];
        snprintf(ts, sizeof ts, "%02d:%02d:%02d.%03d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec, (int)(tv.tv_usec / 1000));
        fprintf(stderr, "%s %s\n", ts, s); fflush(stderr);
        if (f_) { fprintf(f_, "%s %s\n", ts, s); fflush(f_); }
    }
private:
    FILE *f_;
};

// Minimal "key val\n" preference file. Enough for keymap overrides (M1+).
class FileStorage : public Storage {
public:
    FileStorage(const char *path) { path_[0] = 0; if (path) { strncpy(path_, path, sizeof path_ - 1); path_[sizeof path_ - 1] = 0; } }
    bool getInt(const char *key, int *out) {
        if (!path_[0]) return false;
        FILE *f = fopen(path_, "r"); if (!f) return false;
        char k[64]; int v; bool found = false;
        while (fscanf(f, "%63s %d", k, &v) == 2) if (!strcmp(k, key)) { *out = v; found = true; }
        fclose(f); return found;
    }
    bool setInt(const char *key, int val) {
        if (!path_[0]) return false;
        // append (last wins in getInt); fine for M0
        FILE *f = fopen(path_, "a"); if (!f) return false;
        fprintf(f, "%s %d\n", key, val); fclose(f); return true;
    }
private:
    char path_[256];
};

// Shooting Custom Key map: a small TEXT conf under /setting (rw FAT, reboot-recoverable, NOT flash
// -> brick-0). read()/write() of the whole blob; the uicore layer parses/formats and falls back to
// defaults on any failure, so a missing or garbage file is harmless.
class FileKeymapStore : public ShootKeymapStore {
public:
    FileKeymapStore(const char *path) { path_[0] = 0; if (path) { strncpy(path_, path, sizeof path_ - 1); path_[sizeof path_ - 1] = 0; } }
    bool read(char *buf, int n) {
        if (!path_[0] || !buf || n <= 0) return false;
        FILE *f = fopen(path_, "r"); if (!f) return false;
        size_t r = fread(buf, 1, (size_t)n - 1, f);
        fclose(f);
        buf[r] = 0;
        return r > 0;
    }
    bool write(const char *buf) {
        if (!path_[0] || !buf) return false;
        ensureDir();
        FILE *f = fopen(path_, "w"); if (!f) return false;
        size_t len = strlen(buf);
        size_t w = fwrite(buf, 1, len, f);
        fclose(f);
        return w == len;
    }
private:
    void ensureDir() {                      // create the parent dir (e.g. /setting/a5k) if absent
        char d[256]; strncpy(d, path_, sizeof d - 1); d[sizeof d - 1] = 0;
        char *slash = strrchr(d, '/');
        if (slash && slash != d) { *slash = 0; mkdir(d, 0777); }
    }
    char path_[256];
};

} // namespace

namespace a5000 {
uicore::Clock   *createClock()                    { return new SysClock(); }
uicore::Log     *createLog(const char *path)      { return new FileLog(path); }
uicore::Storage *createStorage(const char *path)  { return new FileStorage(path); }
uicore::ShootKeymapStore *createKeymapStore(const char *path) { return new FileKeymapStore(path); }
}
