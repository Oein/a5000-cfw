#include "uicore/apply.h"
#include "uicore/menu_gen.h"
#include "uicore/catalog.h"
#include "uicore/platform.h"
#include "uicore/log.h"
#include <stdlib.h>
#include <string.h>

namespace uicore {

const char *applyMethodName(int m) {
    switch (m) {
        case AM_LOCAL: return "local"; case AM_ACTION: return "action"; case AM_INPROC: return "inproc";
        case AM_SDCOM: return "sdcom"; case AM_BACKUP: return "backup"; case AM_UNMAPPED: return "unmapped";
    }
    return "?";
}

bool applyResolve(int item, int value, ApplyCmd *o) {
    if (!o || item < 0 || item >= MENU_ITEMS) return false;
    const MenuItem &mi = kMenuItems[item];
    const ApplyEntry &e = kApply[item];
    memset(o, 0, sizeof *o);
    o->item = item; o->method = e.method; o->routes = e.routes;
    o->catalogId = e.catalogId; o->sdcomParam = e.sdcomParam; o->sdcomMsg = e.sdcomMsg; o->backupProp = e.backupProp;
    o->inprocKind = e.inproc; o->inprocGroup = e.inprocGroup;
    bool range = (mi.type == MT_RANGE);
    if (range ? (value < mi.rmin || value > mi.rmax) : (value < 0 || value >= mi.nOpts)) {
        if (e.routes || e.method == AM_UNMAPPED) return false;     // pickable item, bad value
    }
    if (e.routes & AR_SDCOM) {
        if (e.enc == AE_INT) {
            o->sdcomValue = range ? value : atoi(mi.opts[value].raw);
            o->sdcomOk = true;
        } else if (e.enc == AE_OPT && e.sdcomVals && value >= 0 && value < mi.nOpts && e.sdcomVals[value] != APPLY_UNMAPPED_VAL) {
            o->sdcomValue = e.sdcomVals[value]; o->sdcomOk = true;
        }
        if (o->sdcomOk) o->catalogValue = o->sdcomValue;           // catalog "A side" == SDCom value
    }
    if ((e.routes & AR_INPROC) && e.inprocVals && value >= 0 && value < mi.nOpts && e.inprocVals[value] != APPLY_UNMAPPED_VAL) {
        o->inprocValue = e.inprocVals[value]; o->inprocOk = true;
    }
    return true;
}

// Default executor: only the catalog bridge (setSetting) is platform-neutral. A platform
// with real routes (a5000-inproc, future SDCom) overrides applySetting().
int CameraControl::applySetting(int item, int value) {
    ApplyCmd c;
    if (!applyResolve(item, value, &c)) return APPLY_UNMAPPED;
    if (c.catalogId && c.sdcomOk && catalogHasValue(c.catalogId, c.catalogValue))
        return setSetting(c.catalogId, c.catalogValue) ? APPLY_OK : APPLY_REJECTED;
    switch (c.method) {
        case AM_LOCAL: case AM_ACTION: return APPLY_LOCAL;
        case AM_BACKUP: return APPLY_RECORDED;
        case AM_UNMAPPED: return APPLY_UNMAPPED;
    }
    return APPLY_NO_ROUTE;
}

} // namespace uicore
