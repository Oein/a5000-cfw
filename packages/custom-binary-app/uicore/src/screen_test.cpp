// M0 test screen: Sony-styled panel showing recent ACTIONS and the CameraControl
// info values, plus a logical-space (853->640 squeeze) reference. Consumes only
// abstract actions, so it is identical on a5000, host and (future) a6400.
#include "uicore/screen.h"
#include "uicore/app.h"
#include "uicore/canvas.h"
#include <stdio.h>

namespace uicore {
namespace {

class TestScreen : public Screen {
public:
    const char *title() const { return "A5KUI M0 TEST"; }
    Context context() const { return CTX_MENU; }     // wheel = NAV here
    void onAction(Action, int, App &) {}              // App keeps the action log

    void draw(Canvas &c, App &app) {
        c.clear(rgba(0, 0, 0, 0));                    // transparent: live view shows through

        Color panel = rgba(20, 24, 30, 230);
        c.fillRect(0, 0, PANEL_W, 40, panel);
        c.fillRect(0, 40, PANEL_W, 4, sonyOrange());
        c.text(12, 10, 3, app.opt().allowWrite ? "A5KUI M0  *WRITE" : "A5KUI M0", rgb(255, 255, 255));

        char buf[96];
        snprintf(buf, sizeof buf, "DISP %s  FRAME %u", app.displayName(), app.frame());
        c.text(410, 14, 1, buf, rgb(180, 180, 180));

        // Left: recent actions.
        int y = 60;
        c.text(12, y, 2, "ACTIONS", sonyOrange()); y += 24;
        int n = app.actionLogCount();
        const ActionLog *al = app.actionLog();
        int head = app.actionLogHead();
        if (n == 0) { c.text(12, y, 2, "(PRESS A KEY)", rgb(120, 120, 120)); y += 20; }
        for (int i = 0; i < n; i++) {
            int idx = (head - n + i) % 10; if (idx < 0) idx += 10;
            const ActionLog &e = al[idx];
            snprintf(buf, sizeof buf, "%-13s r%-3d %s", action_name(e.action), e.raw,
                     e.value == 1 ? "DN" : (e.value == 0 ? "UP" : "RP"));
            c.text(12, y, 2, buf, e.value == 1 ? rgb(255, 255, 255) : rgb(150, 150, 150));
            y += 20;
        }

        // Right: CameraControl info.
        int rx = 360, ry = 60;
        c.text(rx, ry, 2, "CAMERA", sonyOrange()); ry += 24;
        snprintf(buf, sizeof buf, "CONNECT %s%s", app.cameraConnected() ? "YES" : "NO",
                 app.cameraDryrun() ? " (DRY)" : "");
        c.text(rx, ry, 2, buf, app.cameraConnected() ? rgb(80, 220, 80) : rgb(220, 120, 80)); ry += 24;
        for (int i = 0; i < app.infoCount(); i++) {
            snprintf(buf, sizeof buf, "ID %-3d = 0x%X", app.infoIds()[i], app.infoVals()[i]);
            c.text(rx, ry, 2, buf, rgb(200, 200, 200));
            ry += 20;
        }

        // Bottom: logical-space squeeze reference.
        int by = 420;
        c.lFillRect(0, by, LOGICAL_W, 2, rgb(90, 90, 90));
        c.lFillRect(0, by, 100, 20, sonyOrange());
        c.lText(110, by + 2, 2, "LOGICAL 853 -> 640 (X0.75)", rgb(200, 200, 200));
        c.lFillRect(LOGICAL_W - 100, by, 100, 20, sonyOrange());

        c.text(12, 458, 1, "MENU/PLAY LEAVES APP  |  a5kctl key N / a5kctl shot", rgb(140, 140, 140));
    }
};

} // namespace

Screen *createTestScreen() { return new TestScreen(); }

} // namespace uicore
