// a5000 in-process Media backend (route (b)) -- the device-side gallery behind the
// uicore playback screen (uicore/src/ui/screens/playback.cpp -> Media interface).
//
// WHAT THIS IS
// ============
// The stock camera's playback is a separate media subsystem (FactoryPlayer 0x0070,
// stillPlay; re/ledger.tsv L022) driven through the engine's ModelManager. In the
// emulator that whole PLAY path hangs (the camera-mode EE_STOP completion never
// arrives because the SA DSP is not modelled -- emulator/docs/RE_SD_MEDIA_SERVICE.md
// and RE_PLAYBACK_SWITCH.md). It is also brick-sensitive to drive from our process.
// So, exactly as L022 sanctions ("the basic gallery can be replaced by direct DCIM
// JPEG/ARW decode from SD, the previous-app method"), this backend READS the DCF tree
// off the card filesystem and decodes thumbnails itself. It never touches the stock
// media subsystem, never writes/deletes anything (remove() left unsupported), reads
// only the APP1/SOF head of each file, and decodes only small bitmaps -- so it is
// READ-ONLY, RAM-bounded and brick-safe (reboot/undeploy = pure stock).
//
// PATH (device vs emulator)
// -------------------------
// The DCF root is resolved from A5K_UI_DCIM, else the real card mount
// /android/mnt/sdcard/DCIM, else the RAM tmpfs /android/a5k/DCIM (the emulator
// verification drop). The gallery logic is identical, only the directory differs.
//
// SPEED + RAM (device: ~10 MB free, SD is FUSE/slow)
// --------------------------------------------------
//  * Thumbnails come from the EXIF embedded thumbnail (IFD1 JPEGInterchangeFormat, a
//    ~160x120 baseline JPEG inside APP1) -- so a 6000x4000 24 MP still never fully
//    decodes. Only that tiny JPEG is decoded (NanoJPEG).
//  * I/O is SEGMENT-STREAMED: we walk the JPEG markers with lseek, read ONLY the APP1
//    (Exif) body (<=64 KB, holds the thumbnail + the dimensions + the date) and stop at
//    the scan -- instead of slurping a fixed 160 KB head. Non-JPEG/SOF bodies are
//    skipped, not read. Original W/H and the capture date come straight out of EXIF
//    (PixelXDimension / DateTimeOriginal), so no deep read is needed.
//  * Decoded-thumbnail caching is the screen's job (playback.cpp keeps 24 RGBA slots);
//    we keep the per-call allocation minimal and let NanoJPEG reuse its Huffman tables
//    across decodes (nanojpeg.c: vlctab kept resident) to cut per-image cost.
//  * A file with NO EXIF thumbnail falls back to decoding the MAIN image, but only when
//    provably small (file <= kMainMaxFile AND dims <= kMainMaxPix); otherwise the cell
//    stays the placeholder grey (honest, never an OOM).
//
// RAW+JPEG dedup: a RAW+JPEG shot writes DSCxxxxx.ARW AND DSCxxxxx.JPG. When both share
// a basename in the same folder we keep only the JPG (fast real thumbnail) and hide the
// ARW; a lone ARW stays listed (its embedded JPEG thumbnail, or a placeholder).
//
// This file includes no im.elf internals and no uicore internals beyond platform.h.
#include "uicore/platform.h"
#include "uicore/log.h"
#include "nanojpeg.h"

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/stat.h>

