#include "uicore/keymap.h"
#include "uicore/platform.h"
#include <stdio.h>

namespace uicore {

static Action find(const Binding *t, int n, int raw, int ctx) {
    for (int i = 0; i < n; i++)
        if (t[i].raw == raw && t[i].context == ctx) return (Action)t[i].action;
    return ACT_NONE;
}

Action KeyMap::resolve(int raw, Context ctx) const {
    Action a = find(ov_, ovN_, raw, ctx);
    if (a != ACT_NONE) return a;
    a = find(ov_, ovN_, raw, CTX_GLOBAL);
    if (a != ACT_NONE) return a;
    a = find(base_, baseN_, raw, ctx);
    if (a != ACT_NONE) return a;
    return find(base_, baseN_, raw, CTX_GLOBAL);
}

bool KeyMap::addOverride(int raw, Context ctx, Action action) {
    if (ovN_ >= kMaxOverrides) return false;
    ov_[ovN_].raw = raw; ov_[ovN_].context = ctx; ov_[ovN_].action = action;
    ovN_++;
    return true;
}

// Overrides persist as "keymap.<index>.{raw,ctx,act}" ints. M0: simple count-based.
bool KeyMap::load(Storage *s) {
    if (!s) return false;
    int n = 0;
    if (!s->getInt("keymap.n", &n)) return false;
    ovN_ = 0;
    char k[32];
    for (int i = 0; i < n && ovN_ < kMaxOverrides; i++) {
        int raw = 0, ctx = 0, act = 0;
        snprintf(k, sizeof k, "keymap.%d.raw", i); s->getInt(k, &raw);
        snprintf(k, sizeof k, "keymap.%d.ctx", i); s->getInt(k, &ctx);
        snprintf(k, sizeof k, "keymap.%d.act", i); s->getInt(k, &act);
        addOverride(raw, (Context)ctx, (Action)act);
    }
    return true;
}

bool KeyMap::save(Storage *s) const {
    if (!s) return false;
    s->setInt("keymap.n", ovN_);
    char k[32];
    for (int i = 0; i < ovN_; i++) {
        snprintf(k, sizeof k, "keymap.%d.raw", i); s->setInt(k, ov_[i].raw);
        snprintf(k, sizeof k, "keymap.%d.ctx", i); s->setInt(k, ov_[i].context);
        snprintf(k, sizeof k, "keymap.%d.act", i); s->setInt(k, ov_[i].action);
    }
    return true;
}

} // namespace uicore
