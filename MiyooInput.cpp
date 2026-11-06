#include "MiyooInput.h"
#include "Keys.h"  // from deki-input

#include <deki/Time.h>

#if defined(__linux__) && !defined(DEKI_EDITOR)
#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>
#include <deki/LogSystem.h>
#endif

namespace DekiMiyoo
{

namespace
{
// The Miyoo Mini's buttons, as its kernel reports them (linux/input-event-codes.h).
enum MiyooCode : uint16_t
{
    kCodeEsc = 1,         // Menu
    kCodeBackspace = 14,  // R2
    kCodeTab = 15,        // L2
    kCodeE = 18,          // L1
    kCodeT = 20,          // R1
    kCodeEnter = 28,      // Start
    kCodeLeftCtrl = 29,   // B
    kCodeLeftShift = 42,  // X
    kCodeLeftAlt = 56,    // Y
    kCodeSpace = 57,      // A
    kCodeRightCtrl = 97,  // Select
    kCodeUp = 103,
    kCodeLeft = 105,
    kCodeRight = 106,
    kCodeDown = 108,
};
}  // namespace

uint32_t MiyooInput::KeyForCode(uint16_t code)
{
    namespace Keys = DekiInput::Keys;
    switch (code)
    {
        case kCodeUp:
            return Keys::Up;
        case kCodeDown:
            return Keys::Down;
        case kCodeLeft:
            return Keys::Left;
        case kCodeRight:
            return Keys::Right;
        case kCodeSpace:  // A
        case kCodeEnter:  // Start
            return Keys::Enter;
        case kCodeLeftCtrl:  // B
            return Keys::Esc;
        case kCodeLeftShift:  // X
            return Keys::Space;
        case kCodeLeftAlt:  // Y
            return Keys::Tab;
        case kCodeRightCtrl:  // Select
            return Keys::Backspace;
        case kCodeE:  // L1
            return Keys::Q;
        case kCodeT:  // R1
            return Keys::E;
        case kCodeTab:  // L2
            return Keys::Z;
        case kCodeBackspace:  // R2
            return Keys::C;
        default:
            return 0;
    }
}

bool MiyooInput::IsQuitCode(uint16_t code)
{
    return code == kCodeEsc;
}

MiyooInput::~MiyooInput()
{
    Shutdown();
}

void MiyooInput::RegisterEventCallback(const DekiInput::InputEventCallback& callback)
{
    m_EventCallbacks.push_back(callback);
}

void MiyooInput::Notify(const DekiInput::InputEvent& event)
{
    for (const auto& callback : m_EventCallbacks)
    {
        callback(event);
    }
}

bool MiyooInput::GetPointerPosition(int32_t* x, int32_t* y) const
{
    (void)x;
    (void)y;
    return false;  // no touch screen, no mouse
}

bool MiyooInput::IsKeyPressed(uint32_t key) const
{
    auto it = m_KeyDownCount.find(key);
    return it != m_KeyDownCount.end() && it->second > 0;
}

#if defined(__linux__) && !defined(DEKI_EDITOR)

bool MiyooInput::Initialize()
{
    if (m_Initialized)
    {
        return true;
    }
    m_Fd = open(m_DevicePath, O_RDONLY | O_NONBLOCK);
    if (m_Fd < 0)
    {
        DEKI_LOG_ERROR("MiyooInput: cannot open %s", m_DevicePath);
        return false;
    }
    m_Initialized = true;
    return true;
}

void MiyooInput::Shutdown()
{
    if (m_Fd >= 0)
    {
        close(m_Fd);
        m_Fd = -1;
    }
    m_KeyDownCount.clear();
    m_Initialized = false;
}

void MiyooInput::Update()
{
    if (!m_Initialized)
    {
        return;
    }
    input_event ev{};
    while (read(m_Fd, &ev, sizeof(ev)) == static_cast<ssize_t>(sizeof(ev)))
    {
        // value 2 is the kernel's auto-repeat; the engine repeats on its own.
        if (ev.type != EV_KEY || ev.value == 2)
        {
            continue;
        }
        const bool down = ev.value != 0;

        DekiInput::InputEvent event{};
        event.timestamp = Deki::Time::GetTime();

        if (IsQuitCode(ev.code))
        {
            if (down)
            {
                event.type = DekiInput::InputEventType::AppQuit;
                Notify(event);
            }
            continue;
        }

        const uint32_t key = KeyForCode(ev.code);
        if (key == 0)
        {
            continue;
        }
        int& count = m_KeyDownCount[key];
        const bool wasDown = count > 0;
        count = down ? count + 1 : (count > 0 ? count - 1 : 0);
        const bool isDown = count > 0;
        if (wasDown == isDown)
        {
            continue;  // another button already holds this key
        }

        event.type = isDown ? DekiInput::InputEventType::KeyDown : DekiInput::InputEventType::KeyUp;
        event.key = key;
        event.pressed = isDown;
        Notify(event);
    }
}

#else  // not on the device

bool MiyooInput::Initialize()
{
    return false;
}
void MiyooInput::Shutdown() {}
void MiyooInput::Update() {}

#endif

}  // namespace DekiMiyoo
