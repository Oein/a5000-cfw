// M2 core: the main MENU as a cascade / miller 3-column list (user decision: no tiles).
//   [category rail] | [group list with number badges] | [item list: label ... value]
// The three columns are always visible and the hierarchy stacks to the right, exactly like Track A's
// MainMenu (the golden: test/golden/menu__cascade__*.png, 853x480 app pixels). Layout is done in the
// 853-wide LOGICAL space and squeezed X*0.75 to the 640x480 panel (LX()); fonts are pre-squeezed.
//   - Inventory: kMenuCats/kMenuGroups/kMenuItems (menu_gen.cpp, generated from the Track A catalog:
//     7 categories / 21 groups / 140 items -- feature parity, nothing dropped).
//   - Navigation (abstract actions only): UP/DOWN move inside the focused column, RIGHT/CONFIRM go
//     deeper (CONFIRM on an item opens its value picker), LEFT goes back a column, BACK = LEFT (closes
//     at the rail), MENU closes. Item scrolling flows across groups/categories and skips disabled rows.
//   - Value picker (ENUM/BOOL/RANGE/PREF): UP/DOWN select, CONFIRM commits, LEFT/BACK/MENU cancels.
//   - Values: a local value store seeded with the host-sim fake defaults, bridged to the CameraModel
//     for the settings the SettingsCatalog knows (EV, ISO, drive, focus, metering, WB, storage, AEL).
//     Everything else is menu-local until the real apply tracks (SDCom/Bkup) land.
// No platform headers: only uicore canvas/gfx/model.
#include "uicore/screen.h"
#include "uicore/app.h"
#include "uicore/canvas.h"
#include "uicore/camera_model.h"
#include "uicore/gfx.h"
#include "uicore/menu_gen.h"
#include "uicore/log.h"
#include "../widgets/text_input.h"
#include "wifi.h"
#include "nfc.h"
#include "menu_actions.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace uicore {
namespace {

// ---- Track A MainMenu palette ----
const Color kBg       = { 0, 0, 0, 255 };
const Color kSel      = { 0xD3, 0x41, 0x00, 255 };   // focused-column selection (Sony active #D34100)
const Color kSelDim   = { 0x4a, 0x4a, 0x4a, 255 };   // selection in a non-focused column (neutral grey)
const Color kAccent   = { 0xff, 0x9a, 0x2e, 255 };
const Color kDim      = { 0xb9, 0xbc, 0xc0, 255 };
const Color kDisabled = { 0x5a, 0x5d, 0x62, 255 };
const Color kRule     = { 0x3a, 0x3d, 0x42, 255 };
const Color kWhite    = { 255, 255, 255, 255 };
const Color kMoreCol  = { 0x8a, 0x8d, 0x92, 255 };
const Color kCardBg   = { 0x20, 0x22, 0x26, 255 };
const Color kCardLine = { 0x44, 0x47, 0x4c, 255 };

// ---- logical (853x480) geometry measured from the golden ----
enum {
    TOP_RULE_Y = 50, BOT_RULE_Y = 443, BODY_TOP = 52, BODY_H = 391,   // content y 52..442
    ROW_PITCH = 51, ROW_TOP = 59, MORE_H = 20, VIS = 6,
    RAIL_W_FOCUS = 57, RAIL_COL_FOCUS = 65, RAIL_BIG = 102,
    COL_B_W = 333
};

// ------------------------------------------------------------------ helpers
inline Color shade(unsigned argb, int num, int den) {          // 0xRRGGBB scaled
    Color c = { (u8)(((argb >> 16) & 0xff) * num / den), (u8)(((argb >> 8) & 0xff) * num / den),
                (u8)((argb & 0xff) * num / den), 255 };
    return c;
}
inline Color rgbOf(unsigned argb) { return shade(argb, 1, 1); }

inline void fillL(Canvas &c, int x0, int y0, int x1, int y1, Color col) {   // logical x, x1 exclusive
    int p0 = LX(x0), p1 = LX(x1);
    if (p1 > p0 && y1 > y0) c.fillRect(p0, y0, p1 - p0, y1 - y0, col);
}

// UTF-8 aware truncation: keep whole characters so the text fits maxPx physical pixels.
void fitText(const Gfx &g, int font, const char *s, int maxPx, char *out, int outN) {
    int n = (int)strlen(s);
    if (n >= outN) n = outN - 1;
    memcpy(out, s, n); out[n] = 0;
    while (n > 0 && g.textWidth(font, out) > maxPx) {
        n--;
        while (n > 0 && (out[n] & 0xC0) == 0x80) n--;   // back to a character boundary
        out[n] = 0;
    }
}

// ------------------------------------------------------------------ vector shapes (rail icons, chevrons)
enum ShapeKind { SH_DISC, SH_RING, SH_RRECT, SH_RRECT_STROKE, SH_ARC, SH_POLY, SH_LINE };
struct Shape {
    int kind;
    float a, b, c, d, e, f, g;     // params, per kind (see inShape)
    const float *pts; int npts;    // SH_POLY: x,y pairs
};

float sdRR(float px, float py, float x0, float y0, float x1, float y1, float r) {
    float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, hx = (x1 - x0) * 0.5f - r, hy = (y1 - y0) * 0.5f - r;
    float qx = fabsf(px - cx) - hx, qy = fabsf(py - cy) - hy;
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0, m = qx > qy ? qx : qy;
    return sqrtf(ox * ox + oy * oy) + (m < 0 ? m : 0) - r;
}

float segDist(float px, float py, float x0, float y0, float x1, float y1) {
    float dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy;
    float t = l2 > 0 ? ((px - x0) * dx + (py - y0) * dy) / l2 : 0;
    if (t < 0) t = 0; else if (t > 1) t = 1;
    float qx = x0 + t * dx - px, qy = y0 + t * dy - py;
    return sqrtf(qx * qx + qy * qy);
}

bool inShape(const Shape &s, float x, float y) {
    switch (s.kind) {
        case SH_DISC: { float dx = x - s.a, dy = y - s.b; return dx * dx + dy * dy <= s.c * s.c; }
        case SH_RING: { float dx = x - s.a, dy = y - s.b, d = sqrtf(dx * dx + dy * dy); return fabsf(d - s.c) <= s.d * 0.5f; }
        case SH_RRECT: return sdRR(x, y, s.a, s.b, s.c, s.d, s.e) <= 0;
        case SH_RRECT_STROKE: return fabsf(sdRR(x, y, s.a, s.b, s.c, s.d, s.e)) <= s.f * 0.5f;
        case SH_LINE: return segDist(x, y, s.a, s.b, s.c, s.d) <= s.e * 0.5f;
        case SH_ARC: {   // a,b centre; c radius; d stroke; e start deg (y down, clockwise); f sweep deg; round caps
            float dx = x - s.a, dy = y - s.b, d = sqrtf(dx * dx + dy * dy);
            float hw = s.d * 0.5f;
            float a0 = s.e * 3.14159265f / 180.0f, a1 = (s.e + s.f) * 3.14159265f / 180.0f;
            if (fabsf(d - s.c) <= hw) {
                float ang = atan2f(dy, dx) * 180.0f / 3.14159265f - s.e;
                while (ang < 0) ang += 360.0f;
                while (ang >= 360.0f) ang -= 360.0f;
                if (ang <= s.f) return true;
            }
            float ex0 = s.a + cosf(a0) * s.c, ey0 = s.b + sinf(a0) * s.c;
            float ex1 = s.a + cosf(a1) * s.c, ey1 = s.b + sinf(a1) * s.c;
            return (x - ex0) * (x - ex0) + (y - ey0) * (y - ey0) <= hw * hw ||
                   (x - ex1) * (x - ex1) + (y - ey1) * (y - ey1) <= hw * hw;
        }
        case SH_POLY: {
            bool in = false;
            int n = s.npts;
            for (int i = 0, j = n - 1; i < n; j = i++) {
                float xi = s.pts[i * 2], yi = s.pts[i * 2 + 1], xj = s.pts[j * 2], yj = s.pts[j * 2 + 1];
                if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) in = !in;
            }
            return in;
        }
    }
    return false;
}

