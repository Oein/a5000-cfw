// Font ids used by Gfx (subset of gen/fonts.h FontId; kept here so uicore's public
// headers do not depend on the firmware-derived gen/ directory).
#pragma once
namespace uicore {
enum GfxFont { GF_OSD_15 = 0, GF_OSD_16 = 1, GF_OSD_24 = 2, GF_UI_12 = 3,
              // Track A sizes at the golden's density (1.7): 13sp/15+16sp/24sp -> 21/26/40 px
              GF_OSD_21 = 4, GF_OSD_26 = 5, GF_OSD_40 = 6,
              // M2 menu sizes (golden density 1.7): ui face 11/12/14/15/16sp -> 19/20/24/26/27 px, osd 11sp -> 19
              GF_UI_19 = 7, GF_UI_20 = 8, GF_UI_24 = 9, GF_UI_26 = 10, GF_UI_27 = 11, GF_OSD_19 = 12,
              GF_COUNT = 13 };
}
