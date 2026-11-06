#include "MiyooDisplay.h"

// The framebuffer exists only on the device. The editor and other hosts
// compile the package for its components and get an empty display.
#if defined(__linux__) && !defined(DEKI_EDITOR)

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <memory>

#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <deki/LogSystem.h>
#include <deki/Time.h>
#include "MiyooRows.h"
#include "MiyooTimeProvider.h"

namespace DekiMiyoo
{

namespace
{
// The device supplies the engine's time source (CLOCK_MONOTONIC). A static
// initializer registers it, so Deki::Time has a provider before main(), as
// SDL3Display.cpp does for SDL3.
struct MiyooTimeInit
{
    MiyooTimeInit() { Deki::Time::SetTimeProvider(std::make_unique<MiyooTimeProvider>()); }
};
static MiyooTimeInit s_MiyooTimeInit;
}  // namespace

MiyooDisplay::~MiyooDisplay()
{
    Shutdown();
}

bool MiyooDisplay::Initialize(int32_t width, int32_t height)
{
    if (m_Initialized)
    {
        return true;
    }
    m_DisplayWidth = width;
    m_DisplayHeight = height;

    m_Fd = open("/dev/fb0", O_RDWR);
    if (m_Fd < 0)
    {
        DEKI_LOG_ERROR("MiyooDisplay: cannot open /dev/fb0");
        return false;
    }

    fb_var_screeninfo var{};
    fb_fix_screeninfo fix{};
    if (ioctl(m_Fd, FBIOGET_VSCREENINFO, &var) != 0 || ioctl(m_Fd, FBIOGET_FSCREENINFO, &fix) != 0)
    {
        DEKI_LOG_ERROR("MiyooDisplay: cannot read the framebuffer's mode");
        Shutdown();
        return false;
    }
    if (var.bits_per_pixel != 32)
    {
        DEKI_LOG_ERROR("MiyooDisplay: framebuffer is %u bits per pixel, expected 32", var.bits_per_pixel);
        Shutdown();
        return false;
    }

    // Two pages to flip between. The firmware usually leaves more; ask for
    // two when it left one.
    if (var.yres_virtual < var.yres * 2)
    {
        fb_var_screeninfo want = var;
        want.yres_virtual = var.yres * 2;
        want.yoffset = 0;
        if (ioctl(m_Fd, FBIOPUT_VSCREENINFO, &want) == 0)
        {
            ioctl(m_Fd, FBIOGET_VSCREENINFO, &var);
            ioctl(m_Fd, FBIOGET_FSCREENINFO, &fix);
        }
    }

    m_PanelWidth = static_cast<int32_t>(var.xres);
    m_PanelHeight = static_cast<int32_t>(var.yres);
    m_Stride = fix.line_length;
    m_MapSize = fix.smem_len;
    m_Pages = std::max<int32_t>(1, static_cast<int32_t>(var.yres_virtual / var.yres));
    m_Pages = std::min<int32_t>(m_Pages, static_cast<int32_t>(m_MapSize / (m_Stride * var.yres)));

    void* map = mmap(nullptr, m_MapSize, PROT_READ | PROT_WRITE, MAP_SHARED, m_Fd, 0);
    if (map == MAP_FAILED)
    {
        DEKI_LOG_ERROR("MiyooDisplay: cannot map the framebuffer (%u bytes)", m_MapSize);
        Shutdown();
        return false;
    }
    m_Map = static_cast<uint8_t*>(map);

    ClearAllPages();
    m_BackPage = m_Pages > 1 ? 1 : 0;
    MeasureRefresh();
    const size_t frameBytes = static_cast<size_t>(width) * height * BytesPerPixel(m_Format);
    for (Frame& frame : m_Frames)
    {
        frame.pixels.assign(frameBytes, 0);
        frame.width = width;
        frame.height = height;
        frame.format = m_Format;
    }
    m_Free = 0;
    m_Pending = -1;
    m_Drawing = -1;
    m_SaidCopying = false;
    m_Stop = false;
    m_Thread = std::thread(&MiyooDisplay::PresentLoop, this);
    m_Initialized = true;

    DEKI_LOG_INFO("MiyooDisplay: panel %dx%d, %d page(s); screen %dx%d, drawn in %d buffers lent to the game "
                  "and presented on its own thread",
                  (int)m_PanelWidth, (int)m_PanelHeight, (int)m_Pages, (int)width, (int)height, kFrames);
    const int32_t scale = m_Scale > 0 ? m_Scale : std::max<int32_t>(1, std::min(m_PanelWidth / width, m_PanelHeight / height));
    DEKI_LOG_INFO("MiyooDisplay: rows widened %dx with %s", (int)scale,
                  WidensWithNeon(m_Format, scale) ? "NEON" : "plain C++ (NEON covers ARGB8888 at 1x and 2x)");
    return true;
}

void MiyooDisplay::MeasureRefresh()
{
    m_RefreshUs = 0;
    if (m_Pages < 2)
    {
        DEKI_LOG_WARNING("MiyooDisplay: one framebuffer page, so frames are shown as they come and the game is "
                         "paced by its clock alone");
        return;
    }
    // A flip waits for the panel's next refresh, so flips back to back (the
    // pages are blank) finish a refresh apart. The middle of the gaps is the
    // period; a late wake-up only lengthens one.
    constexpr int kFlips = 9;
    uint64_t done[kFlips];
    fb_var_screeninfo var{};
    ioctl(m_Fd, FBIOGET_VSCREENINFO, &var);
    for (int i = 0; i < kFlips; ++i)
    {
        var.yoffset = static_cast<uint32_t>((i % m_Pages) * m_PanelHeight);
        ioctl(m_Fd, FBIOPAN_DISPLAY, &var);
        timespec t{};
        clock_gettime(CLOCK_MONOTONIC, &t);
        done[i] = static_cast<uint64_t>(t.tv_sec) * 1000000u + static_cast<uint64_t>(t.tv_nsec) / 1000u;
    }
    uint64_t gaps[kFlips - 1];
    for (int i = 1; i < kFlips; ++i)
    {
        gaps[i - 1] = done[i] - done[i - 1];
    }
    std::sort(gaps, gaps + kFlips - 1);
    const uint64_t period = gaps[(kFlips - 1) / 2];
    m_BackPage = kFlips % m_Pages;  // the page after the last one shown

    // A panel refreshes between 30 and 120 times a second. Gaps outside that
    // mean flips do not wait for it, and the clock alone paces the game.
    if (period < 8000 || period > 34000)
    {
        DEKI_LOG_WARNING("MiyooDisplay: flips do not wait for the panel's refresh (%u us apart), so the game is "
                         "paced by its clock alone and may tear",
                         static_cast<unsigned>(period));
        return;
    }
    m_RefreshUs = static_cast<uint32_t>(period);
    DEKI_LOG_INFO("MiyooDisplay: panel refreshes every %u us (%.2f Hz); frames are paced to it",
                  static_cast<unsigned>(m_RefreshUs), 1000000.0 / m_RefreshUs);
}

void MiyooDisplay::Shutdown()
{
    if (m_Thread.joinable())
    {
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Stop = true;
        }
        m_Changed.notify_all();
        m_Thread.join();
    }
    if (m_Map)
    {
        // Leave the firmware a black screen on its first page.
        ClearAllPages();
        if (m_Fd >= 0)
        {
            fb_var_screeninfo var{};
            if (ioctl(m_Fd, FBIOGET_VSCREENINFO, &var) == 0)
            {
                var.yoffset = 0;
                ioctl(m_Fd, FBIOPAN_DISPLAY, &var);
            }
        }
        munmap(m_Map, m_MapSize);
        m_Map = nullptr;
    }
    if (m_Fd >= 0)
    {
        close(m_Fd);
        m_Fd = -1;
    }
    m_Initialized = false;
}