// Rasterise a union of shapes (logical coordinates, origin = (ox,oy)), optionally rotated by rotDeg about
// (cx,cy), supersampled 4x4, white at `alpha` over what is already on the canvas.
void rasterShapes(Canvas &c, const Shape *sh, int n, int x0, int y0, int x1, int y1,
                  float rotDeg, float rcx, float rcy, int alpha, Color col) {
    float cs = cosf(rotDeg * 3.14159265f / 180.0f), sn = sinf(rotDeg * 3.14159265f / 180.0f);
    int px0 = LX(x0), px1 = LX(x1);
    // Single-sample point test. 4x4 supersampling (16 point-in-shape tests + softfp
    // math per pixel, x7 icons, every frame) was ~hundreds of ms on the a5000's single
    // slow CPU -- fine on the dev host where this was tuned, far too slow here. One
    // sample is aliased but ~16x cheaper; the icons stay legible.
    for (int py = y0; py < y1; py++) {
        for (int px = px0; px < px1; px++) {
            float lx = (px + 0.5f) * 4.0f / 3.0f;
            float ly = py + 0.5f;
            if (rotDeg != 0) {                  // inverse of the shape rotation
                float dx = lx - rcx, dy = ly - rcy;
                lx = rcx + dx * cs - dy * sn; ly = rcy + dx * sn + dy * cs;
            }
            for (int k = 0; k < n; k++) if (inShape(sh[k], lx, ly)) {
                c.setPixel(px, py, rgba(col.r, col.g, col.b, (u8)alpha));
                break;
            }
        }
    }
}

// Category icons = Track A MenuIcons (white, drawn; every icon fills +-U around the cell centre).
void drawCatIcon(Canvas &c, int kind, int cx0, int cy0, int cw, int ch, int alpha) {
    float cx = cx0 + cw * 0.5f, cy = cy0 + ch * 0.5f;
    float U = 0.2156f * ch;                     // Track A: 0.44 * (0.75*s - 2*0.13*s) squeezed back by the LCD stretch
    float sw = U * 0.18f; if (sw < 1.6f) sw = 1.6f;
    Shape sh[12]; int n = 0;
    float poly[24];
    float rot = 0;
    memset(sh, 0, sizeof sh);
    switch (kind) {
        case MI_CAMERA: {
            sh[n].kind = SH_RRECT_STROKE; sh[n].a = cx - U; sh[n].b = cy - U * 0.55f; sh[n].c = cx + U; sh[n].d = cy + U * 0.78f; sh[n].e = U * 0.2f; sh[n].f = sw; n++;
            sh[n].kind = SH_RRECT; sh[n].a = cx - U * 0.42f; sh[n].b = cy - U * 0.82f; sh[n].c = cx - U * 0.02f; sh[n].d = cy - U * 0.5f; sh[n].e = 0; n++;
            sh[n].kind = SH_RING; sh[n].a = cx; sh[n].b = cy + U * 0.14f; sh[n].c = U * 0.42f; sh[n].d = sw; n++;
            sh[n].kind = SH_DISC; sh[n].a = cx; sh[n].b = cy + U * 0.14f; sh[n].c = U * 0.15f; n++;
            break;
        }
        case MI_GEAR: {
            for (int i = 0; i < 8; i++) {
                float a = 3.14159265f * 2 * i / 8;
                sh[n].kind = SH_DISC; sh[n].a = cx + cosf(a) * U; sh[n].b = cy + sinf(a) * U; sh[n].c = sw; n++;
            }
            sh[n].kind = SH_RING; sh[n].a = cx; sh[n].b = cy; sh[n].c = U * 0.62f; sh[n].d = sw; n++;
            sh[n].kind = SH_DISC; sh[n].a = cx; sh[n].b = cy; sh[n].c = U * 0.24f; n++;
            break;
        }
        case MI_WIFI: {
            float by = cy + U * 0.72f;
            for (int i = 1; i <= 3; i++) {
                sh[n].kind = SH_ARC; sh[n].a = cx; sh[n].b = by; sh[n].c = U * (i / 3.0f); sh[n].d = sw; sh[n].e = 225; sh[n].f = 90; n++;
            }
            sh[n].kind = SH_DISC; sh[n].a = cx; sh[n].b = by; sh[n].c = sw * 0.9f; n++;
            break;
        }
        case MI_GRID: {
            float cell = U * 0.78f, gap = U * 0.28f;
            float x0 = cx - gap / 2 - cell, y0 = cy - gap / 2 - cell;
            for (int ix = 0; ix < 2; ix++) for (int iy = 0; iy < 2; iy++) {
                float x = x0 + ix * (cell + gap), y = y0 + iy * (cell + gap);
                sh[n].kind = SH_RRECT; sh[n].a = x; sh[n].b = y; sh[n].c = x + cell; sh[n].d = y + cell; sh[n].e = cell * 0.22f; n++;
            }
            break;
        }
        case MI_PLAY: {
            poly[0] = cx - U * 0.6f; poly[1] = cy - U; poly[2] = cx - U * 0.6f; poly[3] = cy + U; poly[4] = cx + U * 0.92f; poly[5] = cy;
            sh[n].kind = SH_POLY; sh[n].pts = poly; sh[n].npts = 3; n++;
            break;
        }
        case MI_STAR: {
            for (int i = 0; i < 10; i++) {
                float a = -3.14159265f / 2 + 3.14159265f * i / 5;
                float rr = (i % 2 == 0) ? U : U * 0.42f;
                poly[i * 2] = cx + cosf(a) * rr; poly[i * 2 + 1] = cy + sinf(a) * rr;
            }
            sh[n].kind = SH_POLY; sh[n].pts = poly; sh[n].npts = 10; n++;
            break;
        }
        case MI_WRENCH: {   // shapes in the un-rotated frame; rasterShapes undoes the -42 deg canvas rotation
            float hw = U * 0.34f;
            sh[n].kind = SH_RRECT; sh[n].a = cx - hw; sh[n].b = cy - U * 0.15f; sh[n].c = cx + hw; sh[n].d = cy + U; sh[n].e = hw; n++;
            sh[n].kind = SH_ARC; sh[n].a = cx; sh[n].b = cy - U * 0.38f; sh[n].c = U * 0.62f; sh[n].d = sw * 1.25f; sh[n].e = 110; sh[n].f = 300; n++;
            rot = 42.0f;   // screen -> shape = rotate by +42
            break;
        }
    }
    int r = (int)(U + sw + 2);
    rasterShapes(c, sh, n, (int)(cx - r - 2), (int)(cy - r - 2), (int)(cx + r + 3), (int)(cy + r + 3), rot, cx, cy, alpha, kWhite);
}

