#pragma once

#include <deki/SetupComponent.h>
#include <deki/reflection/Property.h>
#include "MiyooPackage.h"

namespace DekiMiyoo
{

/// Starts the handheld's buttons. Add it to the boot scene; it runs as part of
/// the boot sequence. See MiyooInput.h for which key each button reports.
DEKI_CATEGORY("Miyoo")
DEKI_DESCRIPTION("Feeds a Miyoo handheld's buttons to the engine as keys.")
class DEKI_MIYOO_API MiyooInputSetup : public Deki::SetupComponent
{
public:
    void Setup(SetupCallback onComplete) override;
    const char* GetSetupName() const override { return "Miyoo Input"; }
};

}  // namespace DekiMiyoo