void MiyooDisplay::ClearAllPages()
{
    if (m_Map)
    {
        std::memset(m_Map, 0, static_cast<size_t>(m_Stride) * m_PanelHeight * m_Pages);
    }
}

uint8_t* MiyooDisplay::GetRenderBuffer(int32_t* width, int32_t* height)
{
    if (!m_Initialized)
    {
        return nullptr;
    }
    if (width)
    {
        *width = m_DisplayWidth;
    }
    if (height)
    {
        *height = m_DisplayHeight;
    }
    return m_Frames[m_Free].pixels.data();
}

void MiyooDisplay::Present(const uint8_t* framebuffer, int width, int height, Deki::ColorFormat format)
{
    if (!m_Initialized || !framebuffer || width <= 0 || height <= 0)
    {
        return;
    }

    // Wait while the thread still has the frame before this one waiting. The
    // free buffer is the game's alone, so it is filled outside the lock.
    {
        std::unique_lock<std::mutex> lock(m_Mutex);
        m_Changed.wait(lock, [this] { return m_Pending < 0; });
    }
    Frame& frame = m_Frames[m_Free];
    if (framebuffer != frame.pixels.data())
    {
        // Drawn in a buffer of the game's own (it did not take the one lent
        // to it): copied into the free one.
        const size_t bytes = static_cast<size_t>(width) * height * BytesPerPixel(format);
        if (!m_SaidCopying)
        {
            m_SaidCopying = true;
            DEKI_LOG_INFO("MiyooDisplay: the game draws into a buffer of its own, so each frame is copied (%u bytes)",
                          static_cast<unsigned>(bytes));
        }
        frame.pixels.resize(bytes);
        std::memcpy(frame.pixels.data(), framebuffer, bytes);
    }
    frame.width = width;
    frame.height = height;
    frame.format = format;

    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Pending = m_Free;
        for (int i = 0; i < kFrames; ++i)
        {
            if (i != m_Pending && i != m_Drawing)
            {
                m_Free = i;
                break;
            }
        }
    }
    m_Changed.notify_all();
}

