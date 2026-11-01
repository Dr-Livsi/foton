# common - configure file and version definitions

set(FOTON_QML_URI "foton.private")
set(FOTON_QML_PATH "qrc:/qt/qml/foton/private")
configure_file(Config.h.in ${CMAKE_CURRENT_BINARY_DIR}/Config.h)

# target

kconfig_add_kcfg_files(FOTON_SRCS GENERATE_MOC Gui/SettingsDialog/settings.kcfgc)

ecm_qt_declare_logging_category(FOTON_SRCS
    HEADER foton_debug.h
    IDENTIFIER FOTON_LOG
    CATEGORY_NAME foton
    DESCRIPTION "foton (general)"
    EXPORT FOTON
)

add_executable(foton)
qt_add_qml_module(foton URI ${FOTON_QML_URI} DEPENDENCIES QtCore QtQuick)

target_sources(foton PRIVATE
    ${FOTON_SRCS}
    AppIcon.h
    BarcodeScanner.cpp
    CaptureModeModel.cpp
    CommandLineOptions.cpp
    ExportManager.cpp
    FotonCore.cpp
    Geometry.cpp
    GlobalShortcuts.cpp
    Language.cpp
    ShortcutActions.cpp
    SingleInstance.cpp
    TrayIcon.cpp
    WindowsIntegration.cpp
    Main.cpp
    Gui/CaptureWindow.cpp
    Gui/FotonMenu.cpp
    Gui/FotonWindow.cpp
    Gui/HelpMenu.cpp
    Gui/InlineMessageModel.cpp
    Gui/OptionsMenu.cpp
    Gui/ScreenshotModeMenu.cpp
    Gui/Selection.cpp
    Gui/SelectionEditor.cpp
    Gui/SettingsDialog/FilenameTemplateEditor.cpp
    Gui/SettingsDialog/FolderRequester.cpp
    Gui/SettingsDialog/GeneralOptionsPage.cpp
    Gui/SettingsDialog/ImageSaveOptionsPage.cpp
    Gui/SettingsDialog/SettingsDialog.cpp
    Gui/SettingsDialog/ShortcutsOptionsPage.cpp
    Gui/TextContextMenu.cpp
    Gui/TextRecognitionWindow.cpp
    Gui/ViewerWindow.cpp
    Platforms/ImagePlatform.cpp
    Platforms/PlatformLoader.cpp
    Platforms/PlatformNull.cpp
)

if(WIN32)
    target_sources(foton PRIVATE
        Platforms/ImagePlatformWindows.cpp
        Platforms/ImagePlatformWindows.h
        TextRecognitionWindows.cpp
    )
    # C++/WinRT reports errors with exceptions.
    kde_source_files_enable_exceptions(TextRecognitionWindows.cpp)
    configure_file(foton.rc.in ${CMAKE_CURRENT_BINARY_DIR}/foton.rc @ONLY)
    target_sources(foton PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/foton.rc)
    # A normal GUI application without a console window.
    set_target_properties(foton PROPERTIES WIN32_EXECUTABLE TRUE)
    target_compile_definitions(foton PRIVATE
        # Windows 10 and later
        _WIN32_WINNT=0x0A00
        WINVER=0x0A00
        NOMINMAX
    )
    target_link_libraries(foton PRIVATE
        user32
        gdi32
        dwmapi
        shell32
        ole32
        uuid
        # C++/WinRT for the text recognition of Windows
        windowsapp
    )
else()
    target_sources(foton PRIVATE TextRecognitionNull.cpp)
endif()

# ZXing reports errors with exceptions.
kde_source_files_enable_exceptions(BarcodeScanner.cpp)

ki18n_wrap_ui(foton
    Gui/SettingsDialog/GeneralOptions.ui
    Gui/SettingsDialog/ImageSaveOptions.ui
)

target_link_libraries(foton PRIVATE
    Qt::Concurrent
    Qt::Network
    Qt::Qml
    Qt::Quick
    Qt::QuickControls2
    Qt::QuickTemplates2
    Qt::Widgets
    KF6::CoreAddons
    KF6::WidgetsAddons
    KF6::Notifications
    KF6::ConfigCore
    KF6::ConfigGui
    KF6::ConfigWidgets
    KF6::ColorScheme
    KF6::I18n
    KF6::I18nQml
    KF6::IconThemes
    KF6::XmlGui
    KF6::GuiAddons
    KF6::KirigamiPlatform
    ZXing::ZXing
    KF6::Crash
    KQuickImageEditor
)

# qt_add_qml_module doesn't know how to deal with headers in subdirectories so
# make sure to add those so the headers can be found.
target_include_directories(foton PRIVATE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Gui>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Platforms>"
)

set_source_files_properties(Gui/QmlUtils.qml PROPERTIES
  QT_QML_SINGLETON_TYPE TRUE
)

qt_target_qml_sources(foton
    QML_FILES
    Gui/AcceptAction.qml
    Gui/AnimatedLoader.qml
    Gui/AnnotationEditor.qml
    Gui/AnnotationOptionsToolBarContents.qml
    Gui/AnnotationsToolBarContents.qml
    Gui/ButtonGrid.qml
    Gui/CancelAction.qml
    Gui/CaptureModeButtonsColumn.qml
    Gui/CaptureOptions.qml
    Gui/CaptureOverlay.qml
    Gui/CaptureSettingsColumn.qml
    Gui/CopyImageAction.qml
    Gui/CopyLocationAction.qml
    Gui/DashedOutline.qml
    Gui/DialogPage.qml
    Gui/EditAction.qml
    Gui/EmptyPage.qml
    Gui/FloatingBackground.qml
    Gui/FloatingToolBar.qml
    Gui/Handle.qml
    Gui/HelpMenuButton.qml
    Gui/InlineMessageList.qml
    Gui/Magnifier.qml
    Gui/NewScreenshotToolButton.qml
    Gui/OptionsMenuButton.qml
    Gui/Outline.qml
    Gui/QmlUtils.qml
    Gui/RecognizeTextAction.qml
    Gui/SaveAction.qml
    Gui/SaveAsAction.qml
    Gui/ScreenshotModeMenuButton.qml
    Gui/ScreenshotView.qml
    Gui/ShortcutsTextBox.qml
    Gui/SizeLabel.qml
    Gui/TextContextMenuConnection.qml
    Gui/TtToolButton.qml
    Gui/UndoRedoGroup.qml
    Gui/ViewerPage.qml
    Gui/WinCheckBox.qml
    Gui/WinSpinBox.qml
)

install(TARGETS foton ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES Gui/SettingsDialog/foton.kcfg DESTINATION ${KDE_INSTALL_KCFGDIR})
