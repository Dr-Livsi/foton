# SPDX-License-Identifier: LGPL-2.0-or-later
#
# KDE Craft blueprint for Foton, a screenshot utility for Windows based on KDE Spectacle.
#
# Usage (in a Craft environment, see README.md):
#   craft --add-blueprint-repository <path to this repository>/craft
#   craft --src-dir <path to this repository> foton
#   craft --src-dir <path to this repository> --package foton
#
# Instead of --src-dir, the URL of a git repository can be set with the
# FOTON_GIT_URL environment variable.

import importlib.util
import os
from pathlib import Path

import info
import utils
from Blueprints.CraftPackageObject import CraftPackageObject
from CraftCore import CraftCore


class subinfo(info.infoclass):
    def setTargets(self):
        self.svnTargets["master"] = os.environ.get("FOTON_GIT_URL", "")
        self.defaultTarget = "master"
        self.displayName = "Foton"
        self.description = "Screenshot utility for Windows, based on KDE Spectacle"
        self.webpage = os.environ.get("FOTON_GIT_URL", "")

    def registerOptions(self):
        self.parent.package.categoryInfo.platforms = CraftCore.compiler.Platforms.Windows

    def setDependencies(self):
        self.buildDependencies["kde/frameworks/extra-cmake-modules"] = None
        self.runtimeDependencies["virtual/base"] = None
        self.runtimeDependencies["libs/qt/qtbase"] = None
        self.runtimeDependencies["libs/qt/qtdeclarative"] = None
        self.runtimeDependencies["libs/qt/qtsvg"] = None
        self.runtimeDependencies["libs/qt/qtimageformats"] = None
        self.runtimeDependencies["kde/frameworks/tier1/kcoreaddons"] = None
        self.runtimeDependencies["kde/frameworks/tier2/kcolorscheme"] = None
        self.runtimeDependencies["kde/frameworks/tier1/kwidgetsaddons"] = None
        self.runtimeDependencies["kde/frameworks/tier1/kconfig"] = None
        self.runtimeDependencies["kde/frameworks/tier1/ki18n"] = None
        self.runtimeDependencies["kde/frameworks/tier1/kguiaddons"] = None
        self.runtimeDependencies["kde/frameworks/tier1/kirigami"] = None
        # finds QR codes and other barcodes
        self.runtimeDependencies["libs/zxing-cpp"] = None
        self.runtimeDependencies["kde/frameworks/tier1/breeze-icons"] = None
        self.runtimeDependencies["kde/frameworks/tier2/kcrash"] = None
        self.runtimeDependencies["kde/frameworks/tier3/knotifications"] = None
        self.runtimeDependencies["kde/frameworks/tier3/kconfigwidgets"] = None
        self.runtimeDependencies["kde/frameworks/tier3/kiconthemes"] = None
        self.runtimeDependencies["kde/frameworks/tier3/kxmlgui"] = None
        self.runtimeDependencies["kde/frameworks/tier3/qqc2-desktop-style"] = None
        self.runtimeDependencies["kde/plasma/breeze"] = None
        self.runtimeDependencies["kde/libs/kquickimageeditor"] = None


class Package(CraftPackageObject.get("kde").pattern):
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.subinfo.options.configure.args += ["-DBUILD_TESTING=OFF"]

    def createPackage(self):
        self.blacklist_file.append(self.blueprintDir() / "blacklist.txt")
        icon = self.blueprintDir() / "foton.ico"
        self.defines["executable"] = r"bin\foton.exe"
        self.defines["icon"] = icon
        # Only allow installing into a folder of its own, see installer.nsh.
        self.defines["nsis_include"] = f'!include "{self.blueprintDir() / "installer.nsh"}"'
        # The appId has to match the application name for Windows notifications (SnoreToast).
        self.defines["shortcuts"] = [{"name": "Foton", "target": "bin/foton.exe", "appId": "foton", "icon": icon}]
        # update-mime-database is only used by Craft, when building. snoretoast shows the notifications.
        self.addExecutableFilter(r"(bin|libexec)/(?!(foton|snoretoast)).*")
        self.ignoredPackages.append("binary/mysql")
        self.ignoredPackages.append("libs/dbus")

        # Compress the installer and the portable archive as much as 7-Zip can, Craft uses its default level.
        compress = utils.compress

        def compressMore(archive, source):
            archive = Path(archive)
            sevenZip = CraftCore.cache.findApplication("7za")
            if archive.suffix not in (".7z", ".zip") or ".tar" in archive.suffixes or not os.path.isdir(source) or not sevenZip:
                return compress(archive, source)
            return utils.system([sevenZip, "a", "-r", "-mx=9", "-mmt=on", str(archive), os.path.join(source, "*")])

        utils.compress = compressMore
        try:
            return super().createPackage()
        finally:
            utils.compress = compress

    def preArchive(self):
        # Leave out the files that Foton never uses, see tools/prune_package.py.
        # It isn't next to this file, Craft allows only one Python file in the folder of a blueprint.
        spec = importlib.util.spec_from_file_location("foton_prune", Path(self.sourceDir()) / "tools" / "prune_package.py")
        prune = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(prune)
        tools = Path(CraftCore.standardDirs.craftRoot()) / "bin"
        if not prune.prune(Path(self.archiveDir()), Path(self.sourceDir()), CraftCore.log.info, tools):
            return False
        return super().preArchive()
