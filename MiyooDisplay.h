#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include <deki/Engine.h>  // ColorFormat
#include <deki/providers/IDisplay.h>

namespace DekiMiyoo
{

/// The Linux framebuffer (/dev/fb0) of a Miyoo handheld. The game renders at
/// its own screen size; each present widens it to the panel's ARGB8888, scales
/// it up by whole pixels, centres it, and flips between the framebuffer's
/// pages, so a frame is never shown half drawn. The Miyoo Mini's panel is
/// mounted upside down, so frames are turned 180 degrees by default.
///
/// A flip waits for the panel's refresh. So the game is not held up by it,
/// the game draws into buffers the display lends it (GetRenderBuffer), and
/// Present only hands the drawn one over and returns; a present thread
/// widens it into the next page and flips. The game waits only when the
/// thread still has the frame before it, so it runs at the panel's rate at
/// most.
class MiyooDisplay : public Deki::IDisplay
{
private:
    int m_Fd = -1;
    uint8_t* m_Map = nullptr;  // the framebuffer's memory, every page
    uint32_t m_MapSize = 0;

    // The panel, as the framebuffer device reports it.
    int32_t m_PanelWidth = 0;
    int32_t m_PanelHeight = 0;
    uint32_t m_Stride = 0;  // bytes per panel row
    int32_t m_Pages = 1;
    int32_t m_BackPage = 0;

    // The game's screen, which every frame has.
    int32_t m_DisplayWidth = 0;
    int32_t m_DisplayHeight = 0;
    Deki::ColorFormat m_Format = Deki::ColorFormat::RGB565;

    int32_t m_Scale = 0;  // 0: as large as fits by whole pixels
    bool m_Rotate180 = true;
    bool m_Initialized = false;
    uint32_t m_RefreshUs = 0;  // the panel's refresh period, 0 when flips do not wait for it
    std::atomic<uint32_t> m_RefreshesPerFrame{1};  // set by the game, read by the present thread

    // How long frames stayed on screen, in refreshes (1, 2, 3, 4 or more),
    // counted by the present thread over a 2-second window.
    static constexpr uint32_t kShownSlots = 4;
    uint32_t m_ShownFor[kShownSlots] = {};
    uint64_t m_LastShownUs = 0;
    uint64_t m_WindowStartUs = 0;

    // Frames on the panel and when the latest went up, for GetFramesShown.
    mutable std::mutex m_ShownMutex;
    uint64_t m_FramesShown = 0;
    uint64_t m_FrameShownAtUs = 0;

    // The game's frames, in three buffers lent to it: the one it draws into,
    // one waiting for the present thread, and the one the thread is drawing.
    // A frame the game drew in a buffer of its own is copied into the free one.
    struct Frame
    {
        std::vector<uint8_t> pixels;
        int width = 0;
        int height = 0;
        Deki::ColorFormat format = Deki::ColorFormat::RGB565;
    };
    static constexpr int kFrames = 3;
    Frame m_Frames[kFrames];
    int m_Free = 0;      // the game's; Present moves it
    int m_Pending = -1;  // under m_Mutex; -1 for none
    int m_Drawing = -1;  // under m_Mutex; -1 for none
    bool m_SaidCopying = false;
    bool m_Stop = false;
    std::mutex m_Mutex;
    std::condition_variable m_Changed;
    std::thread m_Thread;

public:
    MiyooDisplay() = default;
    ~MiyooDisplay() override;

    /// Panel pixels per screen pixel, 0 for as large as fits. Set before Initialize.
    void SetScale(int32_t scale) { m_Scale = scale > 0 ? scale : 0; }
    /// Whether frames are turned 180 degrees. Set before Initialize.
    void SetRotate180(bool rotate) { m_Rotate180 = rotate; }
    /// The game's screen format. Set before Initialize.
    void SetColorFormat(Deki::ColorFormat format) { m_Format = format; }

    bool Initialize(int32_t width, int32_t height) override;
    void Shutdown() override;
    void Present(const uint8_t* framebuffer, int width, int height, Deki::ColorFormat format) override;
    /// The free one of the three frame buffers, the screen's size and format:
    /// the game draws into it, and Present hands it to the present thread
    /// without a copy. The same buffer until the next Present.
    uint8_t* GetRenderBuffer(int32_t* width, int32_t* height) override;
    void GetDisplaySize(int32_t* width, int32_t* height) const override;
    Deki::ColorFormat GetColorFormat() const override { return m_Format; }
    bool IsInitialized() const override { return m_Initialized; }
    void RequestFullRefresh() override {}
    bool ProcessEvents() override;
    /// Measured at Initialize from flips back to back.
    uint32_t GetRefreshPeriodUs() const override { return m_RefreshUs; }
    /// The present thread holds each frame this many refreshes.
    void SetRefreshesPerFrame(uint32_t refreshes) override
    {
        m_RefreshesPerFrame.store(refreshes > 0 ? refreshes : 1, std::memory_order_relaxed);
    }
    /// Counted by the present thread as each frame goes up: at its flip, or
    /// as it is written where there is one page.
    bool GetFramesShown(uint64_t* frames, uint64_t* lastShownUs) const override;

    // Overlays are not used: nothing in the engine creates one.
    void* CreateUIOverlay(int32_t, int32_t) override { return nullptr; }
    bool UpdateUIOverlay(void*, int32_t, int32_t, int32_t, int32_t, const uint32_t*) override { return false; }
    bool UpdateUIOverlayRGB565A8(void*, int32_t, int32_t, int32_t, int32_t, const uint8_t*) override { return false; }
    void DestroyUIOverlay(void*) override {}
    void SetActiveUIOverlay(void*) override {}
    void ClearActiveUIOverlay() override {}

private:
    // Times flips of the blank pages to find the panel's refresh period.
    void MeasureRefresh();
    void PresentLoop();
    // Widens, scales and turns `frame` into the back page, then flips to it.
    void Draw(const Frame& frame);
    // After a flip: counts how long the frame before stayed on screen, and
    // says when frames stay up unevenly (a visible hitch).
    void CountShown();
    void ClearAllPages();
};

}  // namespace DekiMiyoo
