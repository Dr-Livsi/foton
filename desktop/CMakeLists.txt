# Notifications (KNotification) are configured with this file.
install(
    FILES foton.notifyrc
    DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR}
)

# The colors of Windows 10, used instead of the colors of KDE's Breeze theme.
install(
    FILES Windows10.colors
    DESTINATION ${KDE_INSTALL_DATADIR}/foton
)