namespace {

using namespace uicore;

// ---- bounds (RAM safety) --------------------------------------------------
enum {
    kMaxEntries   = 4000,            // DCF cap
    kApp1Max      = 68 * 1024,       // EXIF APP1 body cap (JPEG segment len is 16-bit)
    kThumbMaxBytes= 128 * 1024,      // sanity cap on an embedded thumbnail JPEG
    kMainMaxFile  = 2 * 1024 * 1024, // only full-decode a MAIN image when the file is this small
    kMainMaxPix   = 1200000          // ...and its dims are <= this (~1.2 MP -> <=3.6 MB RGB)
};

struct Entry {
    char path[256];
    char name[16];
    char folder[12];
    int  folderNo, fileNo;
    bool movie, raw, dead;          // dead = deduped (ARW hidden behind a JPG)
    // lazily filled by info():
    bool   meta;
    int    w, h;
    unsigned size;
    int    year, mon, day, hour, min, sec;
};

// What one streamed pass over a JPEG/ARW head yields.
struct JpegMeta {
    unsigned char *app1;    // malloc'd Exif APP1 body ("Exif\0\0" + TIFF...) or 0
    int            app1len;
    int            sofW, sofH;
    unsigned       thO, thL; // IFD1 thumbnail: offset (TIFF-relative) + length
    unsigned       exW, exH; // EXIF PixelXDimension / ImageWidth
    char           date[20]; // "YYYY:MM:DD HH:MM:SS" or ""
};

// ======================= EXIF (TIFF) IFD reader ==============================
static unsigned rd16(const unsigned char *p, bool be) { return be ? (p[0] << 8) | p[1] : (p[1] << 8) | p[0]; }
static unsigned rd32(const unsigned char *p, bool be) {
    return be ? ((unsigned)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]
              : ((unsigned)p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0];
}

// Parse one IFD at buf[ifd] (tiff base = buf[0], length n). Collects, where present:
//  thumbnail (0x0201/0x0202), Exif sub-IFD ptr (0x8769), date (0x9003/0x0132),
//  dims (0xA002/0xA003 PixelXY, else 0x0100/0x0101 ImageWidth/Length). Returns next IFD.
static unsigned exifIfd(const unsigned char *buf, int n, unsigned ifd, bool be,
                        JpegMeta *m, unsigned *exifSub) {
    if (ifd + 2 > (unsigned)n) return 0;
    unsigned cnt = rd16(buf + ifd, be), e = ifd + 2;
    for (unsigned i = 0; i < cnt; i++, e += 12) {
        if (e + 12 > (unsigned)n) return 0;
        unsigned tag = rd16(buf + e, be), typ = rd16(buf + e + 2, be), num = rd32(buf + e + 4, be);
        const unsigned char *vp = buf + e + 8;
        switch (tag) {
            case 0x0201: m->thO = rd32(vp, be); break;
            case 0x0202: m->thL = rd32(vp, be); break;
            case 0x8769: if (exifSub) *exifSub = rd32(vp, be); break;
            case 0xA002: m->exW = (typ == 3) ? rd16(vp, be) : rd32(vp, be); break;  // PixelXDimension
            case 0xA003: m->exH = (typ == 3) ? rd16(vp, be) : rd32(vp, be); break;  // PixelYDimension
            case 0x0100: if (!m->exW) m->exW = (typ == 3) ? rd16(vp, be) : rd32(vp, be); break; // ImageWidth
            case 0x0101: if (!m->exH) m->exH = (typ == 3) ? rd16(vp, be) : rd32(vp, be); break; // ImageLength
            case 0x9003: case 0x0132:
                if (typ == 2 && num >= 19 && !m->date[0]) {
                    unsigned off = rd32(vp, be);
                    if (off + 19 <= (unsigned)n) { memcpy(m->date, buf + off, 19); m->date[19] = 0; }
                }
                break;
            default: break;
        }
    }
    if (e + 4 > (unsigned)n) return 0;
    return rd32(buf + e, be);
}

static void parseExif(JpegMeta *m) {
    // app1 = "Exif\0\0" + TIFF; TIFF base = app1+6
    if (!m->app1 || m->app1len < 14 || memcmp(m->app1, "Exif\0\0", 6)) return;
    const unsigned char *t = m->app1 + 6; int tn = m->app1len - 6;
    if (tn <= 8) return;
    bool be = (t[0] == 'M');
    unsigned ifd0 = rd32(t + 4, be), exifSub = 0;
    unsigned ifd1 = exifIfd(t, tn, ifd0, be, m, &exifSub);   // IFD0 (dims/date may be here)
    if (exifSub) exifIfd(t, tn, exifSub, be, m, 0);          // Exif sub-IFD (PixelXY, DateTimeOriginal)
    if (ifd1)    exifIfd(t, tn, ifd1, be, m, 0);             // IFD1 = thumbnail
}

// Stream the JPEG/ARW markers from fd: read only segment headers (lseek over bodies),
// capture the Exif APP1 body and the first SOF dims, stop at SOS. Minimal I/O.
static void streamJpeg(int fd, JpegMeta *m) {
    memset(m, 0, sizeof *m);
    unsigned char h[4];
    if (pread(fd, h, 2, 0) != 2 || h[0] != 0xFF || h[1] != 0xD8) return;   // SOI
    off_t p = 2;
    for (int guard = 0; guard < 64; guard++) {
        if (pread(fd, h, 4, p) != 4) break;
        if (h[0] != 0xFF) break;
        unsigned mk = h[1];
        if (mk == 0xD9 || mk == 0xDA) break;                               // EOI / SOS
        if (mk == 0x01 || (mk >= 0xD0 && mk <= 0xD7)) { p += 2; continue; } // standalone
        int seg = (h[2] << 8) | h[3];                                       // length incl. the 2 bytes
        int blen = seg - 2; off_t body = p + 4;
        if (blen < 0) break;
        if (mk == 0xE1 && blen >= 14 && !m->app1) {                         // APP1: maybe Exif
            int want = blen < kApp1Max ? blen : kApp1Max;
            unsigned char *a = (unsigned char *)malloc(want);
            if (a) {
                if (pread(fd, a, want, body) == want && !memcmp(a, "Exif\0\0", 6)) { m->app1 = a; m->app1len = want; }
                else free(a);
            }
        } else if ((mk == 0xC0 || mk == 0xC1 || mk == 0xC2) && blen >= 5) { // SOF -> dims
            unsigned char s[5];
            if (pread(fd, s, 5, body) == 5) { m->sofH = (s[1] << 8) | s[2]; m->sofW = (s[3] << 8) | s[4]; }
        }
        p = body + blen;
    }
    parseExif(m);
}

static void freeMeta(JpegMeta *m) { if (m->app1) { free(m->app1); m->app1 = 0; } }

// ======================= decode glue (NanoJPEG -> RGBA, bounded) ==============
// Decode the baseline JPEG in [jpg,len] and box-downscale into a fresh RGBA buffer
// no larger than maxW x maxH (aspect preserved). Returns false on any failure.
static bool decodeToRgba(const unsigned char *jpg, int len, int maxW, int maxH, MediaImage *out) {
    njInit();
    if (njDecode(jpg, len) != NJ_OK) { njDone(); return false; }
    int sw = njGetWidth(), sh = njGetHeight();
    const unsigned char *src = njGetImage();
    int comp = njIsColor() ? 3 : 1;
    if (sw <= 0 || sh <= 0 || !src) { njDone(); return false; }
    int dw = sw, dh = sh;
    if (dw > maxW) { dh = (int)((long)dh * maxW / dw); dw = maxW; }
    if (dh > maxH) { dw = (int)((long)dw * maxH / dh); dh = maxH; }
    if (dw < 1) dw = 1; if (dh < 1) dh = 1;
    unsigned char *px = (unsigned char *)malloc((size_t)dw * dh * 4);
    if (!px) { njDone(); return false; }
    for (int y = 0; y < dh; y++) {
        int sy0 = (int)((long)y * sh / dh), sy1 = (int)((long)(y + 1) * sh / dh);
        if (sy1 <= sy0) sy1 = sy0 + 1; if (sy1 > sh) sy1 = sh;
        unsigned char *o = px + (size_t)y * dw * 4;
        for (int x = 0; x < dw; x++, o += 4) {
            int sx0 = (int)((long)x * sw / dw), sx1 = (int)((long)(x + 1) * sw / dw);
            if (sx1 <= sx0) sx1 = sx0 + 1; if (sx1 > sw) sx1 = sw;
            unsigned r = 0, g = 0, b = 0, cnt = 0;
            for (int yy = sy0; yy < sy1; yy++) {
                const unsigned char *sp = src + ((size_t)yy * sw + sx0) * comp;
                for (int xx = sx0; xx < sx1; xx++, sp += comp) {
                    if (comp == 3) { r += sp[0]; g += sp[1]; b += sp[2]; }
                    else           { r += sp[0]; g += sp[0]; b += sp[0]; }
                    cnt++;
                }
            }
            if (!cnt) cnt = 1;
            o[0] = (unsigned char)(r / cnt); o[1] = (unsigned char)(g / cnt);
            o[2] = (unsigned char)(b / cnt); o[3] = 255;
        }
    }
    njDone();
    out->w = dw; out->h = dh; out->px = px;
    return true;
}

// ======================= the Media provider ==================================
class InprocMedia : public Media {
public:
    explicit InprocMedia(const char *root) : n_(0) { scan(root); }
    ~InprocMedia() {}

