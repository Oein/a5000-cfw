// NFC one-touch connect (own design). A phone tap on the N-mark is a SHORTCUT into the existing Wi-Fi AP
// flow (wifi.cpp wifiStartPhoneAp): tap -> Wi-Fi on + AP mode + AP up with the credentials the tag carries
// -> phone joins -> connected (OSD icon = AP via CameraModel::syncWifi). Entered from the wireless menu
// ("원터치(NFC)") or opened automatically by nfcService() when a phone reaches the antenna.
#pragma once

namespace uicore {

// Menu hook: is this (key,label) the one-touch (NFC) row?
bool nfcMenuEntry(const char *key, const char *label);

} // namespace uicore
