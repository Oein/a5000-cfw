// a5000 route-(b) in-process entry: THE custom uicore UI running inside im.elf.
//
// Loaded into im.elf by the stock loader via /setting/env.txt LD_PRELOAD
// (TRACKB_INPROCESS.md §17, a5kpreload path -- NO ptrace, every cold boot,
// reversible by deleting the /setting files). The constructor self-gates to
// im.elf (the preload maps into every init child) and spawns a detached worker
// so the engine's main thread is never blocked.
//
// The worker wires uicore's platform-agnostic App to the in-process backends:
//   Display  = OSD double-buffer blit (display_osd.cpp, RGBA8888 -> RGBA4444)
//   Input    = /dev/input/event0 (input_event0.cpp) + a5000 keymap (data)
//   Camera   = getModelInfo READ / aspect-vtable SET (camera_inproc.cpp)
//   Clock/Log= generic libc (reused a5000 sys)
// then suppresses the stock UI (gate 2b, §15, detach-reversible) so OUR OSD owns
// the panel, and runs the uicore loop. On loop end it restores the stock gate.
//
// RAM-only. No flash writes. Reboot (or deleting the /setting files) restores
// pure stock -> brick risk 0, recovery invariant intact.
#include "uicore/app.h"
#include "uicore/platform.h"
#include "uicore/log.h"
#include "engine.h"

#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

// ---- backend factories (this backend) ----
namespace a5kinproc {
void runApplyTest(uicore::CameraControl *cam, Engine *eng, int aspProbe);
void runFocusTest(uicore::CameraControl *cam, Engine *eng);   // focus SET self-test (marker /android/a5k/focustest)
uicore::Display       *createOsdDisplay();
uicore::Display       *createRigelDisplay(Engine *eng);       // A5K_UI_RIGEL=1: Rigel/GPU backend (fail-safe)
uicore::Display       *createVdfDisplay(Engine *eng);         // A5K_UI_VDF=1: stock VDF OSD present (sub_5D99D0 replica)
uicore::Display       *createFbcDisplay(Engine *eng);         // A5K_UI_FBC=1: stock FbContainer/UpdateManager OSD present (general path)
void                   rigelVdfProbe(Engine *eng);            // RE probe: dump stock FbContainer->VDF OSD state
void                   handToStock_end();                     // force-resume our overlay if we quit mid hand-off
void                   stockShooting_begin(Engine *eng);      // route-b: hand shooting (live-view/OSD+input) to stock
void                   stockShooting_end();                   // route-b: re-engage our UI (menu)
uicore::Input         *createCreateEventInput(Engine *eng);   // §21 engine-input hook
uicore::CameraControl *createInprocCamera(Engine *eng);
void                   startDebugChannel(uicore::CameraControl *cam, Engine *eng);   // no-op unless A5K_DEBUG
uicore::Media         *createInprocMedia();                   // M3 playback: DCF card gallery (READ-ONLY)
}
// ---- reused a5000 platform pieces (data + generic sys) ----
namespace a5000 {
const uicore::Binding *bindings(int *count);
uicore::Clock *createClock();
uicore::Log   *createLog(const char *path);
uicore::ShootKeymapStore *createKeymapStore(const char *path);
}

