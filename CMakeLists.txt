cmake_minimum_required (VERSION 3.16 FATAL_ERROR)

# Foton is a port of KDE Spectacle to Windows.
set(FOTON_VERSION "1.0.0")
# The version of Spectacle that Foton is based on.
set(FOTON_SPECTACLE_VERSION "6.8.80")

project(Foton VERSION ${FOTON_VERSION} LANGUAGES CXX)

# minimum requirements
set(QT_MIN_VERSION "6.8.0" CACHE STRING "Minimum required Qt version")
set(KF6_MIN_VERSION "6.13.0" CACHE STRING "Minimum required KDE Frameworks version")

find_package(ECM ${KF6_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

# set up standard kde build settings

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

# Set C++ standard separately from KDECompilerSettings since that is set to an older C++ standard.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# used cmake macros

include(ECMInstallIcons)
include(ECMSetupVersion)
include(FeatureSummary)
include(ECMQtDeclareLoggingCategory)
include(ECMAddTests)
include(KDEClangFormat)
include(ECMDeprecationSettings)
include(ECMFindQmlModule)

if(NOT WIN32)
    message(WARNING "Foton only supports taking screenshots on Windows. "
                    "Other platforms can only be used for development.")
endif()

# find dependencies

find_package(
    Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED
        Core
        Concurrent
        Network
        Qml
        Quick
        QuickControls2
        QuickTemplates2
        Widgets
        Test
)

qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

find_package(
    KF6 ${KF6_MIN_VERSION} REQUIRED
        CoreAddons
        ColorScheme
        WidgetsAddons
        Notifications
        Config
        ConfigWidgets
        I18n
        IconThemes
        XmlGui
        GuiAddons
        KirigamiPlatform
        Crash
)

# Finds QR codes and other barcodes in screenshots.
find_package(ZXing 2.0 REQUIRED)

ecm_find_qmlmodule(org.kde.kquickimageeditor)
find_package(KQuickImageEditor 0.7 REQUIRED COMPONENTS)
set_package_properties(KQuickImageEditor PROPERTIES
    TYPE REQUIRED
    DESCRIPTION "Simple image editor for QtQuick applications"
    URL "https://invent.kde.org/libraries/kquickimageeditor/"
    PURPOSE "Add image editing capability to the screenshots"
)

# setup handling of deprecated Qt & KF API

ecm_set_disabled_deprecation_versions(QT 6.8.0  KF 6.12.0)

# hand off to subdirectories

add_subdirectory(src)
add_subdirectory(desktop)
add_subdirectory(icons)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

ki18n_install(po)

ecm_qt_install_logging_categories(EXPORT FOTON FILE foton.categories DESTINATION ${KDE_INSTALL_LOGGINGCATEGORIESDIR})

# summaries

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)

file(GLOB_RECURSE ALL_CLANG_FORMAT_SOURCE_FILES src/*.cpp src/*.h tests/*.cpp)
kde_clang_format(${ALL_CLANG_FORMAT_SOURCE_FILES})