void drawChevron(Canvas &c, float cx, float cy, float h, float w, Color col) {   // thin "›"
    Shape sh[2]; memset(sh, 0, sizeof sh);
    sh[0].kind = SH_LINE; sh[0].a = cx - w * 0.5f; sh[0].b = cy - h * 0.5f; sh[0].c = cx + w * 0.5f; sh[0].d = cy; sh[0].e = 1.7f;
    sh[1].kind = SH_LINE; sh[1].a = cx + w * 0.5f; sh[1].b = cy; sh[1].c = cx - w * 0.5f; sh[1].d = cy + h * 0.5f; sh[1].e = 1.7f;
    rasterShapes(c, sh, 2, (int)(cx - w - 2), (int)(cy - h), (int)(cx + w + 3), (int)(cy + h), 0, 0, 0, col.a, col);
}

// ------------------------------------------------------------------ menu value model
struct SettingBridge { const char *key; int id; };
const SettingBridge kBridge[] = {
    { "exposure-compensation", SET_EV },   { "iso-sensitivity", SET_ISO },     { "drive-mode", SET_DRIVE },
    { "focus-mode", SET_FOCUS_MODE },      { "af-mode", SET_AF_MODE },         { "focus-area-type", SET_FOCUS_AREA },
    { "metering-mode", SET_METERING },     { "whitebalance", SET_WB },         { "storage-fmt", SET_STORAGE_FMT },
    { "ae-lock", SET_AE_LOCK },
};
int bridgeId(const char *key) {
    for (unsigned i = 0; i < sizeof kBridge / sizeof kBridge[0]; i++) if (!strcmp(kBridge[i].key, key)) return kBridge[i].id;
    return SET_NONE;
}

// The stock "custom-key" catalog item opens our full Custom Key settings screen (all shooting keys),
// not a one-shot PREF picker -- matched by key, like the nfc/wifi rows below.
bool keyConfigRow(const MenuItem &it) { return !strcmp(it.key, "custom-key"); }

// TEXT items (device name, Wi-Fi SSID/PW) open the text-input widget (ui/widgets/text_input); they are
// live even where the generated inventory marks the Wi-Fi rows disabled.
bool wifiRow(const MenuItem &it) { int p; bool a; return wifiMenuEntry(it.key, it.label, &p, &a) || nfcMenuEntry(it.key, it.label); }
// App-launch / action rows (menu_actions): wired to the stock native path, so live even where the
// generated inventory marks them MF_DISABLED (= "not available" placeholder), exactly like wifiRow.
bool actionRow(const MenuItem &it) { MenuActionDesc d; return menuActionEntry(it.key, it.label, &d); }
bool disabled(const MenuItem &it) { return (it.flags & MF_DISABLED) != 0 && !findTextField(it.key, it.label) && !wifiRow(it) && !actionRow(it); }

// Track A valueLabel fallback: pretty(raw)
void prettyOf(const char *raw, char *out, int n) {
    int i = 0;
    for (; raw[i] && i < n - 1; i++) out[i] = (raw[i] == '-' || raw[i] == '_') ? ' ' : raw[i];
    out[i] = 0;
    if (out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 'a' + 'A');
}

class MenuScreen : public Screen {
public:
    MenuScreen() : focus_(0), cat_(0), group_(0), item_(0), modal_(false), valSel_(0), n_(0), synced_(false),
                   pFocus_(-1), pCat_(-1), pGroup_(-1), pItem_(-1), pStartI_(-1), dirty_(true),
                   actModal_(AMK_NONE), confirmSel_(0) {
        for (int i = 0; i < MENU_ITEMS; i++) { strncpy(val_[i], kMenuItems[i].def, sizeof val_[i] - 1); val_[i][sizeof val_[i] - 1] = 0; }
        toast_[0] = 0; memset(&actDesc_, 0, sizeof actDesc_);
    }
    const char *title() const { return "MENU"; }
    Context context() const { return CTX_MENU; }

    // ---------------------------------------------------------- model queries
    const MenuGroup &grp(int cat, int g) const { return kMenuGroups[kMenuCats[cat].firstGroup + g]; }
    int itemIdx(int cat, int g, int i) const { return grp(cat, g).first + i; }
    int nGroups(int cat) const { return kMenuCats[cat].nGroups; }
    int nItems(int cat, int g) const { return grp(cat, g).count; }
    int curItem() const { return itemIdx(cat_, group_, item_); }