    int  count() { return n_; }

    bool info(int i, MediaEntry *e) {
        if (i < 0 || i >= n_) return false;
        Entry &en = ent_[i];
        if (!en.meta) fillMeta(en);
        memset(e, 0, sizeof *e);
        strncpy(e->name, en.name, sizeof e->name - 1);
        strncpy(e->folder, en.folder, sizeof e->folder - 1);
        e->folderNo = en.folderNo; e->fileNo = en.fileNo;
        e->w = en.w; e->h = en.h; e->size = en.size;
        e->year = en.year; e->mon = en.mon; e->day = en.day;
        e->hour = en.hour; e->min = en.min; e->sec = en.sec;
        e->movie = en.movie; e->raw = en.raw; e->prot = false;
        e->durationSec = 0;
        return true;
    }

    // Both grid thumbnail and full preview come from the EXIF thumbnail (RAM-safe); the
    // preview relies on the screen's bilinear upscale. Movies get no bitmap (play badge).
    bool load(int i, int maxW, int maxH, bool /*thumb*/, MediaImage *out) {
        if (i < 0 || i >= n_ || maxW < 4 || maxH < 4 || !out) return false;
        out->px = 0; out->w = out->h = 0;
        Entry &en = ent_[i];
        if (en.movie) return false;

        int fd = open(en.path, O_RDONLY);
        if (fd < 0) return false;
        JpegMeta m; streamJpeg(fd, &m);

        bool ok = false;
        // --- primary: the EXIF embedded thumbnail ---
        if (m.app1 && m.thL > 0 && m.thL <= kThumbMaxBytes &&
            6 + m.thO + m.thL <= (unsigned)m.app1len) {
            const unsigned char *tj = m.app1 + 6 + m.thO;      // TIFF base = app1+6
            if (m.thL >= 3 && tj[0] == 0xFF && tj[1] == 0xD8)
                ok = decodeToRgba(tj, (int)m.thL, maxW, maxH, out);
        }
        // --- fallback: decode the MAIN image, only when provably small ---
        if (!ok && !en.raw) {
            int w = m.exW ? (int)m.exW : m.sofW, h = m.exH ? (int)m.exH : m.sofH;
            long pix = (long)w * h;
            struct stat st; bool haveSz = (fstat(fd, &st) == 0);
            if (w > 0 && pix > 0 && pix <= kMainMaxPix &&
                haveSz && st.st_size > 0 && st.st_size <= kMainMaxFile) {
                unsigned char *full = (unsigned char *)malloc((size_t)st.st_size);
                if (full) {
                    int fn = (int)pread(fd, full, (size_t)st.st_size, 0);
                    if (fn > 4) ok = decodeToRgba(full, fn, maxW, maxH, out);
                    free(full);
                }
            }
        }
        freeMeta(&m);
        close(fd);
        return ok;
    }

