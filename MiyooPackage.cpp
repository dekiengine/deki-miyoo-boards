// Package entry point for the deki-miyoo DLL. Exports the standard Deki plugin
// interface, so the editor can load the DLL and find its components.
//
// Display and input are set up by their SetupComponents; the Miyoo time
// provider is registered in MiyooDisplay.cpp. The program entry (main) and the
// memory and filesystem layers live in the deki-desktop-integration package.

#include "MiyooPackage.h"
#include <deki/interop/Plugin.h>
#include <deki/Time.h>
#include <deki/providers/ITimeProvider.h>
#include <deki/reflection/ComponentRegistry.h>
#include <deki/reflection/ComponentFactory.h>

extern void DekiMiyooRegisterComponents();
extern int DekiMiyooGetAutoComponentCount();
extern const Deki::ComponentMeta* DekiMiyooGetAutoComponentMeta(int index);

namespace DekiMiyoo
{

#ifdef DEKI_EDITOR

static bool s_MiyooRegistered = false;

// The exports below are C symbols at global scope; the package's own
// registration helpers and statics live in its namespace.
using namespace DekiMiyoo;

extern "C"
{
    // Registers the package's components once. Returns how many there are.
    DEKI_MIYOO_API int DekiMiyooEnsureRegistered(void)
    {
        if (s_MiyooRegistered)
        {
            return ::DekiMiyooGetAutoComponentCount();
        }
        s_MiyooRegistered = true;

        // Generated: registers every Miyoo component with ComponentRegistry and ComponentFactory.
        ::DekiMiyooRegisterComponents();

        return ::DekiMiyooGetAutoComponentCount();
    }

    // =============================================================================
    // Plugin metadata
    // =============================================================================

    DEKI_PLUGIN_API const char* DekiPluginGetName(void)
    {
        return "Deki Miyoo Package";
    }

    DEKI_PLUGIN_API const char* DekiPluginGetVersion(void)
    {
#ifdef DEKI_PACKAGE_VERSION
        return DEKI_PACKAGE_VERSION;
#else
        return "0.0.0-dev";
#endif
    }

    DEKI_PLUGIN_API int DekiPluginInit(void)
    {
        return 0;
    }

    DEKI_PLUGIN_API void DekiPluginShutdown(void)
    {
        s_MiyooRegistered = false;
        Deki::Time::SetTimeProvider(nullptr);
    }

    DEKI_PLUGIN_API int DekiPluginGetComponentCount(void)
    {
        return ::DekiMiyooGetAutoComponentCount();
    }

    DEKI_PLUGIN_API const Deki::ComponentMeta* DekiPluginGetComponentMeta(int index)
    {
        return ::DekiMiyooGetAutoComponentMeta(index);
    }

    DEKI_PLUGIN_API void DekiPluginRegisterComponents(void)
    {
        DekiMiyooEnsureRegistered();
    }

    // =============================================================================
    // Package-specific API, with names that do not clash when DLLs link each other
    // =============================================================================

    DEKI_MIYOO_API const char* DekiMiyooGetName(void)
    {
        return "Miyoo";
    }

}  // extern "C"

#else  // !DEKI_EDITOR - Runtime registration

// Components are registered by the generated ::DekiMiyooRegisterComponents(),
// called from DekiRegisterProjectPackages(). Display and input run as boot
// SetupComponents, and a static initializer in MiyooDisplay.cpp registers the
// Miyoo time provider.

#endif  // DEKI_EDITOR
}  // namespace DekiMiyoo