    // Pull catalog-backed values from the camera model (raw string = HAL value name / decimal).
    void syncFromModel(App &app) {
        const CameraState &st = app.model().state();
        if (!app.model().online()) return;
        for (int i = 0; i < MENU_ITEMS; i++) {
            int id = bridgeId(kMenuItems[i].key);
            int v;
            if (id == SET_NONE || !st.get(id, &v)) continue;
            const SettingDef *d = catalogDef(id);
            if (!d) continue;
            if (d->kind == SK_INT || d->kind == SK_LIST) snprintf(val_[i], sizeof val_[i], "%d", v);
            else { const char *l = catalogLabel(id, v); if (l) { strncpy(val_[i], l, sizeof val_[i] - 1); val_[i][sizeof val_[i] - 1] = 0; } }
        }
    }

    void valueText(int idx, char *out, int n) const {
        const MenuItem &it = kMenuItems[idx];
        if (const TextFieldDef *tf = findTextField(it.key, it.label)) { textFieldSummary(*tf, out, n); return; }
        if (keyConfigRow(it)) { snprintf(out, n, "\xe2\x96\xb6"); return; }   // opens the Custom Key screen
        if (actionRow(it)) { snprintf(out, n, "\xe2\x96\xb6"); return; }       // app-launch / stock action
        if (disabled(it)) { snprintf(out, n, "\xe2\x80\x94"); return; }
        if (it.type == MT_ACTION) {
            if (!strncmp(it.key, "quick-slot-", 11)) snprintf(out, n, "%s", it.def);   // slot target label (picker = follow-up)
            else snprintf(out, n, "\xe2\x96\xb6");
            return;
        }
        if (it.type == MT_INFO) { snprintf(out, n, "%s", it.def[0] ? it.def : "--"); return; }
        if (it.type == MT_TODO) { snprintf(out, n, "\xe2\x80\x94"); return; }
        if (it.type == MT_EXP) {
            if (!(it.flags & MF_EXP_ON)) { snprintf(out, n, "\xec\x8b\xa4\xed\x97\x98(\xeb\xb9\x84\xed\x99\x9c\xec\x84\xb1)"); return; }   // "실험(비활성)"
            snprintf(out, n, "\xe2\x80\x94"); return;
        }
        const char *raw = val_[idx];
        if (!raw[0]) { snprintf(out, n, "--"); return; }
        for (int i = 0; i < it.nOpts; i++) if (!strcmp(it.opts[i].raw, raw)) { snprintf(out, n, "%s", it.opts[i].label); return; }
        if (it.type == MT_RANGE) { snprintf(out, n, "%s", raw); return; }
        prettyOf(raw, out, n);
    }

    // Picker option i of item idx. RANGE options are generated (rmin..rmax).
    int optCount(int idx) const {
        const MenuItem &it = kMenuItems[idx];
        if (it.type == MT_RANGE) return it.rmax >= it.rmin ? it.rmax - it.rmin + 1 : 0;
        return it.nOpts;
    }
    void optAt(int idx, int i, char *raw, char *label, int n) const {
        const MenuItem &it = kMenuItems[idx];
        if (it.type == MT_RANGE) { snprintf(raw, n, "%d", it.rmin + i); snprintf(label, n, "%d", it.rmin + i); return; }
        snprintf(raw, n, "%s", it.opts[i].raw); snprintf(label, n, "%s", it.opts[i].label);
    }
    bool openable(int idx) const {
        const MenuItem &it = kMenuItems[idx];
        if (disabled(it)) return false;
        if (it.type == MT_INFO || it.type == MT_ACTION || it.type == MT_TODO) return false;
        if (it.type == MT_EXP && !(it.flags & MF_EXP_ON)) return false;   // gated: no modal, no apply
        return optCount(idx) > 0;
    }

    // ---------------------------------------------------------- input
    void onAction(Action a, int value, App &app) {
        if (!synced_) { syncFromModel(app); synced_ = true; }
        if (value == 0 && a != ACT_NONE) return;              // act on key down / repeat only
        if (a == ACT_DIAL_MAIN_CW || a == ACT_DIAL_SUB_CW) a = ACT_NAV_DOWN;
        if (a == ACT_DIAL_MAIN_CCW || a == ACT_DIAL_SUB_CCW) a = ACT_NAV_UP;
        if (actModal_ != AMK_NONE) { actModalAction(a, app); return; }
        if (modal_) { pickerAction(a, app); return; }
        switch (a) {
            case ACT_NAV_UP:    move(-1); break;
            case ACT_NAV_DOWN:  move(+1); break;
            case ACT_NAV_RIGHT:
            case ACT_CONFIRM:   deeper(app); break;
            case ACT_NAV_LEFT:  if (focus_ > 0) focus_--; break;
            case ACT_BACK:      if (focus_ > 0) focus_--; else app.popScreen(); break;
            case ACT_MENU:      app.popScreen(); break;
            default: break;
        }
    }

    void clampSel() {
        if (group_ >= nGroups(cat_)) group_ = 0;
        if (item_ >= nItems(cat_, group_)) item_ = 0;
    }

    void move(int d, int guard = 0) {
        if (focus_ == 0) {
            cat_ = (cat_ + d + MENU_CATS) % MENU_CATS; group_ = 0; item_ = 0;
        } else if (focus_ == 1) {
            int n = nGroups(cat_); group_ = (group_ + d + n) % n; item_ = 0;
        } else {
            // items flow across group and category boundaries (one big scroll, Sony style)
            int next = item_ + d;
            if (next >= nItems(cat_, group_)) {
                if (group_ < nGroups(cat_) - 1) { group_++; item_ = 0; }
                else { cat_ = (cat_ + 1) % MENU_CATS; group_ = 0; item_ = 0; }
            } else if (next < 0) {
                if (group_ > 0) { group_--; item_ = nItems(cat_, group_) - 1; }
                else { cat_ = (cat_ - 1 + MENU_CATS) % MENU_CATS; group_ = nGroups(cat_) - 1; item_ = nItems(cat_, group_) - 1; }
            } else item_ = next;
            if (disabled(kMenuItems[curItem()]) && guard < 512) move(d, guard + 1);   // skip rows the app cannot do
        }
    }

