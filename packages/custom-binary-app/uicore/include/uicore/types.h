// uicore basic types — STL-free, exception-free. Portable across a5000 (bionic/
// glibc) and a6400 (im.elf mod loader) and the host simulator.
#pragma once
#include <stdint.h>

namespace uicore {

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t  i32;

// Physical panel = 640x480. Logical UI space = 853x480, squeezed on X by 0.75
// (Track A SqueezeImageView). LX() maps a logical X to a physical X.
enum { PANEL_W = 640, PANEL_H = 480, LOGICAL_W = 853 };
static inline int LX(int logical_x) { return (logical_x * 3) / 4; }

struct Rect { int x, y, w, h; };
struct Color { u8 r, g, b, a; };
static inline Color rgb(u8 r, u8 g, u8 b)        { Color c = { r, g, b, 255 }; return c; }
static inline Color rgba(u8 r, u8 g, u8 b, u8 a) { Color c = { r, g, b, a   }; return c; }
static inline Color sonyOrange() { return rgb(0xD3, 0x41, 0x00); }   // #D34100 accent

} // namespace uicore
