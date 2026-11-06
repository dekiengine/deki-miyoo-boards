#include "MiyooRows.h"

#include <cstring>

#include <deki/Neon.h>

namespace DekiMiyoo
{

namespace
{
constexpr uint32_t kOpaque = 0xFF000000u;

uint32_t From565(uint32_t p)
{
    const uint32_t r = (p >> 11) & 0x1F;
    const uint32_t g = (p >> 5) & 0x3F;
    const uint32_t b = p & 0x1F;
    return kOpaque | (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
}

// Writes `count` source pixels, each `scale` times, forward from out[0] or,
// turned, backward from out[last]. `Read(i)` is source pixel i as ARGB8888.
template <typename Read>
void WriteRow(int count, int32_t scale, bool reversed, uint32_t* out, Read read)
{
    const int32_t step = reversed ? -1 : 1;
    uint32_t* o = reversed ? out + static_cast<size_t>(count) * scale - 1 : out;
    for (int i = 0; i < count; ++i)
    {
        const uint32_t c = read(i);
        for (int32_t s = 0; s < scale; ++s, o += step)
        {
            *o = c;
        }
    }
}

#if DEKI_NEON
// Four ARGB8888 pixels at a time, made opaque; reversed, the four turned
// round as well and stored from the far end. The pixels left over past a
// multiple of four take the plain path, at their own place in the row.
template <int32_t Scale>
void WidenArgbNeon(const uint8_t* src, int count, bool reversed, uint32_t* out)
{
    const uint32x4_t opaque = vdupq_n_u32(kOpaque);
    const int blocks = count & ~3;
    for (int i = 0; i < blocks; i += 4)
    {
        uint32x4_t v = vorrq_u32(vreinterpretq_u32_u8(vld1q_u8(src + static_cast<size_t>(i) * 4)), opaque);
        int at = i;
        if (reversed)
        {
            const uint32x4_t pairs = vrev64q_u32(v);  // 1 0 3 2
            v = vcombine_u32(vget_high_u32(pairs), vget_low_u32(pairs));
            at = count - 4 - i;
        }
        if (Scale == 1)
        {
            vst1q_u32(out + at, v);
        }
        else
        {
            uint32x4x2_t twice;
            twice.val[0] = v;
            twice.val[1] = v;
            vst2q_u32(out + static_cast<size_t>(at) * 2, twice);  // a a b b c c d d
        }
    }
    const int rest = count - blocks;
    if (rest > 0)
    {
        const uint8_t* tail = src + static_cast<size_t>(blocks) * 4;
        uint32_t* tailOut = reversed ? out : out + static_cast<size_t>(blocks) * Scale;
        WriteRow(rest, Scale, reversed, tailOut, [tail](int i) {
            uint32_t v;
            std::memcpy(&v, tail + i * 4, 4);
            return kOpaque | v;
        });
    }
}
#endif
}  // namespace

size_t BytesPerPixel(Deki::ColorFormat format)
{
    switch (format)
    {
        case Deki::ColorFormat::RGB565:
            return 2;
        case Deki::ColorFormat::RGB565A8:
        case Deki::ColorFormat::RGB888:
            return 3;
        case Deki::ColorFormat::ARGB8888:
        default:
            return 4;
    }
}

void WidenRowPlain(const uint8_t* src, Deki::ColorFormat format, int count, int32_t scale, bool reversed,
                   uint32_t* out)
{
    switch (format)
    {
        case Deki::ColorFormat::RGB565:
#ifdef DEKI_RGB565_BIG_ENDIAN
            WriteRow(count, scale, reversed, out, [src](int i) { return From565((src[i * 2] << 8) | src[i * 2 + 1]); });
#else
            WriteRow(count, scale, reversed, out, [src](int i) { return From565(src[i * 2] | (src[i * 2 + 1] << 8)); });
#endif
            break;
        case Deki::ColorFormat::RGB565A8:
            WriteRow(count, scale, reversed, out, [src](int i) { return From565(src[i * 3] | (src[i * 3 + 1] << 8)); });
            break;
        case Deki::ColorFormat::RGB888:
            WriteRow(count, scale, reversed, out, [src](int i) {
                const uint8_t* p = src + i * 3;
                return kOpaque | (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
            });
            break;
        case Deki::ColorFormat::ARGB8888:
        default:
            WriteRow(count, scale, reversed, out, [src](int i) {
                uint32_t v;
                std::memcpy(&v, src + i * 4, 4);
                return kOpaque | v;
            });
            break;
    }
}

bool WidensWithNeon(Deki::ColorFormat format, int32_t scale)
{
    return DEKI_NEON && format == Deki::ColorFormat::ARGB8888 && (scale == 1 || scale == 2);
}

void WidenRow(const uint8_t* src, Deki::ColorFormat format, int count, int32_t scale, bool reversed, uint32_t* out)
{
#if DEKI_NEON
    if (WidensWithNeon(format, scale))
    {
        if (scale == 1)
        {
            WidenArgbNeon<1>(src, count, reversed, out);
        }
        else
        {
            WidenArgbNeon<2>(src, count, reversed, out);
        }
        return;
    }
#endif
    WidenRowPlain(src, format, count, scale, reversed, out);
}

}  // namespace DekiMiyoo
