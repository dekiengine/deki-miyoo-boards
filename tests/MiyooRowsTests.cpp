// The display's row widening: a screen row as the panel's ARGB8888, each
// pixel scaled up and, on the Miyoo Mini's upside-down panel, turned round.
//
// Built for the desktop these pin the plain path. Built for the Miyoo (the
// same file, cross-compiled), WidenRow takes NEON for ARGB8888 at 1x and 2x,
// and these hold it to the plain path pixel for pixel: every length, so the
// pixels past the last block of four are covered too, both ways round.

#include <gtest/gtest.h>

#include "MiyooRows.h"

#include <deki/Neon.h>

#include <cstring>
#include <vector>

using namespace DekiMiyoo;
using Deki::ColorFormat;

namespace
{
// Pixels that differ in every byte, alpha included, so a lost or swapped
// byte, a pixel out of place and an alpha left unset all show.
std::vector<uint8_t> Pattern(int count, ColorFormat format)
{
    std::vector<uint8_t> src(static_cast<size_t>(count) * BytesPerPixel(format));
    uint32_t x = 0x12345678u;
    for (uint8_t& b : src)
    {
        x = x * 1664525u + 1013904223u;
        b = static_cast<uint8_t>(x >> 24);
    }
    return src;
}

// A row with a guard pixel each side, which nothing may write.
constexpr uint32_t kGuard = 0xDEADBEEFu;

std::vector<uint32_t> Widen(bool neonAllowed, const std::vector<uint8_t>& src, ColorFormat format, int count,
                            int32_t scale, bool reversed)
{
    std::vector<uint32_t> out(static_cast<size_t>(count) * scale + 2, kGuard);
    if (neonAllowed)
    {
        WidenRow(src.data(), format, count, scale, reversed, out.data() + 1);
    }
    else
    {
        WidenRowPlain(src.data(), format, count, scale, reversed, out.data() + 1);
    }
    return out;
}
}  // namespace

TEST(MiyooRows, ArgbIsMadeOpaqueAndScaled)
{
    const uint8_t src[8] = { 0x11, 0x22, 0x33, 0x00, 0x44, 0x55, 0x66, 0x7F };  // two pixels, little-endian
    uint32_t out[4] = {};
    WidenRowPlain(src, ColorFormat::ARGB8888, 2, 2, false, out);
    EXPECT_EQ(out[0], 0xFF332211u);
    EXPECT_EQ(out[1], 0xFF332211u);
    EXPECT_EQ(out[2], 0xFF665544u);
    EXPECT_EQ(out[3], 0xFF665544u);
}

TEST(MiyooRows, TurnedRoundWritesFromTheFarEnd)
{
    const uint8_t src[12] = { 1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0 };
    uint32_t out[3] = {};
    WidenRowPlain(src, ColorFormat::ARGB8888, 3, 1, true, out);
    EXPECT_EQ(out[0], 0xFF000003u);
    EXPECT_EQ(out[1], 0xFF000002u);
    EXPECT_EQ(out[2], 0xFF000001u);
}

TEST(MiyooRows, Rgb565WidensToFullRange)
{
#ifdef DEKI_RGB565_BIG_ENDIAN
    const uint8_t src[4] = { 0xFF, 0xFF, 0xF8, 0x00 };
#else
    const uint8_t src[4] = { 0xFF, 0xFF, 0x00, 0xF8 };
#endif
    uint32_t out[2] = {};
    WidenRowPlain(src, ColorFormat::RGB565, 2, 1, false, out);
    EXPECT_EQ(out[0], 0xFFFFFFFFu);  // white stays white, not 0xF8FCF8
    EXPECT_EQ(out[1], 0xFFFF0000u);  // pure red
}

TEST(MiyooRows, NeonOnlyWhereTheCoreHasIt)
{
    EXPECT_EQ(WidensWithNeon(ColorFormat::ARGB8888, 2), DEKI_NEON == 1);
    EXPECT_EQ(WidensWithNeon(ColorFormat::ARGB8888, 1), DEKI_NEON == 1);
    EXPECT_FALSE(WidensWithNeon(ColorFormat::ARGB8888, 3));
    EXPECT_FALSE(WidensWithNeon(ColorFormat::RGB565, 2));
}

// The Miyoo Mini's own case (320 at 2x, turned) and every length round it:
// each pixel the same as the plain path, and nothing outside the row written.
TEST(MiyooRows, WidenRowMatchesThePlainPathPixelForPixel)
{
    int compared = 0;
    for (ColorFormat format : { ColorFormat::ARGB8888, ColorFormat::RGB565, ColorFormat::RGB888 })
    {
        for (int32_t scale = 1; scale <= 3; ++scale)
        {
            for (bool reversed : { false, true })
            {
                for (int count = 0; count <= 70; ++count)
                {
                    for (int length : { count, count + 250 })
                    {
                        const std::vector<uint8_t> src = Pattern(length, format);
                        const std::vector<uint32_t> plain = Widen(false, src, format, length, scale, reversed);
                        const std::vector<uint32_t> chosen = Widen(true, src, format, length, scale, reversed);
                        ASSERT_EQ(plain, chosen) << "format " << static_cast<int>(format) << ", " << length
                                                 << " pixels at " << scale << "x" << (reversed ? ", turned" : "");
                        EXPECT_EQ(chosen.front(), kGuard);
                        EXPECT_EQ(chosen.back(), kGuard);
                        ++compared;
                    }
                }
            }
        }
    }
    EXPECT_EQ(compared, 3 * 3 * 2 * 71 * 2);
}
