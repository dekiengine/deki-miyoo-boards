#pragma once

#include <cstddef>
#include <cstdint>

#include <deki/Engine.h>  // ColorFormat

namespace DekiMiyoo
{

/// Bytes per pixel of a frame in `format`.
size_t BytesPerPixel(Deki::ColorFormat format);

/// One screen row of `count` pixels in `format` as the panel's ARGB8888, each
/// pixel `scale` times: written forward from out[0] or, turned 180 degrees,
/// backward from out[count * scale - 1]. An ARGB8888 row at scale 1 or 2 is
/// widened with NEON where the core has it (WidensWithNeon), any other row by
/// the plain path; both give the same pixels.
void WidenRow(const uint8_t* src, Deki::ColorFormat format, int count, int32_t scale, bool reversed, uint32_t* out);

/// The plain C++ path WidenRow takes without NEON, for every format and scale.
void WidenRowPlain(const uint8_t* src, Deki::ColorFormat format, int count, int32_t scale, bool reversed,
                   uint32_t* out);

/// Whether WidenRow widens rows of this format and scale with NEON.
bool WidensWithNeon(Deki::ColorFormat format, int32_t scale);

}  // namespace DekiMiyoo