void MiyooDisplay::PresentLoop()
{
    for (;;)
    {
        int drawing;
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_Changed.wait(lock, [this] { return m_Pending >= 0 || m_Stop; });
            if (m_Stop)
            {
                return;
            }
            drawing = m_Drawing = m_Pending;
            m_Pending = -1;
        }
        m_Changed.notify_all();
        Draw(m_Frames[drawing]);
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Drawing = -1;
        }
    }
}

void MiyooDisplay::Draw(const Frame& frame)
{
    int32_t scale = m_Scale;
    if (scale == 0)
    {
        scale = std::max<int32_t>(1, std::min(m_PanelWidth / frame.width, m_PanelHeight / frame.height));
    }
    const int32_t outW = std::min<int32_t>(frame.width * scale, m_PanelWidth);
    const int32_t outH = std::min<int32_t>(frame.height * scale, m_PanelHeight);
    const int32_t originX = (m_PanelWidth - outW) / 2;
    const int32_t originY = (m_PanelHeight - outH) / 2;
    const int count = outW / scale;
    const size_t srcStride = static_cast<size_t>(frame.width) * BytesPerPixel(frame.format);

    uint8_t* page = m_Map + static_cast<size_t>(m_BackPage) * m_Stride * m_PanelHeight;

    // Each screen row widened straight into the `scale` panel rows it covers.
    for (int32_t y = 0; y < frame.height && y * scale < outH; ++y)
    {
        const uint8_t* src = frame.pixels.data() + static_cast<size_t>(y) * srcStride;
        for (int32_t dy = 0; dy < scale && y * scale + dy < outH; ++dy)
        {
            int32_t panelY = originY + y * scale + dy;
            int32_t panelX = originX;
            if (m_Rotate180)
            {
                panelY = m_PanelHeight - 1 - panelY;
                panelX = m_PanelWidth - originX - count * scale;
            }
            uint32_t* out = reinterpret_cast<uint32_t*>(page + static_cast<size_t>(panelY) * m_Stride +
                                                        static_cast<size_t>(panelX) * 4);
            WidenRow(src, frame.format, count, scale, m_Rotate180, out);
        }
    }

    if (m_Pages > 1)
    {
        // A frame stays up as many refreshes as the game asks (two at half
        // the panel's rate): one that came early is held, not shown at once
        // and followed by a long one. A flip waits for the next refresh, so
        // it is sent after the last refresh before the one it is due on.
        const uint32_t refreshes = m_RefreshesPerFrame.load(std::memory_order_relaxed);
        if (m_RefreshUs > 0 && refreshes > 1 && m_LastShownUs != 0)
        {
            const uint64_t sendAt = m_LastShownUs + (refreshes - 1) * static_cast<uint64_t>(m_RefreshUs) +
                                    m_RefreshUs / 4;
            timespec t{};
            clock_gettime(CLOCK_MONOTONIC, &t);
            const uint64_t now = static_cast<uint64_t>(t.tv_sec) * 1000000u + static_cast<uint64_t>(t.tv_nsec) / 1000u;
            if (sendAt > now)
            {
                std::this_thread::sleep_for(std::chrono::microseconds(sendAt - now));
            }
        }
        fb_var_screeninfo var{};
        if (ioctl(m_Fd, FBIOGET_VSCREENINFO, &var) == 0)
        {
            var.yoffset = static_cast<uint32_t>(m_BackPage * m_PanelHeight);
            ioctl(m_Fd, FBIOPAN_DISPLAY, &var);
        }
        m_BackPage = (m_BackPage + 1) % m_Pages;
        if (m_RefreshUs > 0)
        {
            CountShown();
        }
    }

    // Up now: after the flip, which returns on the refresh that shows it, or
    // as written where there is one page.
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    const uint64_t shownUs = static_cast<uint64_t>(t.tv_sec) * 1000000u + static_cast<uint64_t>(t.tv_nsec) / 1000u;
    std::lock_guard<std::mutex> lock(m_ShownMutex);
    ++m_FramesShown;
    m_FrameShownAtUs = shownUs;
}

