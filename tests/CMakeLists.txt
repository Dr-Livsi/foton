include_directories(${PROJECT_SOURCE_DIR}/src)

SET(FILENAME_TEST_SRCS
    FilenameTest.cpp
    ../src/BarcodeScanner.cpp
    ../src/ExportManager.cpp
    ../src/WindowsIntegration.cpp
    ../src/Platforms/ImagePlatform.cpp
)

kde_source_files_enable_exceptions(../src/BarcodeScanner.cpp)

ecm_qt_declare_logging_category(FILENAME_TEST_SRCS
    HEADER foton_debug.h
    IDENTIFIER FOTON_LOG
    CATEGORY_NAME foton
    DESCRIPTION "foton (general)"
)

kconfig_add_kcfg_files(FILENAME_TEST_SRCS GENERATE_MOC ${PROJECT_SOURCE_DIR}/src/Gui/SettingsDialog/settings.kcfgc)

ecm_add_test(
    ${FILENAME_TEST_SRCS}
    TEST_NAME "filename_test"
    LINK_LIBRARIES Qt::Test Qt::Concurrent Qt::Widgets Qt::Qml
        KF6::I18n KF6::ConfigCore KF6::ConfigGui KF6::GuiAddons ZXing::ZXing
)
if(WIN32)
    target_link_libraries(filename_test shell32 ole32 uuid dwmapi)
    target_compile_definitions(filename_test PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 NOMINMAX)
endif()