    void release(MediaImage *img) { if (img && img->px) { free(img->px); img->px = 0; img->w = img->h = 0; } }

    // remove() deliberately NOT overridden: READ-ONLY (brick-safe). The delete UI stays inert.

private:
    int   n_;
    Entry ent_[kMaxEntries];

    // ---- enumeration ------------------------------------------------------
    static bool isDcfFolder(const char *d, int *folderNo) {
        if (!isdigit((unsigned char)d[0]) || !isdigit((unsigned char)d[1]) || !isdigit((unsigned char)d[2])) return false;
        *folderNo = (d[0] - '0') * 100 + (d[1] - '0') * 10 + (d[2] - '0');
        return true;
    }
    static bool hasExt(const char *name, const char *ext) {
        int ln = (int)strlen(name), le = (int)strlen(ext);
        if (ln < le) return false;
        for (int i = 0; i < le; i++) if (toupper((unsigned char)name[ln - le + i]) != toupper((unsigned char)ext[i])) return false;
        return true;
    }
    static int fileNoOf(const char *name) {
        const char *dot = strrchr(name, '.'); int end = dot ? (int)(dot - name) : (int)strlen(name);
        int i = end; while (i > 0 && isdigit((unsigned char)name[i - 1])) i--;
        int v = 0; for (int k = i; k < end; k++) v = v * 10 + (name[k] - '0');
        return v;
    }
    // filename stem (name without the final extension), lower-cased, for dedup
    static void stemOf(const char *name, char *out, int outN) {
        const char *dot = strrchr(name, '.'); int len = dot ? (int)(dot - name) : (int)strlen(name);
        int k = 0; for (; k < len && k < outN - 1; k++) out[k] = (char)tolower((unsigned char)name[k]);
        out[k] = 0;
    }

    void addFile(const char *folder, int folderNo, const char *fname) {
        if (n_ >= kMaxEntries) return;
        bool jpg = hasExt(fname, ".JPG") || hasExt(fname, ".JPEG");
        bool raw = hasExt(fname, ".ARW");
        bool mov = hasExt(fname, ".MP4") || hasExt(fname, ".MTS") || hasExt(fname, ".MOV") || hasExt(fname, ".M2TS");
        if (!jpg && !raw && !mov) return;
        Entry &e = ent_[n_];
        memset(&e, 0, sizeof e);
        snprintf(e.path, sizeof e.path, "%s/%s", folder, fname);
        strncpy(e.name, fname, sizeof e.name - 1);
        strncpy(e.folder, strrchr(folder, '/') ? strrchr(folder, '/') + 1 : folder, sizeof e.folder - 1);
        e.folderNo = folderNo; e.fileNo = fileNoOf(fname);
        e.movie = mov; e.raw = raw;
        n_++;
    }

