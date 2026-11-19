// SurfaceFlinger 네이티브 클라이언트 시험 (Android 2.3.7, bionic)
// 전체 화면 RGBA surface를 최상위 레이어에 띄우고 GLES2로 반투명 색을 <sec>초 동안 그린 뒤 종료.
// 사용법(카메라, chroot /android 안): sftest [sec] [alpha(0~255)] [gl|sw]
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <binder/IPCThreadState.h>
#include <binder/ProcessState.h>
#include <surfaceflinger/Surface.h>
#include <surfaceflinger/SurfaceComposerClient.h>
#include <ui/DisplayInfo.h>
#include <ui/PixelFormat.h>

using namespace android;

static volatile int g_stop = 0;
static void onSig(int) { g_stop = 1; }

int main(int argc, char **argv) {
    int seconds = argc > 1 ? atoi(argv[1]) : 5;
    float alpha = (argc > 2 ? atoi(argv[2]) : 128) / 255.0f;
    signal(SIGINT, onSig);
    signal(SIGTERM, onSig);
    signal(SIGHUP, onSig);
    setvbuf(stdout, NULL, _IOLBF, 0);

    sp<ProcessState> proc(ProcessState::self());
    ProcessState::self()->startThreadPool();

    sp<SurfaceComposerClient> client = new SurfaceComposerClient();
    printf("client initCheck=%d\n", client->initCheck());
    DisplayInfo info;
    status_t st = SurfaceComposerClient::getDisplayInfo(0, &info);
    printf("display: st=%d %ux%u fmt=%d density=%f fps=%f\n", st, info.w, info.h, info.pixelFormatInfo.format,
           info.density, info.fps);
    if (st != 0 || info.w == 0) return 1;

    sp<SurfaceControl> ctl = client->createSurface(getpid(), 0, info.w, info.h, PIXEL_FORMAT_RGBA_8888);
    if (ctl == 0) { printf("createSurface failed\n"); return 1; }
    SurfaceComposerClient::openGlobalTransaction();
    ctl->setLayer(0x40000000);
    ctl->show();
    SurfaceComposerClient::closeGlobalTransaction();
    sp<Surface> surface = ctl->getSurface();
    printf("surface=%p\n", surface.get());

    if (argc > 3 && argv[3][0] == 's') {   // 소프트웨어 경로: lock → 픽셀 채움 → unlockAndPost
        unsigned a8 = (unsigned)(alpha * 255.0f);
        time_t end2 = time(NULL) + seconds;
        int f = 0;
        while (!g_stop && time(NULL) < end2) {
            Surface::SurfaceInfo si;
            status_t ls = surface->lock(&si);
            if (ls != 0) { printf("lock failed %d\n", ls); break; }
            unsigned g = (f * 8) & 0xff;
            // RGBA_8888 (메모리 순서 R,G,B,A), premultiplied
            unsigned px = ((a8 * 255 / 255) & 0xff) | (((g * a8 / 255) & 0xff) << 8) | (0u << 16) | (a8 << 24);
            uint32_t *p = (uint32_t *)si.bits;
            for (uint32_t y = 0; y < si.h; y++)
                for (uint32_t x = 0; x < si.w; x++) p[y * si.s + x] = (x < 20 || y < 20 || x >= si.w - 20 || y >= si.h - 20) ? 0xff00ff00u : px;
            status_t ps = surface->unlockAndPost();
            if (f < 3) printf("sw frame#%d %ux%u s=%u fmt=%d post=%d\n", f, si.w, si.h, si.s, si.format, ps);
            f++;
        }
        printf("sw frames=%d\n", f);
        SurfaceComposerClient::openGlobalTransaction();
        ctl->hide();
        SurfaceComposerClient::closeGlobalTransaction();
        ctl.clear();
        client->dispose();
        printf("done\n");
        return 0;
    }

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    eglInitialize(dpy, &major, &minor);
    printf("EGL %d.%d vendor=%s\n", major, minor, eglQueryString(dpy, EGL_VENDOR));
    const EGLint cfgAttr[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                               EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE };
    EGLConfig cfg;
    EGLint n = 0;
    eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n);
    printf("eglChooseConfig n=%d\n", n);
    if (n < 1) return 1;
    EGLSurface es = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)surface.get(), NULL);
    const EGLint ctxAttr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
    printf("surface=%p ctx=%p err=0x%x\n", es, ctx, eglGetError());
    if (es == EGL_NO_SURFACE || ctx == EGL_NO_CONTEXT) return 1;
    eglMakeCurrent(dpy, es, es, ctx);
    printf("GL_RENDERER=%s GL_VERSION=%s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

    time_t end = time(NULL) + seconds;
    int frames = 0;
    while (!g_stop && time(NULL) < end) {
        float t = (frames % 60) / 60.0f;
        glClearColor(1.0f, t, 0.0f, alpha);   // 빨강→노랑 반복, 반투명
        glClear(GL_COLOR_BUFFER_BIT);
        EGLBoolean ok = eglSwapBuffers(dpy, es);
        if (frames < 3 || !ok) {
            printf("swap#%d ok=%d err=0x%x glErr=0x%x\n", frames, ok, eglGetError(), glGetError());
            if (!ok && frames > 5) break;
        }
        frames++;
    }
    printf("frames=%d\n", frames);

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    eglDestroySurface(dpy, es);
    eglTerminate(dpy);
    SurfaceComposerClient::openGlobalTransaction();
    ctl->hide();
    SurfaceComposerClient::closeGlobalTransaction();
    ctl.clear();
    client->dispose();
    printf("done\n");
    return 0;
}
