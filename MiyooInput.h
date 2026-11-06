#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "IDekiInput.h"  // from deki-input

namespace DekiMiyoo
{

/// The handheld's buttons, read from their evdev device (/dev/input/event0 on
/// the Miyoo Mini). Each button reports a Deki key, so games written for a
/// keyboard or a trackball work unchanged:
///
///   D-pad    Up / Down / Left / Right
///   A        Enter          B       Esc
///   X        Space          Y       Tab
///   Start    Enter          Select  Backspace
///   L1       q              R1      e
///   L2       z              R2      c
///   Menu     quits the game, back to the firmware's menu
class MiyooInput : public DekiInput::IDekiInput
{
private:
    const char* m_DevicePath;
    int m_Fd = -1;
    bool m_Initialized = false;
    std::vector<DekiInput::InputEventCallback> m_EventCallbacks;

    // How many buttons now hold each key down: Start and A both press Enter.
    std::unordered_map<uint32_t, int> m_KeyDownCount;

    void Notify(const DekiInput::InputEvent& event);

public:
    explicit MiyooInput(const char* devicePath = "/dev/input/event0") : m_DevicePath(devicePath) {}
    ~MiyooInput() override;

    bool Initialize() override;
    void Shutdown() override;
    void Update() override;
    void RegisterEventCallback(const DekiInput::InputEventCallback& callback) override;
    bool IsInitialized() const override { return m_Initialized; }
    bool GetPointerPosition(int32_t* x, int32_t* y) const override;
    bool IsKeyPressed(uint32_t key) const override;

    /// The Deki key a button's evdev code reports; 0 for none.
    static uint32_t KeyForCode(uint16_t code);
    /// True for the button that quits the game (Menu).
    static bool IsQuitCode(uint16_t code);
};

}  // namespace DekiMiyoo