    void deeper(App &app) {
        if (focus_ < 2) {
            focus_++;
            if (focus_ == 2 && disabled(kMenuItems[curItem()])) move(1);
            return;
        }
        int idx = curItem();
        const MenuItem &it = kMenuItems[idx];
        if (disabled(it)) return;
        if (const TextFieldDef *tf = findTextField(it.key, it.label)) { openTextField(app, *tf); return; }
        if (keyConfigRow(it)) { app.pushScreen(createKeyConfigScreen()); return; }   // 사용자 키 설정
        if (nfcMenuEntry(it.key, it.label)) { app.pushScreen(createNfcScreen()); return; }
        { int wp; bool wa; if (wifiMenuEntry(it.key, it.label, &wp, &wa)) { app.pushScreen(createWifiScreen(wp | (wa ? WP_AUTOSTART : 0))); return; } }
        { MenuActionDesc d; if (menuActionEntry(it.key, it.label, &d)) { startAction(app, d); return; } }
        if (it.type == MT_ACTION) {
            if (strncmp(it.key, "quick-slot-", 11)) { logf("menu: action %s (apply = follow-up track)", it.key); app.popScreen(); }
            return;
        }
        if (!openable(idx)) return;
        n_ = optCount(idx);
        valSel_ = 0;
        char raw[24], lab[48];
        for (int i = 0; i < n_; i++) { optAt(idx, i, raw, lab, sizeof raw); if (!strcmp(raw, val_[idx])) { valSel_ = i; break; } }
        modal_ = true;
    }

    void pickerAction(Action a, App &app) {
        switch (a) {
            case ACT_NAV_UP:   valSel_ = (valSel_ - 1 + n_) % n_; break;
            case ACT_NAV_DOWN: valSel_ = (valSel_ + 1) % n_; break;
            case ACT_CONFIRM: commit(app); break;
            case ACT_NAV_LEFT: case ACT_BACK: case ACT_MENU: modal_ = false; break;
            default: break;
        }
    }

    void commit(App &app) {
        int idx = curItem();
        char raw[24], lab[48];
        optAt(idx, valSel_, raw, lab, sizeof raw);
        strncpy(val_[idx], raw, sizeof val_[idx] - 1); val_[idx][sizeof val_[idx] - 1] = 0;
        // Every picker commit goes through the generic apply path (conversion table). Catalog-backed
        // items still reach setSetting() via the default executor; the rest use the platform's routes.
        // RANGE: the number; enumerated: the option index.
        int val = (kMenuItems[idx].type == MT_RANGE) ? kMenuItems[idx].rmin + valSel_ : valSel_;
        int r = app.model().applySetting(idx, val);
        logf("menu: apply result %d", r);
        logf("menu: set %s = %s", kMenuItems[idx].key, raw);
        modal_ = false;
        dirty_ = true;          // the item's value text changed -> force a full redraw
    }

    // ---------------------------------------------------------- app-launch / action items
    // deeper() lands here for a menu_actions row. Safe actions (app launch / openView / model
    // toggle) run at once; destructive ones (format/reset/db-recover/delete) open a confirm modal
    // and run only when BOTH the user confirms AND menuActionGateEnabled() (CLAUDE.md s1 brick-0).
    void startAction(App &app, const MenuActionDesc &d) {
        actDesc_ = d;
        if (d.kind == MAK_DESTRUCTIVE) {
            if (!menuActionGateEnabled()) {
                // Inert, exactly like Track A's EXPERIMENTAL_ENABLED: no run, explain the gate.
                snprintf(toast_, sizeof toast_,
                         "\xec\x8b\xa4\xed\x97\x98 \xeb\xaa\xa8\xeb\x93\x9c \xeb\xb9\x84\xed\x99\x9c\xec\x84\xb1 \xe2\x80\x94 \xeb\xb3\xb4\xed\x98\xb8\xeb\x90\x9c \xeb\x8f\x99\xec\x9e\x91");  // "실험 모드 비활성 — 보호된 동작"
                actModal_ = AMK_TOAST;
            } else {
                confirmSel_ = 0;                 // default on 취소 (cancel)
                actModal_ = AMK_CONFIRM;
            }
            dirty_ = true;
            return;
        }
        // safe action: dispatch immediately, show the result toast
        runMenuAction(app, actDesc_, toast_, sizeof toast_);
        actModal_ = AMK_TOAST;
        dirty_ = true;
    }

    void actModalAction(Action a, App &app) {
        if (actModal_ == AMK_TOAST) {            // any key dismisses the toast
            if (a == ACT_CONFIRM || a == ACT_BACK || a == ACT_NAV_LEFT || a == ACT_MENU ||
                a == ACT_NAV_UP || a == ACT_NAV_DOWN || a == ACT_NAV_RIGHT) { actModal_ = AMK_NONE; dirty_ = true; }
            return;
        }
        // AMK_CONFIRM: a two-choice (취소 / 실행) gate for a destructive action
        switch (a) {
            case ACT_NAV_UP: case ACT_NAV_DOWN: confirmSel_ ^= 1; dirty_ = true; break;
            case ACT_CONFIRM:
                if (confirmSel_ == 1) { runMenuAction(app, actDesc_, toast_, sizeof toast_); actModal_ = AMK_TOAST; }
                else                 { actModal_ = AMK_NONE; }
                dirty_ = true;
                break;
            case ACT_NAV_LEFT: case ACT_BACK: case ACT_MENU: actModal_ = AMK_NONE; dirty_ = true; break;
            default: break;
        }
    }

    // ---------------------------------------------------------- drawing
    struct Cols { int railW, vr1, bx0, bx1, vr2, cx0; };

    void textL(Canvas &c, const Gfx &g, int font, int lx, int baseline, const char *s, Color col) const {
        g.text(c, font, LX(lx), baseline, s, col, false);
    }
    int widthL(const Gfx &g, int font, const char *s) const { return g.textWidth(font, s) * 4 / 3; }

    static int clampStart(int sel, int n, int vis) {
        int s = sel - vis / 2;
        if (s > n - vis) s = n - vis;
        if (s < 0) s = 0;
        return s;
    }

