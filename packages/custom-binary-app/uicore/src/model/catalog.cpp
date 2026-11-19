#include "uicore/catalog.h"

namespace uicore {

const SettingDef *catalogDef(int id) {
    return (id > SET_NONE && id < SET_COUNT) ? &kCatalog[id] : 0;
}

const SettingDef *catalogByParam(int paramId) {
    for (int i = 1; i < SET_COUNT; i++)
        if (kCatalog[i].paramId == paramId) return &kCatalog[i];
    return 0;
}

const char *catalogLabel(int id, int value) {
    const SettingDef *d = catalogDef(id);
    if (!d) return 0;
    for (int i = 0; i < d->nVals; i++)
        if (d->vals[i].value == value) return d->vals[i].label;
    return 0;
}

bool catalogHasValue(int id, int value) {
    const SettingDef *d = catalogDef(id);
    if (!d) return false;
    if (d->kind == SK_INT) return value >= d->minv && value <= d->maxv;
    for (int i = 0; i < d->nVals; i++)
        if (d->vals[i].value == value) return true;
    return false;
}

bool catalogNeighbor(int id, int cur, int dir, int *out) {
    const SettingDef *d = catalogDef(id);
    if (!d || !out || dir == 0) return false;
    if (d->kind == SK_INT) {
        int n = cur + (dir > 0 ? 1 : -1);
        if (n < d->minv || n > d->maxv) return false;
        *out = n; return true;
    }
    if (d->kind != SK_ENUM && d->kind != SK_LIST) return false;
    // List order = table order (ISO/self-timer tables are ordered by the TSV).
    int idx = -1;
    for (int i = 0; i < d->nVals; i++) if (d->vals[i].value == cur) { idx = i; break; }
    if (idx < 0) { *out = d->vals[dir > 0 ? 0 : d->nVals - 1].value; return d->nVals > 0; }
    int n = idx + (dir > 0 ? 1 : -1);
    if (n < 0 || n >= d->nVals) return false;
    *out = d->vals[n].value;
    return true;
}

} // namespace uicore