bool MiyooDisplay::GetFramesShown(uint64_t* frames, uint64_t* lastShownUs) const
{
    std::lock_guard<std::mutex> lock(m_ShownMutex);
    if (frames)
    {
        *frames = m_FramesShown;
    }
    if (lastShownUs)
    {
        *lastShownUs = m_FrameShownAtUs;
    }
    return m_Initialized;
}

void MiyooDisplay::CountShown()
{
    // A flip returns on the refresh that shows its frame, so the gap between
    // two is how many refreshes the earlier frame stayed on screen. A steady
    // game keeps every frame up as long as the rest (one refresh at 60, two
    // at 30); a frame that stays longer is a hitch you can see.
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    const uint64_t now = static_cast<uint64_t>(t.tv_sec) * 1000000u + static_cast<uint64_t>(t.tv_nsec) / 1000u;
    if (m_LastShownUs != 0)
    {
        const uint32_t refreshes =
            std::max<uint32_t>(1u, static_cast<uint32_t>((now - m_LastShownUs + m_RefreshUs / 2) / m_RefreshUs));
        const uint32_t slot = std::min<uint32_t>(refreshes, kShownSlots) - 1;
        ++m_ShownFor[slot];
    }
    else
    {
        m_WindowStartUs = now;
    }
    m_LastShownUs = now;

    constexpr uint64_t kWindowUs = 2000000u;
    if (now - m_WindowStartUs < kWindowUs)
    {
        return;
    }
    // The usual time on screen is the most common one; frames that stayed
    // longer are the hitches.
    uint32_t usual = 0;
    uint32_t frames = 0;
    for (uint32_t i = 0; i < kShownSlots; ++i)
    {
        frames += m_ShownFor[i];
        if (m_ShownFor[i] > m_ShownFor[usual])
        {
            usual = i;
        }
    }
    uint32_t late = 0;
    uint32_t longest = usual;
    for (uint32_t i = usual + 1; i < kShownSlots; ++i)
    {
        late += m_ShownFor[i];
        if (m_ShownFor[i] > 0)
        {
            longest = i;
        }
    }
    const double refreshMs = m_RefreshUs / 1000.0;
    if (late > 0)
    {
        DEKI_LOG_WARNING("MiyooDisplay: %u of %u frames in the last 2 s stayed on screen longer than the others (up "
                         "to %.1f ms, against %.1f ms): the game missed the panel's refresh, a hitch you can see",
                         late, frames, (longest + 1) * refreshMs, (usual + 1) * refreshMs);
    }
    DEKI_LOG_INTERNAL("MiyooDisplay: %u frames shown in the last 2 s, each for %.1f ms, %u for longer", frames,
                      (usual + 1) * refreshMs, late);
    for (uint32_t& count : m_ShownFor)
    {
        count = 0;
    }
    m_WindowStartUs = now;
}

void MiyooDisplay::GetDisplaySize(int32_t* width, int32_t* height) const
{
    if (width)
    {
        *width = m_DisplayWidth;
    }
    if (height)
    {
        *height = m_DisplayHeight;
    }
}

bool MiyooDisplay::ProcessEvents()
{
    // MiyooInput sends the quit (the MENU button) through DekiInput, and
    // DekiInput::ShouldExit() detects it, so this only keeps the program running.
    return true;
}

}  // namespace DekiMiyoo

#else  // not on the device

namespace DekiMiyoo
{

MiyooDisplay::~MiyooDisplay() = default;
bool MiyooDisplay::Initialize(int32_t, int32_t)
{
    return false;
}
void MiyooDisplay::Shutdown() {}
void MiyooDisplay::Present(const uint8_t*, int, int, Deki::ColorFormat) {}
uint8_t* MiyooDisplay::GetRenderBuffer(int32_t*, int32_t*)
{
    return nullptr;
}
bool MiyooDisplay::GetFramesShown(uint64_t*, uint64_t*) const
{
    return false;
}
void MiyooDisplay::GetDisplaySize(int32_t* width, int32_t* height) const
{
    if (width)
    {
        *width = 0;
    }
    if (height)
    {
        *height = 0;
    }
}
bool MiyooDisplay::ProcessEvents()
{
    return true;
}
void MiyooDisplay::ClearAllPages() {}

}  // namespace DekiMiyoo

#endif
