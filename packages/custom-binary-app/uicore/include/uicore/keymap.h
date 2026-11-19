// KeyMap resolves (raw code, context) -> Action using a platform-supplied binding
// table (data). Per-context rules win over CTX_GLOBAL. User remaps (overrides) win
// over both and can be persisted via Storage later.
#pragma once
#include "uicore/action.h"

namespace uicore {

class Storage;

class KeyMap {
public:
    KeyMap() : base_(0), baseN_(0), ovN_(0) {}
    void setTable(const Binding *bindings, int count) { base_ = bindings; baseN_ = count; }

    // Returns the mapped action, or ACT_NONE. Looks up: overrides, then (raw,ctx),
    // then (raw, CTX_GLOBAL).
    Action resolve(int raw, Context ctx) const;

    // User remapping (M1+). Overrides are checked first.
    bool addOverride(int raw, Context ctx, Action action);
    void clearOverrides() { ovN_ = 0; }

    // Persist/restore overrides via Storage (key prefix "keymap."). Stubs for M0.
    bool load(Storage *s);
    bool save(Storage *s) const;

private:
    enum { kMaxOverrides = 32 };
    const Binding *base_;
    int            baseN_;
    Binding        ov_[kMaxOverrides];
    int            ovN_;
};

} // namespace uicore
