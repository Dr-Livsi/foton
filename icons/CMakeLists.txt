# the application icon for icon themes
ecm_install_icons(
    ICONS
        16-apps-foton.png
        22-apps-foton.png
        32-apps-foton.png
        48-apps-foton.png
        64-apps-foton.png
        128-apps-foton.png
        256-apps-foton.png
    DESTINATION ${KDE_INSTALL_ICONDIR}
    THEME hicolor
)

# the application icon used by the windows and the system tray icon
qt_add_resources(foton "foton_icons"
    PREFIX "/icons"
    FILES
        foton-16.png
        foton-20.png
        foton-24.png
        foton-32.png
        foton-40.png
        foton-48.png
        foton-64.png
        foton-128.png
        foton-256.png
)
