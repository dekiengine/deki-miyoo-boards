#pragma once

#include <cstdint>
#include <deki/SetupComponent.h>
#include <deki/reflection/Property.h>
#include "MiyooPackage.h"

namespace DekiMiyoo
{

/// Opens the handheld's screen (the Linux framebuffer). The game renders at
/// the platform's screen size; the display scales it up by whole pixels to
/// fill the panel. Runs as part of the boot sequence.
DEKI_CATEGORY("Miyoo")
DEKI_DESCRIPTION("Shows the game on a Miyoo handheld's screen.")
class DEKI_MIYOO_API MiyooDisplaySetup : public Deki::SetupComponent
{
public:
    DEKI_EXPORT
    DEKI_TOOLTIP("Panel pixels per screen pixel. 0 scales as large as fits.")
    DEKI_RANGE(0, 8)
    int32_t scale = 0;

    DEKI_EXPORT
    DEKI_TOOLTIP("Turn frames 180 degrees. The Miyoo Mini's panel is mounted upside down.")
    bool rotate180 = true;

    void Setup(SetupCallback onComplete) override;
    const char* GetSetupName() const override { return "Miyoo Display"; }
};

}  // namespace DekiMiyoo
