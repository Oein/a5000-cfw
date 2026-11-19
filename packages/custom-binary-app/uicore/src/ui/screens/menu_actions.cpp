#include "menu_actions.h"
#include "uicore/app.h"
#include "uicore/platform.h"
#include "uicore/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace uicore {
namespace {

// ---- the non-setting catalog -> stock native path map (re/menu/app_launch_and_actions.md s3) ----
// Matched by (key,label) because the generated catalog reuses one key across several rows. Only the
// rows whose stock path is a concrete openView / model-exec / app hand-off are wired; pure ENUM/RANGE
// rows (USB-LUN, HDMI res, language, folder-name, PAL/NTSC) stay in the settings domain (apply.cpp).
//
//   kind           sysAction        arg                        title / confirm
struct Row { const char *key, *label; MenuActionDesc d; };
const Row kRows[] = {
    // ---- APP LAUNCH (Application tab) -- Android world hand-off (s1) ----
    // Row 1 of the 43-item map. Primary = Sony launcher (ScalarALauncher); the direct OM-Tweak
    // hop is the device-seal direct path inside the platform executor.
    { "info-apps", "\xec\x95\xa0\xed\x94\x8c\xeb\xa6\xac\xec\xbc\x80\xec\x9d\xb4\xec\x85\x98 \xeb\xaa\xa9\xeb\xa1\x9d",   // "애플리케이션 목록"
      { MAK_APP_LAUNCH, SYS_LAUNCH_APP, 0,
        "\xec\x95\xa0\xed\x94\x8c\xeb\xa6\xac\xec\xbc\x80\xec\x9d\xb4\xec\x85\x98", 0 } },             // "애플리케이션"

    // ---- SAFE ACTIONS (direct trigger) ----
    // Airplane mode (map #3): stock requestModelExecute toggle (ui_libInfraAirplaneMode).
    { "sys-airplane", "\xeb\xb9\x84\xed\x96\x89\xea\xb8\xb0 \xeb\xaa\xa8\xeb\x93\x9c",                 // "비행기 모드"
      { MAK_MODEL_TOGGLE, SYS_MODEL_EXEC, "model/AIRPLANE",
        "\xeb\xb9\x84\xed\x96\x89\xea\xb8\xb0 \xeb\xaa\xa8\xeb\x93\x9c", 0 } },
    // Edit device name (map #4): stock viewKeyboard + NsMiscSetDeviceName. openView KEYBOARD.
    { "sys-devicename", "\xec\x9e\xa5\xec\xb9\x98 \xec\x9d\xb4\xeb\xa6\x84 \xed\x8e\xb8\xec\xa7\x91",   // "장치 이름 편집"
      { MAK_OPEN_VIEW, SYS_OPEN_VIEW, "KEYBOARD",
        "\xec\x9e\xa5\xec\xb9\x98 \xec\x9d\xb4\xeb\xa6\x84", 0 } },
    // REC folder select / create (map #36/#37): openView SELECT_FOLDER / CREATE_FOLDER.
    { "info-na-recfolder", "REC \xed\x8f\xb4\xeb\x8d\x94 \xec\x84\xa0\xed\x83\x9d",                    // "REC 폴더 선택"
      { MAK_OPEN_VIEW, SYS_OPEN_VIEW, "SELECT_FOLDER",
        "REC \xed\x8f\xb4\xeb\x8d\x94", 0 } },
    { "info-na-recfolder", "\xec\x83\x88 \xed\x8f\xb4\xeb\x8d\x94",                                    // "새 폴더"
      { MAK_OPEN_VIEW, SYS_OPEN_VIEW, "CREATE_FOLDER",
        "\xec\x83\x88 \xed\x8f\xb4\xeb\x8d\x94", 0 } },
    // USB connection mode (map #25): openView USBAUTO (mode picker).
    { "info-na-usb", "USB \xec\x97\xb0\xea\xb2\xb0",                                                  // "USB 연결"
      { MAK_OPEN_VIEW, SYS_OPEN_VIEW, "USBAUTO",
        "USB \xec\x97\xb0\xea\xb2\xb0", 0 } },
    // Area/region set (map #33): openView AREA_SET. Tier B (PAL/NTSC linked) -- safe to OPEN the view.
    { "info-na-region", "\xec\xa7\x80\xec\x97\xad \xec\x84\xa4\xec\xa0\x95",                           // "지역 설정"
      { MAK_OPEN_VIEW, SYS_OPEN_VIEW, "AREA_SET",
        "\xec\xa7\x80\xec\x97\xad \xec\x84\xa4\xec\xa0\x95", 0 } },

    // ---- DESTRUCTIVE (EXPERIMENTAL-gated + confirm) ----
    // Format (map #34): viewFormat.so -> engine FORMAT. SD data loss.
    { "sys-format", "\xed\x8f\xac\xeb\xa7\xb7",                                                       // "포맷"
      { MAK_DESTRUCTIVE, SYS_OPEN_VIEW, "FORMAT",
        "\xed\x8f\xac\xeb\xa7\xb7",
        "\xeb\xa9\x94\xeb\xaa\xa8\xeb\xa6\xac \xec\xb9\xb4\xeb\x93\x9c\xeb\xa5\xbc \xed\x8f\xac\xeb\xa7\xb7\xed\x95\xa9\xeb\x8b\x88\xeb\x8b\xa4. \xeb\xaa\xa8\xeb\x93\xa0 \xec\x82\xac\xec\xa7\x84\xec\x9d\xb4 \xec\x82\xad\xec\xa0\x9c\xeb\x90\xa9\xeb\x8b\x88\xeb\x8b\xa4." } },  // "메모리 카드를 포맷합니다. 모든 사진이 삭제됩니다."
    // Initialize / factory reset (map #43): viewInitialSetting.so -> backup reset.
    { "sys-reset", "\xec\x84\xa4\xec\xa0\x95 \xeb\xa6\xac\xec\x85\x8b",                                // "설정 리셋"
      { MAK_DESTRUCTIVE, SYS_OPEN_VIEW, "INITIAL_SETTING",
        "\xec\x84\xa4\xec\xa0\x95 \xeb\xa6\xac\xec\x85\x8b",
        "\xeb\xaa\xa8\xeb\x93\xa0 \xec\x84\xa4\xec\xa0\x95\xec\x9d\x84 \xec\xb4\x88\xea\xb8\xb0\xed\x99\x94\xed\x95\xa9\xeb\x8b\x88\xeb\x8b\xa4." } },  // "모든 설정을 초기화합니다."
    // Image DB recover (map #39): viewManualRepair.so. DB rebuild (long/interruptible).
    { "sys-dbrestore", "\xec\x9d\xb4\xeb\xaf\xb8\xec\xa7\x80 DB \xeb\xb3\xb5\xec\x9b\x90",             // "이미지 DB 복원"
      { MAK_DESTRUCTIVE, SYS_OPEN_VIEW, "MANUALREPAIR",
        "\xec\x9d\xb4\xeb\xaf\xb8\xec\xa7\x80 DB \xeb\xb3\xb5\xec\x9b\x90",
        "\xec\x9d\xb4\xeb\xaf\xb8\xec\xa7\x80 \xea\xb4\x80\xeb\xa6\xac DB\xeb\xa5\xbc \xec\x9e\xac\xea\xb5\xac\xec\xb6\x95\xed\x95\xa9\xeb\x8b\x88\xeb\x8b\xa4." } },  // "이미지 관리 DB를 재구축합니다."
    // Playback delete (map #14): stock delete model-exec (individual). Keep the confirm, gate the run.
    { "pb-view", "\xec\x82\xad\xec\xa0\x9c(\xea\xb0\xa4\xeb\x9f\xac\xeb\xa6\xac\xec\x97\x90\xec\x84\x9c \xed\x9c\xb4\xec\xa7\x80\xed\x86\xb5\xed\x82\xa4)",  // "삭제(갤러리에서 휴지통키)"
      { MAK_DESTRUCTIVE, SYS_OPEN_VIEW, "PLAYBACK_DELETE",
        "\xec\x82\xad\xec\xa0\x9c",
        "\xec\x84\xa0\xed\x83\x9d\xed\x95\x9c \xec\x82\xac\xec\xa7\x84\xec\x9d\x84 \xec\x82\xad\xec\xa0\x9c\xed\x95\xa9\xeb\x8b\x88\xeb\x8b\xa4." } },  // "선택한 사진을 삭제합니다."
};

const char *SA_name(int r) {
    switch (r) {
        case SA_DONE:        return "DONE";
        case SA_DEVICE_SEAL: return "DEVICE-SEAL";
        default:             return "UNSUPPORTED";
    }
}

} // namespace

