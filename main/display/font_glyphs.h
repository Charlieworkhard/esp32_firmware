#ifndef FONT_GLYPHS_H
#define FONT_GLYPHS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 8x16 ASCII 字模（列优先，每列 1 字节，bit0 在上）。
extern const uint8_t kAsciiFont[95][16];

// 16x16 中文字模（列优先，每列 2 字节，先上后下）。
extern const int kHanziCount;
extern const uint32_t kHanziCodepoints[];
extern const uint8_t kHanziFont[][32];

#ifdef __cplusplus
}
#endif

#endif  // FONT_GLYPHS_H
