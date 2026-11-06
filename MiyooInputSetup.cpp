#include "MiyooInputSetup.h"
#include <deki/LogSystem.h>

#if !defined(DEKI_EDITOR) && defined(DEKI_PACKAGE_MIYOO)
#include "MiyooInput.h"
#include "DekiInput.h"      // from deki-input
#include "DekiInputInit.h"  // DekiInputInitSystem (from deki-input)
#include <memory>
#endif

namespace DekiMiyoo
{

#if !defined(DEKI_EDITOR) && defined(DEKI_PACKAGE_MIYOO)

void MiyooInputSetup::Setup(SetupCallback onComplete)
{
    DEKI_LOG_INFO("MiyooInputSetup: Opening the buttons");

    auto input = std::make_unique<MiyooInput>();
    if (input->Initialize())
    {
        // deki-input's namespace and class share the name DekiInput; the alias
        // names the class (as SDL3InputSetup does).
        using DekiInputApi = DekiInput::DekiInput;
        DekiInputApi::SetInput(std::move(input), "Miyoo");

        // The static build links the engine's empty DekiInitPackageSystems(),
        // so deki-input's dispatch system starts here. Safe to repeat.
        DekiInputInitSystem();

        onComplete(true);
    }
    else
    {
        DEKI_LOG_ERROR("MiyooInputSetup: Failed to open the buttons");
        onComplete(false);
    }
}

#else

void MiyooInputSetup::Setup(SetupCallback onComplete)
{
    // The editor and other targets take input elsewhere.
    onComplete(true);
}

#endif

}  // namespace DekiMiyoo
