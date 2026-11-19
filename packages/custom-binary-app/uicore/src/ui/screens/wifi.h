// Wi-Fi settings / connection screen (own design). Pages: MASTER (on/off + mode), STA scan list,
// STA saved-networks manager, AP (smartphone) page. Entered from the wireless menu group.
#pragma once

namespace uicore {

class App;

enum WifiPage { WP_MASTER = 0, WP_STA, WP_SAVED, WP_AP };
enum { WP_AUTOSTART = 0x100 };      // createWifiScreen(page | WP_AUTOSTART)

// Menu hook: does this (key,label) wireless row open the Wi-Fi screen? Fills *page and *autoStart
// (turn the radio on in the matching mode when entering = "send to smartphone" / "PC" intent).
bool wifiMenuEntry(const char *key, const char *label, int *page, bool *autoStart);

// Shared with the NFC one-touch flow: the AP credentials (text-input fields wifi.ssid / wifi.psk) and
// "radio on + AP mode + AP started with them" (= what the smartphone page does on entry). False = no Wi-Fi backend.
void wifiApCredentials(const char **ssid, const char **password);
bool wifiStartPhoneAp(App &app);

} // namespace uicore