bool menuActionEntry(const char *key, const char *label, MenuActionDesc *out) {
    if (!key || !label) return false;
    for (unsigned i = 0; i < sizeof kRows / sizeof kRows[0]; i++)
        if (!strcmp(kRows[i].key, key) && !strcmp(kRows[i].label, label)) {
            if (out) *out = kRows[i].d;
            return true;
        }
    return false;
}

bool menuActionGateEnabled() {
    const char *e = getenv("A5K_EXPERIMENTAL");
    if (e && *e) return true;
    FILE *f = fopen("/android/a5k/experimental", "r");   // RAM tmpfs marker (brick-safe)
    if (f) { fclose(f); return true; }
    return false;
}

int runMenuAction(App &app, const MenuActionDesc &d, char *toastOut, int n) {
    CameraControl *cam = app.camera();
    int r = cam ? cam->systemAction(d.sysAction, d.arg) : SA_UNSUPPORTED;
    logf("menu-action: %s arg=%s -> %s", d.title ? d.title : "?", d.arg ? d.arg : "(launcher)", SA_name(r));
    if (!toastOut || n <= 0) return r;
    // UTF-8 result lines.
    const char *done = "\xec\x8b\xa4\xed\x96\x89\xeb\x90\xa8";                                        // "실행됨"
    const char *seal = "\xea\xb8\xb0\xea\xb8\xb0\xec\x97\x90\xec\x84\x9c \xeb\x8f\x99\xec\x9e\x91 (device-seal)";  // "기기에서 동작 (device-seal)"
    const char *nope = "\xea\xb2\xbd\xeb\xa1\x9c \xec\x97\x86\xec\x9d\x8c";                            // "경로 없음"
    const char *msg = r == SA_DONE ? done : r == SA_DEVICE_SEAL ? seal : nope;
    snprintf(toastOut, n, "%s", msg);
    return r;
}

} // namespace uicore