    void scanFolder(const char *folderPath, int folderNo) {
        DIR *d = opendir(folderPath);
        if (!d) return;
        struct dirent *de;
        while ((de = readdir(d)) && n_ < kMaxEntries) {
            if (de->d_name[0] == '.') continue;
            addFile(folderPath, folderNo, de->d_name);
        }
        closedir(d);
    }

    void scan(const char *root) {
        DIR *d = opendir(root);
        if (!d) { uicore::logf("media: DCIM root '%s' not present (empty gallery)", root); return; }
        struct dirent *de;
        while ((de = readdir(d))) {
            if (de->d_name[0] == '.') continue;
            int fno = 0;
            if (!isDcfFolder(de->d_name, &fno)) continue;
            char sub[256]; snprintf(sub, sizeof sub, "%s/%s", root, de->d_name);
            struct stat st;
            if (stat(sub, &st) == 0 && S_ISDIR(st.st_mode)) scanFolder(sub, fno);
        }
        closedir(d);
        int hidden = dedupRawJpeg();
        sortByDcf();
        uicore::logf("media: DCIM '%s' -> %d item(s) (%d ARW hidden by JPG), oldest-first", root, liveCount(), hidden);
    }

    // RAW+JPEG dedup: mark an ARW dead when a JPG with the same folder+stem exists.
    int dedupRawJpeg() {
        int hidden = 0;
        for (int i = 0; i < n_; i++) {
            if (!ent_[i].raw || ent_[i].dead) continue;
            char si[64]; stemOf(ent_[i].name, si, sizeof si);
            for (int j = 0; j < n_; j++) {
                if (j == i || ent_[j].raw || ent_[j].movie) continue;
                if (ent_[j].folderNo != ent_[i].folderNo) continue;
                char sj[64]; stemOf(ent_[j].name, sj, sizeof sj);
                if (!strcmp(si, sj)) { ent_[i].dead = true; hidden++; break; }
            }
        }
        // compact out the dead ARW entries
        int w = 0;
        for (int i = 0; i < n_; i++) if (!ent_[i].dead) ent_[w++] = ent_[i];
        n_ = w;
        return hidden;
    }
    int liveCount() const { return n_; }

    void sortByDcf() {
        for (int i = 1; i < n_; i++) {
            Entry tmp = ent_[i]; int j = i - 1;
            long ki = (long)tmp.folderNo * 100000 + tmp.fileNo;
            while (j >= 0 && (long)ent_[j].folderNo * 100000 + ent_[j].fileNo > ki) { ent_[j + 1] = ent_[j]; j--; }
            ent_[j + 1] = tmp;
        }
    }

    // ---- lazy metadata (dims + date + size) -------------------------------
    void fillMeta(Entry &e) {
        e.meta = true;
        e.w = e.h = 0; e.size = 0;
        e.year = 2026; e.mon = 1; e.day = 1; e.hour = e.min = e.sec = 0;
        struct stat st;
        if (stat(e.path, &st) == 0) e.size = (unsigned)st.st_size;
        if (e.movie) return;
        int fd = open(e.path, O_RDONLY);
        if (fd < 0) return;
        JpegMeta m; streamJpeg(fd, &m);
        int w = m.exW ? (int)m.exW : m.sofW, h = m.exH ? (int)m.exH : m.sofH;
        if (w > 0) { e.w = w; e.h = h; }
        if (m.date[0]) {
            int Y, M, D, hh, mm, ss;
            if (sscanf(m.date, "%d:%d:%d %d:%d:%d", &Y, &M, &D, &hh, &mm, &ss) == 6) {
                e.year = Y; e.mon = M; e.day = D; e.hour = hh; e.min = mm; e.sec = ss;
            }
        }
        freeMeta(&m);
        close(fd);
    }
};

// resolve the DCF root: explicit env, else the real card mount, else the emulator drop.
static const char *resolveDcimRoot() {
    static char out[256];
    const char *env = getenv("A5K_UI_DCIM");
    if (env && *env) { strncpy(out, env, sizeof out - 1); out[sizeof out - 1] = 0; return out; }
    static const char *cands[] = { "/android/mnt/sdcard/DCIM", "/android/a5k/DCIM" };
    for (unsigned i = 0; i < sizeof cands / sizeof cands[0]; i++) {
        DIR *d = opendir(cands[i]);
        if (d) { closedir(d); return cands[i]; }
    }
    return cands[0];
}

} // namespace

namespace a5kinproc {
uicore::Media *createInprocMedia() { return new InprocMedia(resolveDcimRoot()); }
}
