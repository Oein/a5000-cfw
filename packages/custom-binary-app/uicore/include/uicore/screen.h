// Screen interface. Screens consume ACTIONS (never physical keys) and declare the
// input context they run in (so the platform keymap can pick wheel=NAV vs exposure).
#pragma once
#include "uicore/action.h"

namespace uicore {

class Canvas;
class App;

class Screen {
public:
    virtual ~Screen() {}
    virtual void onAction(Action a, int value, App &app) = 0;
    virtual void draw(Canvas &c, App &app)               = 0;
    virtual Context context() const { return CTX_MENU; }
    virtual const char *title() const                    = 0;
};

Screen *createTestScreen();
Screen *createShootingScreen();
Screen *createPlaybackScreen();     // M3 playback / gallery (src/ui/screens/playback.cpp)
Screen *createWifiScreen(int page); // Wi-Fi settings/connection (src/ui/screens/wifi.cpp); page = WifiPage in screens/wifi.h
Screen *createNfcScreen();          // NFC one-touch connect (src/ui/screens/nfc.cpp)
void    nfcService(App &app);       // per-frame: a phone tap opens the one-touch screen from anywhere
Screen *createMenuScreen();         // M2 cascade main menu (src/ui/screens/menu_cascade.cpp)     // M1 shooting OSD (src/ui/screens/shooting_osd.cpp)
Screen *createKeyConfigScreen();    // Custom Key settings (src/ui/screens/key_config.cpp)

} // namespace uicore