    void draw(Canvas &c, App &app) {
        const Gfx &g = app.gfx();
        if (!synced_) { syncFromModel(app); synced_ = true; dirty_ = true; }
        clampSel();
        int startI, endI, nI;
        Cols k = computeCols(g, startI, endI, nI);

        // Dirty-region: the rail icons / column dim / static text only change on a LAYER
        // move (focus / category / group / scroll). A plain selection move inside the
        // focused item column repaints just the two affected rows + the "N / M" readout,
        // instead of the whole ~500ms software redraw. present() then blits only those
        // rows. (Full software redraw is far too slow on the a5000's single CPU; this is
        // the a5000-side answer to "only update what changed" until a GPU/Rigel path.)
        bool layoutSame = !dirty_ && !modal_ && actModal_ == AMK_NONE && focus_ == pFocus_ && cat_ == pCat_
                          && group_ == pGroup_ && startI == pStartI_;
        if (layoutSame && item_ == pItem_) return;        // nothing visible changed: keep canvas

        if (layoutSame && focus_ == 2) {
            itemRow(c, g, k, startI, pItem_);             // old row loses the highlight
            itemRow(c, g, k, startI, item_);              // new row gains it
            redrawHeaderPos(c, g);
        } else {
            c.clear(kBg);
            drawHeader(c, g);
            drawBody(c, g, k, startI, endI, nI);
            c.fillRect(0, TOP_RULE_Y, PANEL_W, 2, kRule);
            c.fillRect(0, BOT_RULE_Y, PANEL_W, 2, kRule);
            drawHint(c, g);
            if (modal_) drawPicker(c, g);
            if (actModal_ != AMK_NONE) drawActModal(c, g);
        }
        pFocus_ = focus_; pCat_ = cat_; pGroup_ = group_; pItem_ = item_; pStartI_ = startI;
        dirty_ = false;
    }

    // Column geometry + the visible item window -- the exact computation drawBody used to
    // inline, shared so the dirty-region fast path lands on the same rows.
    Cols computeCols(const Gfx &g, int &startI, int &endI, int &nI) const {
        Cols k;
        if (focus_ == 0) { k.railW = RAIL_BIG; k.vr1 = 132; k.bx0 = 134; k.bx1 = 467; k.vr2 = 467; k.cx0 = 469; }
        else             { k.railW = RAIL_COL_FOCUS; k.vr1 = 66; k.bx0 = 68; k.bx1 = 68 + COL_B_W; k.vr2 = k.bx1; k.cx0 = k.vr2 + 2; }
        nI = nItems(cat_, group_);
        startI = clampStart(item_, nI, VIS); endI = startI + VIS < nI ? startI + VIS : nI;
        if (focus_ == 2) {
            int maxVal = 0; char vb[64];
            for (int i = startI; i < endI; i++) {
                valueText(itemIdx(cat_, group_, i), vb, sizeof vb);
                int w = widthL(g, GF_UI_26, vb); if (w > maxVal) maxVal = w;
            }
            int availBC = LOGICAL_W - k.railW - 5;
            int target = availBC - (maxVal + 41 + 145);
            if (target < 145) target = 145;
            if (target > COL_B_W) target = COL_B_W;
            k.bx1 = k.bx0 + target; k.vr2 = k.bx1; k.cx0 = k.vr2 + 2;
        }
        return k;
    }

    // Paint exactly one item-column row (bg + optional highlight + value + label). The item
    // column is undimmed while focus==2 (the only time the fast path runs), so clearing to
    // kBg gives a clean row. Mirrors drawItems()'s per-row body exactly.
    void itemRow(Canvas &c, const Gfx &g, const Cols &k, int start, int i) const {
        int y = ROW_TOP + (start > 0 ? MORE_H : 0) + (i - start) * ROW_PITCH;
        fillL(c, k.cx0, y, LOGICAL_W, y + ROW_PITCH, kBg);
        int idx = itemIdx(cat_, group_, i);
        const MenuItem &it = kMenuItems[idx];
        bool sel = i == item_, dis = disabled(it);
        if (sel && !dis) fillL(c, k.cx0, y, LOGICAL_W, y + ROW_PITCH, focus_ == 2 ? kSel : kSelDim);
        char vb[64]; valueText(idx, vb, sizeof vb);
        Color lc = dis ? kDisabled : ((sel || focus_ < 2) ? kWhite : kDim);
        Color vc = dis ? kDisabled : ((sel && focus_ == 2) ? kWhite : kDim);
        int vw = g.textWidth(GF_UI_26, vb);
        int vx = LX(LOGICAL_W - 44) - vw;
        g.text(c, GF_UI_26, vx, y + 34, vb, vc, false);
        int labelMax = vx - LX(k.cx0 + 23) - LX(14);
        char lb[96]; fitText(g, GF_UI_26, it.label, labelMax, lb, sizeof lb);
        textL(c, g, GF_UI_26, k.cx0 + 23, y + 34, lb, lc);
    }

    // Repaint just the "N / M" readout in the header (clears its box first).
    void redrawHeaderPos(Canvas &c, const Gfx &g) const {
        fillL(c, 700, 8, 828, 44, kBg);          // cover the full glyph box (y=32 is the baseline)
        int n = focus_ == 0 ? MENU_CATS : (focus_ == 1 ? nGroups(cat_) : nItems(cat_, group_));
        int s = focus_ == 0 ? cat_ : (focus_ == 1 ? group_ : item_);
        char pos[24]; snprintf(pos, sizeof pos, "%d / %d", s + 1, n);
        int w = g.textWidth(GF_UI_24, pos);
        g.text(c, GF_UI_24, LX(822) - w, 32, pos, kDim, false);
    }

    void drawHeader(Canvas &c, const Gfx &g) const {
        const MenuCat &cat = kMenuCats[cat_];
        int x = 31;
        textL(c, g, GF_UI_27, x, 34, cat.name, kAccent);
        if (focus_ >= 1) {
            x += widthL(g, GF_UI_27, cat.name);
            drawChevron(c, (float)(x + 22), 22.5f, 13, 7, kAccent);
            textL(c, g, GF_UI_27, x + 38, 34, grp(cat_, group_).name, kAccent);
        }
        int n = focus_ == 0 ? MENU_CATS : (focus_ == 1 ? nGroups(cat_) : nItems(cat_, group_));
        int s = focus_ == 0 ? cat_ : (focus_ == 1 ? group_ : item_);
        char pos[24]; snprintf(pos, sizeof pos, "%d / %d", s + 1, n);
        int w = g.textWidth(GF_UI_24, pos);
        g.text(c, GF_UI_24, LX(822) - w, 32, pos, kDim, false);
    }

    void drawHint(Canvas &c, const Gfx &g) const {
        const char *h = "\xe2\x96\xb6 \xec\x95\x88\xec\x9c\xbc\xeb\xa1\x9c   \xe2\x97\x80 \xec\x9d\xb4\xec\xa0\x84   \xe2\x97\x8f \xec\x84\xa0\xed\x83\x9d   MENU \xeb\x8b\xab\xea\xb8\xb0";
        int w = g.textWidth(GF_UI_20, h);
        g.text(c, GF_UI_20, LX(821) - w + 1, 468, h, kDim, false);
    }