namespace {

// Production log path. /android/<...> is the RAM tmpfs that s_start_android.sh
// mounts; it is RAM-only (no flash wear, brick-safe) and -- unlike /setting/a5k,
// whose directory does not exist unless the autostart created it -- is reliably
// writable from im.elf for BOTH the LD_PRELOAD and the manual-ptrace-inject path
// (the minimal inj.so proved /android/a5k writable on device + emulator). The
// worker mkdir()s the directory before opening, so a bare manual inject also logs.
const char *LOG_PATH   = "/android/a5k/ui.log";
const char *LOG_DIR    = "/android/a5k";

int env_int(const char *name, int def) {
    const char *v = getenv(name);
    if (!v || !*v) return def;
    return (int)strtol(v, 0, 0);
}

// Resolve the baked-asset directory WITHOUT relying on env. The manual ptrace-inject
// path (the real-device deploy) does not propagate env into im.elf, so A5K_UI_ASSETS
// is typically unset there; without a default the UI silently falls back to the built-in
// 5x7 font. Probe A5K_UI_ASSETS first, then the two fixed on-device locations (the flash
// /setting/a5k dir the autostart uses, and the RAM tmpfs the inject path can push to),
// using icons.bin as the marker. Returns a static path or 0 (then uicore uses the 5x7
// fallback, which is now X0.75 pre-squeezed so it still fits the panel).
const char *resolve_assets_dir() {
    static const char *cands[] = {
        0,                          // [0] replaced by env below
        "/setting/a5k/assets",
        "/android/a5k/assets",
    };
    cands[0] = getenv("A5K_UI_ASSETS");
    for (unsigned i = 0; i < sizeof cands / sizeof cands[0]; i++) {
        const char *d = cands[i];
        if (!d || !*d) continue;
        char marker[256];
        snprintf(marker, sizeof marker, "%s/icons.bin", d);
        if (access(marker, R_OK) == 0) return cands[i];
    }
    return 0;
}

void comm_of_self(char *o, int n) {
    o[0] = 0;
    /* process (main-thread) comm, so manual ptrace-inject into any thread also
     * self-gates correctly (not the hijacked thread's name). */
    char path[32]; snprintf(path, sizeof path, "/proc/%d/comm", (int)getpid());
    int fd = open(path, O_RDONLY); if (fd < 0) return;
    int r = read(fd, o, n - 1); close(fd); if (r < 0) r = 0;
    while (r && (o[r-1] == '\n' || o[r-1] == ' ')) r--;
    o[r] = 0;
}

void wait_osd_on() {
    for (int i = 0; i < 1500; i++) {            // up to ~150 s
        int s = a5kinproc::osd_on();
        if (s == 1) return;
        if (s < 0 && i > 100) return;
        struct timespec t = { 0, 100 * 1000000L }; nanosleep(&t, 0);
    }
}

// route-b stock-shooting passthrough hook: the uicore App calls this (plat.setStockShooting)
// when the screen stack enters (on=true -> stock OSD + input) / leaves (on=false -> our UI) the
// shooting root. Bridges to the platform hand-to-stock primitive with the resolved engine.
static a5kinproc::Engine *g_hookEng = 0;
static void setStockShootingHook(bool on) {
    if (!g_hookEng) return;
    if (on) a5kinproc::stockShooting_begin(g_hookEng);
    else    a5kinproc::stockShooting_end();
}

void *worker(void *) {
    wait_osd_on();

    // Resolve im.elf internals (libObj / viewUnified2 bases, per-boot).
    static a5kinproc::Engine eng;
    for (int i = 0; i < 20 && !eng.resolve(); i++) {
        struct timespec t = { 0, 200 * 1000000L }; nanosleep(&t, 0);
    }
    mkdir("/android", 0777);        // RAM tmpfs; harmless (EEXIST) if already there
    mkdir(LOG_DIR, 0777);           // ensure the log dir exists for a bare manual inject
    uicore::Log *log = a5000::createLog(LOG_PATH);
    uicore::log_set(log);
    if (!eng.ok()) { uicore::logf("inproc: engine symbols NOT resolved -> abort (stock untouched)"); return 0; }
    uicore::logf("inproc: engine resolved libObj=0x%lx vu=0x%lx", eng.libObj(), eng.vu());

    // RE probe (file-gated; writes persistent /setting/rprobe.log). Read-only dump
    // of the stock FbContainer -> VDF OSD scanout state, for the GPU/VDF present
    // RE. Does not touch the UI; returns to normal flow after.
    // Trigger: the tmpfs marker /android/a5k/rprobe, OR A5K_UI_PROBE=1 in env.txt
    // (persistent through the autostart reboot, where tmpfs markers do not survive).
    // The gen_struct_for_update call inside is further gated by /android/a5k/rprobe2
    // or A5K_UI_PROBE=2 (it can _assert_fail->abort if the element BMP is invalid).
    if (access("/android/a5k/rprobe", F_OK) == 0 || env_int("A5K_UI_PROBE", 0) >= 1) {
        uicore::logf("inproc: VDF/FbContainer probe (marker or A5K_UI_PROBE)");
        a5kinproc::rigelVdfProbe(&eng);
    }

    // Optional visible+reversible SET self-test (§18 image aspect). Off by default
    // (it changes live engine state); enable with A5K_UI_SETTEST=1.
    if (env_int("A5K_UI_SETTEST", 0)) {
        int cur = -1;
        if (eng.cvpdmRead(&cur) && cur > 0) {
            int nv = (cur == 2) ? 4 : 2;
            uicore::logf("inproc: aspect self-test %d -> %d", cur, nv);
            eng.cvpdmWrite(nv);
            struct timespec t = { 2, 0 }; nanosleep(&t, 0);
            eng.cvpdmWrite(cur);            // revert
            uicore::logf("inproc: aspect self-test reverted to %d", cur);
        } else uicore::logf("inproc: aspect self-test skipped (global not bound)");
    }

    // Build the platform bundle.
    uicore::Platform plat;
    memset(&plat, 0, sizeof plat);
    // Display backend select (A5K_UI_RIGEL=1 -> Rigel/GPU compositor backend,
    // display_rigel.cpp; default/fallback -> software OSD blit, display_osd.cpp).
    // The Rigel backend is init()'d HERE so a failure (expected on stock fw --
    // see display_rigel.cpp: the in-proc libObj client cannot register an overlay
    // window) cleanly falls back to software with zero regression. The software
    // backend is init()'d below by the platform-main block (displayInited_ guards
    // against a double init of a backend already brought up here).
    bool displayInited = false;
    // A5K_UI_RIGEL=1 env, OR the /android/a5k/rigel marker file (the manual
    // ptrace-inject path does not propagate env into im.elf, same reason
    // resolve_assets_dir() probes fixed dirs -- the file lets the Rigel backend
    // be exercised by a bare inject on emulator or device).
    bool wantRigel = env_int("A5K_UI_RIGEL", 0) || access("/android/a5k/rigel", F_OK) == 0;
    if (wantRigel) {
        uicore::Display *rd = a5kinproc::createRigelDisplay(&eng);
        if (rd && rd->init()) {
            plat.display = rd;
            displayInited = true;
            uicore::logf("inproc: display backend = %s (Rigel requested)", rd->name());
        } else {
            if (rd) { rd->shutdown(); delete rd; }
            uicore::logf("inproc: Rigel requested but init failed -> software OSD fallback");
        }
    }
    // VDF OSD present backend -- DETOUR-AND-SWAP (the DEFAULT route-b present path):
    // utilGetMemory -> own 16bpp OSD buffer; present() fills it; a hook on the stock
    // vdf_if_port_send_update swaps the panel-OSD record's phys to our buffer so the
    // STOCK submit (correct OSD-pin context) carries our pixels -- VDF composites them
    // over the live-view YUV plane. DEFAULT ON (A5K_UI_VDF defaults to 1); force the
    // old software /dev/mem OSD blit with A5K_UI_SOFT=1 or the /android/a5k/soft
    // marker (comparison/safety). On ANY VDF init failure it cleanly falls back to
    // the software blit below (zero regression, brick risk 0). NOTE: double-buffering
    // is not applied yet, so fast value changes may tear -- known limitation.
    bool softForce = env_int("A5K_UI_SOFT", 0) || access("/android/a5k/soft", F_OK) == 0;
    bool wantVdf = !plat.display && !softForce && env_int("A5K_UI_VDF", 1) != 0;
    if (wantVdf) {
        // NOTE: VDF detour-and-swap MODE must NOT suppress stock draw -- the hook
        // only fires while the stock OSD present keeps submitting (we swap its
        // record's buffer to ours). So no suppressStock here, and the later
        // suppression block is skipped for the vdf-inproc backend.
        uicore::Display *vd = a5kinproc::createVdfDisplay(&eng);
        if (vd && vd->init()) {
            plat.display = vd;
            displayInited = true;
            uicore::logf("inproc: display backend = %s (VDF requested)", vd->name());
        } else {
            if (vd) { vd->shutdown(); delete vd; }
            uicore::logf("inproc: VDF requested but init failed -> software OSD fallback");
        }
    }
    // FbContainer/UpdateManager OSD present backend (the GENERAL stock OSD path:
    // FbContainer panel element -> gen_struct_for_update -> UpdateManager::send).
    // Gated by A5K_UI_FBC / /android/a5k/fbc; engages only when the stock panel
    // compositor has a bound buffer (real-device/live-view), else software fallback.
    bool wantFbc = !plat.display &&
                   (env_int("A5K_UI_FBC", 0) || access("/android/a5k/fbc", F_OK) == 0);
    if (wantFbc) {
        if (eng.ok() && eng.suppressStock())
            uicore::logf("inproc: stock-draw suppressed for FBC backend");
        uicore::Display *fd = a5kinproc::createFbcDisplay(&eng);
        if (fd && fd->init()) {
            plat.display = fd;
            displayInited = true;
            uicore::logf("inproc: display backend = %s (FBC requested)", fd->name());
        } else {
            if (fd) { fd->shutdown(); delete fd; }
            eng.restoreStock();
            uicore::logf("inproc: FBC requested but init failed -> software OSD fallback");
        }
    }
    if (!plat.display) {
        plat.display = a5kinproc::createOsdDisplay();
        uicore::logf("inproc: display backend = %s", plat.display->name());
    }
    plat.input   = a5kinproc::createCreateEventInput(&eng);   // live keys via createEvent hook
    plat.camera  = a5kinproc::createInprocCamera(&eng);
    plat.media   = a5kinproc::createInprocMedia();            // M3 playback gallery (DCF card, READ-ONLY)

    // Optional apply-path self-test (A5K_UI_APPLYTEST=1): drives the generic applySetting() path
    // (uicore/apply.h conversion table) and logs result + engine read-back. Reversible, RAM-only.
    // A5K_UI_ASPPROBE=<v> additionally writes that raw internal aspect value (probe only), then reverts.
    if (env_int("A5K_UI_APPLYTEST", 0)) a5kinproc::runApplyTest(plat.camera, &eng, env_int("A5K_UI_ASPPROBE", 0));
    // Focus-mode SET self-test (issue-(1) device proof): toggles AF<->MF via setSetting and
    // logs the group-13 read-back, then reverts. Env OR the tmpfs marker (inject path has no
    // env), so a bare manual inject can prove the SET->engine route. RAM-only, reversible.
    if (env_int("A5K_UI_FOCUSTEST", 0) || access("/android/a5k/focustest", F_OK) == 0)
        a5kinproc::runFocusTest(plat.camera, &eng);
    a5kinproc::startDebugChannel(plat.camera, &eng);           // stub unless built with DEBUG=1
    plat.clock   = a5000::createClock();
    plat.log     = log;
    plat.storage = 0;
    plat.keymapStore = a5000::createKeymapStore("/setting/a5k/keymap.conf");   // persistent Custom Key map (brick-0)
    plat.bindings = a5000::bindings(&plat.bindingCount);
    // Stock-shooting passthrough is OPT-IN (env A5K_STOCK_SHOOTING or /android/a5k/stock_shooting):
    // default = OUR UI draws both the shooting OSD and the menu (user: "osd도 우리껄 써"), made
    // responsive by Path A (A5K_VDF_PATHA / vdf_patha marker). With the marker set instead,
    // shooting hands off to the stock Sony OSD and our UI shows only for the MENU.
    g_hookEng = &eng;
    bool wantStockShooting = env_int("A5K_STOCK_SHOOTING", 0) ||
                             access("/android/a5k/stock_shooting", F_OK) == 0;
    plat.setStockShooting = wantStockShooting ? &setStockShootingHook : 0;
    uicore::logf("inproc: shooting OSD = %s", wantStockShooting ? "STOCK (passthrough; our UI = menu only)" : "OURS (Path A full-rate)");

    // Platform main owns display/input init (App::init only drives camera).
    // Skip if the backend was already brought up by the A5K_UI_RIGEL select above.
    if (!displayInited && !plat.display->init()) { uicore::logf("inproc: display init FAILED -> abort"); return 0; }
    plat.input->init();

    uicore::Options opt;
    memset(&opt, 0, sizeof opt);
    opt.shotPath   = "/setting/a5k/ui_shot.ppm";
    opt.allowWrite = false;
    opt.frameMs    = env_int("A5K_UI_FRAME_MS", 66);   // ~15 fps
    opt.runSeconds = env_int("A5K_UI_SECS", 0);        // 0 = until reboot
    opt.assetsDir  = resolve_assets_dir();             // env OR fixed on-device dirs (no env needed)
    uicore::logf("inproc: assets dir = %s", opt.assetsDir ? opt.assetsDir : "(none -> 5x7 fallback)");
    opt.testScreen = env_int("A5K_UI_TEST", 0) != 0;

    static uicore::App app;
    if (!app.init(opt, plat)) { uicore::logf("inproc: app init FAILED"); return 0; }

    // Own the panel: suppress the stock UI draw (reversible gate 2b). Suppression
    // is ON by default -- it MUST be, because env vars do not propagate to im.elf
    // via ptrace inject, so the production default must stand on its own.
    // A5K_UI_NOSUPPRESS=1 skips it (diagnostic, LD_PRELOAD path only).
    bool vdfBackend = plat.display && plat.display->name() &&
                      strcmp(plat.display->name(), "vdf-inproc") == 0;
    if (vdfBackend) {
        uicore::logf("inproc: suppression SKIPPED (VDF detour-and-swap needs stock present firing)");
    } else if (env_int("A5K_UI_NOSUPPRESS", 0)) {
        uicore::logf("inproc: suppression SKIPPED (A5K_UI_NOSUPPRESS=1)");
    } else if (eng.suppressStock()) uicore::logf("inproc: stock draw suppressed (gate 2b @0x66D442)");
    else                     uicore::logf("inproc: stock suppression FAILED (UI still drawn over stock)");

    uicore::logf("inproc: uicore UI running (frameMs=%d secs=%d assets=%s)",
                 opt.frameMs, opt.runSeconds, opt.assetsDir ? opt.assetsDir : "builtin");

    // Diagnostic: open the playback gallery at startup (A5K_UI_PLAYBACK=1). Mirrors the
    // shooting-OSD ACT_PLAYBACK route (pushScreen(createPlaybackScreen())) without relying
    // on the emulator PLAY-key path, so the M3 gallery can be screendump-verified. Off by
    // default; production opens playback from the real PLAY button.
    if (env_int("A5K_UI_PLAYBACK", 0)) {
        app.injectAction(uicore::ACT_PLAYBACK, 1);
        uicore::logf("inproc: A5K_UI_PLAYBACK=1 -> opened playback gallery");
    }
    app.run();

    // Loop ended (runSeconds OR the /android/a5k/stop teardown file) -> restore the
    // stock UI + input hook (reversibility). Remove the stop file so a freshly
    // injected a5kinproc.so is not immediately torn down by a stale flag.
    a5kinproc::handToStock_end();             // force-resume if we quit mid hand-off (restores present state)
    eng.restoreStock();
    if (plat.input) plat.input->shutdown();   // unpatch createEvent hook
    unlink("/android/a5k/stop");
    uicore::logf("inproc: loop ended, stock gate + input hook restored (stop file cleared)");
    return 0;
}

} // namespace

__attribute__((constructor))
static void a5kinproc_ctor(void) {
    char comm[32]; comm_of_self(comm, sizeof comm);
    if (strcmp(comm, "im.elf") != 0) return;     // inert everywhere but im.elf
    pthread_t t;
    if (pthread_create(&t, 0, worker, 0) == 0) pthread_detach(t);
}
