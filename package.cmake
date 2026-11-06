# Package descriptor for deki-engine auto-discovery
set(PACKAGE_DISPLAY_NAME "Miyoo")
set(PACKAGE_PREFIX "DekiMiyoo")
set(PACKAGE_UPPER "MIYOO")
set(PACKAGE_TARGET "deki-miyoo")
set(PACKAGE_FILE_PREFIX "Miyoo")
set(PACKAGE_SOURCES
    MiyooDisplaySetup.cpp
    MiyooInputSetup.cpp
    MiyooDisplay.cpp
    MiyooRows.cpp
    MiyooInput.cpp
)
set(PACKAGE_ENTRY MiyooPackage.cpp)
set(PACKAGE_LINK_DEPS deki-input)