    void drawBody(Canvas &c, const Gfx &g, const Cols &k, int startI, int endI, int nI) {
        drawRail(c, k);
        drawGroups(c, g, k);
        drawItems(c, g, k, startI, endI, nI);
        // dim the columns that are not focused (Track A dimView = 0x99000000)
        Color dim = rgba(0, 0, 0, 0x99);
        if (focus_ != 1) fillL(c, k.bx0, BODY_TOP, k.bx1, BOT_RULE_Y, dim);
        if (focus_ < 2)  fillL(c, k.cx0, BODY_TOP, LOGICAL_W, BOT_RULE_Y, dim);
        // vertical rules
        fillL(c, k.vr1, BODY_TOP, k.vr1 + 2, BOT_RULE_Y, kRule);
        fillL(c, k.vr2, BODY_TOP, k.vr2 + 2, BOT_RULE_Y, kRule);
    }

    void drawRail(Canvas &c, const Cols &k) const {
        float H = (float)BODY_H;
        float y = (float)BODY_TOP;
        for (int i = 0; i < MENU_CATS; i++) {
            float s = focus_ == 0 ? (i == cat_ ? H * 0.26f : (H - H * 0.26f) / (MENU_CATS - 1)) : H / MENU_CATS;
            int y0 = (int)(y + 0.5f), y1 = (int)(y + s + 0.5f);
            int w = focus_ == 0 ? (int)(s + 0.5f) : RAIL_W_FOCUS;
            bool sel = i == cat_;
            const MenuCat &cat = kMenuCats[i];
            fillL(c, 0, y0, w, y1, sel ? rgbOf(cat.color) : shade(cat.color, 58, 100));
            drawCatIcon(c, cat.icon, 0, y0, w, y1 - y0, sel ? 255 : 205);
            y += s;
        }
        (void)k;
    }

    void drawMore(Canvas &c, const Gfx &g, int x0, int x1, int top, bool up) const {
        const char *a = up ? "\xe2\x96\xb2" : "\xe2\x96\xbc";
        int w = widthL(g, GF_UI_19, a);
        textL(c, g, GF_UI_19, (x0 + x1) / 2 - w / 2, top + 16, a, kMoreCol);
    }

    void drawGroups(Canvas &c, const Gfx &g, const Cols &k) const {
        int n = nGroups(cat_);
        int start = clampStart(group_, n, VIS), end = start + VIS < n ? start + VIS : n;
        int y = ROW_TOP;
        if (start > 0) { drawMore(c, g, k.bx0, k.bx1, y, true); y += MORE_H; }
        unsigned catColor = kMenuCats[cat_].color;
        for (int i = start; i < end; i++, y += ROW_PITCH) {
            bool sel = i == group_;
            // number badge: category colour, white digit (osd bold)
            fillL(c, k.bx0 + 14, y + 9, k.bx0 + 14 + 32, y + 9 + 32, rgbOf(catColor));
            char num[8]; snprintf(num, sizeof num, "%d", i + 1);
            int nw = widthL(g, GF_OSD_19, num);
            textL(c, g, GF_OSD_19, k.bx0 + 14 + 16 - nw / 2, y + 31, num, kWhite);
            int hx = k.bx0 + 64;
            if (sel) fillL(c, hx, y, k.bx1, y + ROW_PITCH, focus_ == 1 ? kSel : kSelDim);
            Color tc = (sel || focus_ < 1) ? kWhite : kDim;
            int avail = (k.bx1 - hx - 4 - (focus_ < 2 ? 56 : 23));
            char buf[64]; fitText(g, GF_UI_26, grp(cat_, i).name, avail * 3 / 4, buf, sizeof buf);
            textL(c, g, GF_UI_26, hx + 4, y + 34, buf, tc);
            if (focus_ < 2)
                drawChevron(c, (float)(k.bx1 - 25), (float)(y + 26), 12, 6, (sel && focus_ == 1) ? kWhite : kDim);
        }
        if (end < n) drawMore(c, g, k.bx0, k.bx1, y, false);
    }

    void drawItems(Canvas &c, const Gfx &g, const Cols &k, int start, int end, int n) const {
        int y = ROW_TOP;
        if (start > 0) { drawMore(c, g, k.cx0, LOGICAL_W, y, true); y += MORE_H; }
        for (int i = start; i < end; i++, y += ROW_PITCH) {
            int idx = itemIdx(cat_, group_, i);
            const MenuItem &it = kMenuItems[idx];
            bool sel = i == item_, dis = disabled(it);
            if (sel && !dis) fillL(c, k.cx0, y, LOGICAL_W, y + ROW_PITCH, focus_ == 2 ? kSel : kSelDim);
            char vb[64]; valueText(idx, vb, sizeof vb);
            Color lc = dis ? kDisabled : ((sel || focus_ < 2) ? kWhite : kDim);
            Color vc = dis ? kDisabled : ((sel && focus_ == 2) ? kWhite : kDim);
            int vw = g.textWidth(GF_UI_26, vb);
            int vx = LX(LOGICAL_W - 44) - vw;                 // value right edge: row pad 10dp + value pad 10dp
            g.text(c, GF_UI_26, vx, y + 34, vb, vc, false);
            int labelMax = vx - LX(k.cx0 + 23) - LX(14);
            char lb[96]; fitText(g, GF_UI_26, it.label, labelMax, lb, sizeof lb);
            textL(c, g, GF_UI_26, k.cx0 + 23, y + 34, lb, lc);
        }
        if (end < n) drawMore(c, g, k.cx0, LOGICAL_W, y, false);
    }

