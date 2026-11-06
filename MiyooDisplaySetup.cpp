#include "MiyooDisplaySetup.h"
#include <deki/LogSystem.h>

#if !defined(DEKI_EDITOR) && defined(DEKI_PACKAGE_MIYOO)
#include "MiyooDisplay.h"
#include <deki/Engine.h>
#include <memory>
#endif

namespace DekiMiyoo
{

#if !defined(DEKI_EDITOR) && defined(DEKI_PACKAGE_MIYOO)

// The package owns the display and keeps it for the whole program; the engine
// only holds a pointer to it.
static std::unique_ptr<MiyooDisplay> s_MiyooDisplay;

#if !defined(DEKI_SCREEN_WIDTH) || !defined(DEKI_SCREEN_HEIGHT) || !defined(DEKI_SCREEN_COLOR_FORMAT)
#error "A Miyoo build must define DEKI_SCREEN_WIDTH, DEKI_SCREEN_HEIGHT and DEKI_SCREEN_COLOR_FORMAT (the platform's screenWidth, screenHeight and colorFormat)."
#endif

void MiyooDisplaySetup::Setup(SetupCallback onComplete)
{
    DEKI_LOG_INFO("MiyooDisplaySetup: Opening the framebuffer (%dx%d screen, scale %d)", (int)DEKI_SCREEN_WIDTH,
                  (int)DEKI_SCREEN_HEIGHT, (int)scale);

    s_MiyooDisplay = std::make_unique<MiyooDisplay>();
    s_MiyooDisplay->SetScale(scale);
    s_MiyooDisplay->SetRotate180(rotate180);
    s_MiyooDisplay->SetColorFormat(DEKI_SCREEN_COLOR_FORMAT);
    if (s_MiyooDisplay->Initialize(DEKI_SCREEN_WIDTH, DEKI_SCREEN_HEIGHT))
    {
        Deki::Engine::GetInstance().SetDisplay(s_MiyooDisplay.get(), "Miyoo");
        onComplete(true);
    }
    else
    {
        DEKI_LOG_ERROR("MiyooDisplaySetup: Failed to open the framebuffer");
        s_MiyooDisplay.reset();
        onComplete(false);
    }
}

#else

void MiyooDisplaySetup::Setup(SetupCallback onComplete)
{
    // The editor and other targets have no Miyoo screen to open.
    onComplete(true);
}

#endif

}  // namespace DekiMiyoo