    // ---------------------------------------------------------- value picker (modal card)
    void drawPicker(Canvas &c, const Gfx &g) const {
        int idx = curItem();
        const MenuItem &it = kMenuItems[idx];
        c.fillRect(0, 0, PANEL_W, PANEL_H, rgba(0, 0, 0, 0xaa));          // modal scrim
        const int VISM = 6;
        int start = clampStart(valSel_, n_, VISM), end = start + VISM < n_ ? start + VISM : n_;
        bool up = start > 0, down = end < n_;
        int rows = end - start;
        // card height: title 62 + rows*48.5 + more rows 20 + hint tail 54.5 (+ border)
        int H = 62 + (rows * 97) / 2 + (up ? MORE_H : 0) + (down ? MORE_H : 0) + 55;
        int top = (PANEL_H - H) / 2;
        int x0 = 161, x1 = 691;
        cardRect(c, x0, top, x1, top + H);
        textL(c, g, GF_UI_27, 192, top + 43, it.label, kWhite);
        int y = top + 62;
        if (up) { drawMore(c, g, x0, x1, y, true); y += MORE_H; }
        char cur[24]; strncpy(cur, val_[idx], sizeof cur - 1); cur[sizeof cur - 1] = 0;
        for (int i = start; i < end; i++) {
            int ry = y + ((i - start) * 97) / 2;
            bool sel = i == valSel_;
            char raw[24], lab[48]; optAt(idx, i, raw, lab, sizeof raw);
            bool isCur = !strcmp(raw, cur);
            if (sel) fillL(c, 191, ry, 660, ry + 50, kSel);
            Color rc = sel ? kWhite : (isCur ? kAccent : kDim);
            {
                Shape rs; memset(&rs, 0, sizeof rs);
                rs.a = 220.0f; rs.b = (float)(ry + 24);
                if (isCur) { rs.kind = SH_DISC; rs.c = 5.5f; } else { rs.kind = SH_RING; rs.c = 5.0f; rs.d = 1.6f; }
                rasterShapes(c, &rs, 1, 212, ry + 14, 230, ry + 34, 0, 0, 0, 255, rc);
            }
            textL(c, g, GF_UI_27, 249, ry + 34, lab, sel ? kWhite : kDim);
        }
        y += (rows * 97) / 2;
        if (down) { drawMore(c, g, x0, x1, y, false); y += MORE_H; }
        const char *h = "\xe2\x96\xb2\xe2\x96\xbc \xec\x84\xa0\xed\x83\x9d   \xe2\x97\x8f \xed\x99\x95\xec\xa0\x95   MENU \xeb\x92\xa4\xeb\xa1\x9c";
        int w = g.textWidth(GF_UI_20, h);
        g.text(c, GF_UI_20, LX(659) - w, y + 30, h, kDim, false);
    }

    // Action confirm / toast card. CONFIRM = destructive action gate (취소 / 실행 rows, 실행 in red);
    // TOAST = one-line result (safe action done / device-seal / gate-inert). Same card chrome as the picker.
    void drawActModal(Canvas &c, const Gfx &g) const {
        c.fillRect(0, 0, PANEL_W, PANEL_H, rgba(0, 0, 0, 0xaa));          // scrim
        bool confirm = (actModal_ == AMK_CONFIRM);
        int x0 = 141, x1 = 711;
        int H = confirm ? 236 : 150;
        int top = (PANEL_H - H) / 2;
        cardRect(c, x0, top, x1, top + H);
        const char *title = actDesc_.title ? actDesc_.title : "";
        textL(c, g, GF_UI_27, x0 + 31, top + 43, title, kWhite);
        if (confirm) {
            if (actDesc_.confirm) textL(c, g, GF_UI_24, x0 + 31, top + 80, actDesc_.confirm, kDim);
            // warning line (brick-0: this is a destructive, EXPERIMENTAL-gated action)
            textL(c, g, GF_UI_20, x0 + 31, top + 108,
                  "\xec\x8b\xa4\xed\x97\x98 \xeb\xaa\xa8\xeb\x93\x9c\xec\x97\x90\xec\x84\x9c\xeb\xa7\x8c \xec\x8b\xa4\xed\x96\x89\xeb\x90\xa9\xeb\x8b\x88\xeb\x8b\xa4.", kAccent);   // "실험 모드에서만 실행됩니다."
            const char *opts[2] = { "\xec\x95\x84\xeb\x8b\x88\xec\x98\xa4(\xec\xb7\xa8\xec\x86\x8c)", "\xec\x98\x88(\xec\x8b\xa4\xed\x96\x89)" };  // "아니오(취소)" / "예(실행)"
            for (int i = 0; i < 2; i++) {
                int ry = top + 130 + i * 48;
                bool sel = (confirmSel_ == i);
                if (sel) fillL(c, x0 + 20, ry, x1 - 20, ry + 42, i == 1 ? kSel : kSelDim);
                Color rc = sel ? kWhite : (i == 1 ? kAccent : kDim);
                textL(c, g, GF_UI_26, x0 + 40, ry + 29, opts[i], rc);
            }
        } else {
            textL(c, g, GF_UI_26, x0 + 31, top + 86, toast_, kAccent);
            const char *h = "\xe2\x97\x8f \xed\x99\x95\xec\x9d\xb8";                               // "● 확인"
            int w = g.textWidth(GF_UI_20, h);
            g.text(c, GF_UI_20, LX(x1 - 20) - w, top + H - 18, h, kDim, false);
        }
    }

    void cardRect(Canvas &c, int x0, int y0, int x1, int y1) const {   // rounded card, 2px border (logical x)
        int px0 = LX(x0), px1 = LX(x1);
        float r = 10.0f;
        for (int py = y0; py < y1; py++) for (int px = px0; px < px1; px++) {
            int cov = 0, covIn = 0;
            for (int sy = 0; sy < 3; sy++) for (int sx = 0; sx < 3; sx++) {
                float lx = (px + (sx + 0.5f) / 3.0f) * 4.0f / 3.0f, ly = py + (sy + 0.5f) / 3.0f;
                float d = sdRR(lx, ly, (float)x0, (float)y0, (float)x1, (float)y1, r);
                if (d <= 0) { cov++; if (d <= -2.0f) covIn++; }
            }
            if (!cov) continue;
            // blend border colour by outer coverage, then fill colour by inner coverage
            c.setPixel(px, py, rgba(kCardLine.r, kCardLine.g, kCardLine.b, (u8)(cov * 255 / 9)));
            if (covIn) c.setPixel(px, py, rgba(kCardBg.r, kCardBg.g, kCardBg.b, (u8)(covIn * 255 / cov / 1)));
        }
    }

private:
    int  focus_, cat_, group_, item_;
    bool modal_;
    int  valSel_, n_;
    bool synced_;
    char val_[MENU_ITEMS][24];
    int  pFocus_, pCat_, pGroup_, pItem_, pStartI_;   // previous-draw state (dirty-region)
    bool dirty_;                                       // force a full redraw next draw()
    // app-launch / action overlay (menu_actions)
    enum ActModalKind { AMK_NONE = 0, AMK_CONFIRM, AMK_TOAST };
    int  actModal_;
    int  confirmSel_;              // AMK_CONFIRM: 0 = cancel, 1 = run
    MenuActionDesc actDesc_;
    char toast_[96];
};

} // namespace

Screen *createMenuScreen() { return new MenuScreen(); }

} // namespace uicore
