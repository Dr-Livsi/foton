# SPDX-License-Identifier: LGPL-2.0-or-later
#
# Removes the files from the Windows package of Foton that Foton never uses.
#
# Craft packages everything that the dependencies install, and puts the Qt plugins
# (e.g. bin/platforms) and the QML modules (e.g. bin/QtQuick) directly into bin. This removes:
# - QML modules that neither Foton nor the QML modules it uses import,
# - plugins that need a Qt or KDE library that Foton doesn't load, which can't be loaded then,
#   and plugins for features that Foton doesn't have (e.g. audio and video),
# - DLLs in bin that no program, plugin or QML module of the package imports,
# - translations into languages that Foton's interface isn't available in, and data nothing reads,
# - the data of ICU that Qt never asks for. The trimmed data is only used if ICU gives the same
#   answers with it as with the original data.
#
# Used by the Craft blueprint in craft/foton/foton.py: prune(archiveDir, sourceDir, log, tools)
# For testing with an unpacked package: python prune_package.py <package dir> <source dir> [<folder with the ICU tools>]

import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

# Plugins below bin that Foton doesn't need, as glob patterns.
UNUSED_PLUGINS = [
    # Audio and video, which need FFmpeg. Qt Multimedia itself is still needed by the
    # QR code scanner of Prison, which only scans images and doesn't need a media backend.
    "multimedia",
    # Foton doesn't connect to the internet.
    "tls",
    "networkinformation",
    "generic",
    # The QML debugger and language server.
    "qmltooling",
    "qmlls",
    # Only the Windows platform plugin is used.
    "platforms/qdirect2d.dll",
    "platforms/qminimal.dll",
    "platforms/qoffscreen.dll",
]

# Data that nothing in the package reads, relative to the package. On Windows, Qt and KDE read
# their data from bin/data. share has e.g. translations of GLib and gettext, examples of the
# Intel Video Processing Library and Wayland protocols.
UNUSED_DATA = [
    "share",
    # Modules of OpenSSL for old algorithms.
    "lib/ossl-modules",
    # Scripts for the Serbian translations, which choose the grammatical forms of names.
    # Without them, the translations are used as they are.
    "bin/data/locale/sr*/LC_SCRIPTS",
]

# Data of libraries, with a text that identifies the library. The data is removed if the text is only
# found in binaries that were removed, then the library it belongs to isn't in the package anymore.
LIBRARY_DATA = {
    "bin/data/kf6/searchproviders": "searchproviders",
    "bin/data/iso-codes": "iso-codes",
}

# The QML modules that Foton imports, besides the ones in its own QML files.
QML_ROOTS = [
    # The style for Qt Quick Controls set in Main.cpp and the fallback for controls it doesn't have.
    "org.kde.desktop",
    "QtQuick.Controls.Basic",
    "QtQml.Models",
    "QtQml.WorkerScript",
]

# Styles of Qt Quick Controls that aren't used. Some modules have variants of their QML files for them,
# in folders like +Material (file selectors) or styles/Material (Kirigami), which are only loaded with that style.
UNUSED_STYLES = {"Material", "Universal", "Fusion", "Imagine", "FluentWinUI3", "Windows", "macOS", "iOS"}

# Translations of Qt that are used: Qt itself and Qt Quick.
QT_TRANSLATIONS = {"qt", "qtbase", "qtdeclarative"}

# Parts of the Universal C Runtime that are part of Windows 10 and later.
# Windows always uses its own copy, even if the application has one.
UCRT_PATTERN = re.compile(r"^(api-ms-win-.*|ucrtbase)\.dll$", re.IGNORECASE)

# The libraries of Qt and KDE. A plugin that imports one of them that Foton doesn't load can't be loaded.
FRAMEWORK_PATTERN = re.compile(r"^(qt6|kf6|kirigami|kquickimageeditor)", re.IGNORECASE)

IMPORT_PATTERN = re.compile(r"^\s*import\s+([A-Za-z_][\w.]*)", re.MULTILINE)

# Data of ICU that Qt never asks for. Qt only uses ICU for collation (QCollator), for converting
# the case of text with the rules of a language, for text codecs and for time zones and their names.
UNUSED_ICU_DATA = [
    # Rules and dictionaries for breaking text into words and lines.
    "brkitr/*",
    # Transliteration.
    "translit/*",
    # Spelling out numbers.
    "rbnf/*",
    # Names of units and currencies.
    "unit/*",
    "curr/*",
    # Names of characters.
    "unames.icu",
]

# Asks ICU what Qt asks it for, with the data in a folder, and prints the answers.
# The answers with the trimmed data have to be the same as with the original data.
ICU_CHECK = r"""
import ctypes, json, os, sys

directory, version = sys.argv[1], sys.argv[2]
os.add_dll_directory(directory)
common = ctypes.CDLL(os.path.join(directory, f"icuuc{version}.dll"))
i18n = ctypes.CDLL(os.path.join(directory, f"icuin{version}.dll"))
Status = ctypes.c_int
Text = ctypes.c_wchar_p


def function(library, name, result, *arguments):
    f = getattr(library, f"{name}_{version}")
    f.restype = result
    f.argtypes = arguments
    return f


u_init = function(common, "u_init", None, ctypes.POINTER(Status))
ucol_open = function(i18n, "ucol_open", ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(Status))
ucol_strcoll = function(i18n, "ucol_strcoll", ctypes.c_int, ctypes.c_void_p, Text, ctypes.c_int32, Text, ctypes.c_int32)
ucol_close = function(i18n, "ucol_close", None, ctypes.c_void_p)
ucnv_open = function(common, "ucnv_open", ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(Status))
ucnv_toUChars = function(common, "ucnv_toUChars", ctypes.c_int32, ctypes.c_void_p, Text, ctypes.c_int32, ctypes.c_char_p, ctypes.c_int32, ctypes.POINTER(Status))
ucnv_close = function(common, "ucnv_close", None, ctypes.c_void_p)
ucnv_countAvailable = function(common, "ucnv_countAvailable", ctypes.c_int32)
u_strToUpper = function(common, "u_strToUpper", ctypes.c_int32, Text, ctypes.c_int32, Text, ctypes.c_int32, ctypes.c_char_p, ctypes.POINTER(Status))
u_strToLower = function(common, "u_strToLower", ctypes.c_int32, Text, ctypes.c_int32, Text, ctypes.c_int32, ctypes.c_char_p, ctypes.POINTER(Status))
ucal_open = function(i18n, "ucal_open", ctypes.c_void_p, Text, ctypes.c_int32, ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(Status))
ucal_getTimeZoneDisplayName = function(i18n, "ucal_getTimeZoneDisplayName", ctypes.c_int32, ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p, Text, ctypes.c_int32, ctypes.POINTER(Status))
ucal_close = function(i18n, "ucal_close", None, ctypes.c_void_p)

answers = []
status = Status(0)
u_init(ctypes.byref(status))
answers.append(["init", status.value])

for locale, a, b in [("de", "ä", "b"), ("sv", "ä", "z"), ("ru", "ё", "ж"), ("hu", "cs", "d"), ("ja", "あ", "ア"),
                     ("tr", "ı", "i"), ("zh", "中", "国"), ("en", "a", "B"), ("cs", "ch", "h"), ("es", "ñ", "o")]:
    status = Status(0)
    collator = ucol_open(locale.encode(), ctypes.byref(status))
    order = ucol_strcoll(collator, a, len(a), b, len(b)) if collator else None
    answers.append(["collation", locale, a, b, status.value, order])
    if collator:
        ucol_close(collator)

answers.append(["converters", ucnv_countAvailable()])
for name, data in [("windows-1251", b"\xcf\xf0\xe8\xe2\xe5\xf2"), ("Shift_JIS", b"\x82\xa0"), ("ISO-8859-2", b"\xb1"),
                   ("GB18030", b"\xd6\xd0"), ("Big5", b"\xa4\xa4"), ("KOI8-R", b"\xf0\xd2\xc9"), ("EUC-KR", b"\xb0\xa1")]:
    status = Status(0)
    converter = ucnv_open(name.encode(), ctypes.byref(status))
    buffer = ctypes.create_unicode_buffer(64)
    length = ucnv_toUChars(converter, buffer, 64, data, len(data), ctypes.byref(status)) if converter else -1
    answers.append(["conversion", name, status.value, buffer[:length] if length >= 0 else None])
    if converter:
        ucnv_close(converter)

for locale, text in [("tr", "iı"), ("lt", "i\u0307"), ("de", "Straße"), ("el", "άλφα"), ("en", "Text")]:
    for convert in (u_strToUpper, u_strToLower):
        status = Status(0)
        buffer = ctypes.create_unicode_buffer(64)
        length = convert(buffer, 64, text, len(text), locale.encode(), ctypes.byref(status))
        answers.append(["case", locale, text, status.value, buffer[:length]])

for zone in ["Europe/Moscow", "America/New_York", "Asia/Tokyo", "Europe/Berlin", "Australia/Sydney", "Asia/Kolkata"]:
    for locale in ["ru", "en", "ja", "de", "hu", "pt_BR", "zh_TW", "ar", "he", "ko", "uk", "fr"]:
        status = Status(0)
        calendar = ucal_open(zone, len(zone), locale.encode(), 0, ctypes.byref(status))
        for kind in range(4):
            buffer = ctypes.create_unicode_buffer(128)
            length = ucal_getTimeZoneDisplayName(calendar, kind, locale.encode(), buffer, 128, ctypes.byref(status)) if calendar else -1
            answers.append(["zone", zone, locale, kind, status.value, buffer[:length] if length >= 0 else None])
        if calendar:
            ucal_close(calendar)

print(json.dumps(answers, ensure_ascii=False))
"""


def annotate(level: str, title: str, message: str):
    """An annotation of the run of GitHub Actions, in the output of Craft."""
    message = message.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
    print(f"::{level} title={title}::{message}", flush=True)


def catalog_domain(catalog: Path) -> str:
    """The translation domain of a catalog, e.g. kio6 for kio6.mo and kio6_qt.qm."""
    return catalog.stem.removesuffix("_qt")


class PeFile:
    """A program or DLL of Windows (Portable Executable)."""

    def __init__(self, path: Path):
        self.data = path.read_bytes()
        self.valid = len(self.data) >= 0x40 and self.data[:2] == b"MZ"
        if not self.valid:
            return
        pe = struct.unpack_from("<I", self.data, 0x3C)[0]
        self.valid = self.data[pe : pe + 4] == b"PE\0\0"
        if not self.valid:
            return
        coff = pe + 4
        section_count = struct.unpack_from("<H", self.data, coff + 2)[0]
        optional_size = struct.unpack_from("<H", self.data, coff + 16)[0]
        optional = coff + 20
        magic = struct.unpack_from("<H", self.data, optional)[0]
        # The data directories of PE32+ (64 bit) and PE32 start at different offsets.
        self.directories = optional + (112 if magic == 0x20B else 96)
        self.directory_count = struct.unpack_from("<I", self.data, self.directories - 4)[0]
        # Virtual address, virtual size, file offset and size in the file of each section.
        self.sections = []
        for i in range(section_count):
            virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from("<IIII", self.data, optional + optional_size + 40 * i + 8)
            self.sections.append((virtual_address, virtual_size, raw_pointer, raw_size))

    def section_of(self, rva):
        for section in self.sections:
            virtual_address, virtual_size, _, raw_size = section
            if virtual_address <= rva < virtual_address + max(virtual_size, raw_size):
                return section
        return None

    def offset(self, rva):
        """The position in the file of a relative virtual address."""
        section = self.section_of(rva)
        return None if section is None else rva - section[0] + section[2]

    def string(self, rva):
        start = self.offset(rva)
        if start is None:
            return None
        return self.data[start : self.data.index(b"\0", start)].decode("ascii", "replace")

    def directory(self, index):
        if index >= self.directory_count:
            return 0
        return struct.unpack_from("<I", self.data, self.directories + 8 * index)[0]

    def imports(self) -> set[str]:
        """The names of the imported DLLs, including delay loaded ones, in lower case."""
        if not self.valid:
            return set()
        names = set()
        # Import descriptors: 20 bytes each, the name of the DLL at offset 12.
        position = self.offset(self.directory(1)) if self.directory(1) else None
        while position is not None and position + 20 <= len(self.data):
            descriptor = struct.unpack_from("<5I", self.data, position)
            if not any(descriptor):
                break
            if name := self.string(descriptor[3]):
                names.add(name.lower())
            position += 20
        # Delay load descriptors: 32 bytes each, the name of the DLL at offset 4.
        position = self.offset(self.directory(13)) if self.directory(13) else None
        while position is not None and position + 32 <= len(self.data):
            descriptor = struct.unpack_from("<8I", self.data, position)
            if not any(descriptor):
                break
            if name := self.string(descriptor[1]):
                names.add(name.lower())
            position += 32
        return names

    def exports(self) -> dict[str, int]:
        """The exported symbols and their relative virtual addresses."""
        if not self.valid or not self.directory(0):
            return {}
        position = self.offset(self.directory(0))
        count, functions, names, ordinals = struct.unpack_from("<IIII", self.data, position + 24)
        result = {}
        for i in range(count):
            name = self.string(struct.unpack_from("<I", self.data, self.offset(names) + 4 * i)[0])
            ordinal = struct.unpack_from("<H", self.data, self.offset(ordinals) + 2 * i)[0]
            result[name] = struct.unpack_from("<I", self.data, self.offset(functions) + 4 * ordinal)[0]
        return result


def pe_imports(path: Path) -> set[str]:
    """The names of the DLLs that a program or DLL imports, including delay loaded ones, in lower case."""
    return PeFile(path).imports()


def binaries(path: Path):
    if path.is_dir():
        return [file for file in path.rglob("*") if file.suffix.lower() in (".dll", ".exe")]
    return [path] if path.suffix.lower() in (".dll", ".exe") else []


class Pruner:
    def __init__(self, archive: Path, source: Path, log, tools: Path = None):
        self.archive = archive
        self.bin = archive / "bin"
        self.source = source
        self.log = log
        # Where the tools of ICU are, from the installation of Craft.
        self.tools = tools
        self.removed = 0
        # Texts that identify translation catalogs and data of libraries, see find_texts().
        self.texts = set(LIBRARY_DATA.values())
        locale = self.bin / "data" / "locale"
        for catalog in locale.glob("*/LC_MESSAGES/*"):
            if catalog.suffix in (".mo", ".qm"):
                self.texts.add(catalog_domain(catalog))
        self.texts_in_removed_binaries = set()

    def find_texts(self, files) -> set[str]:
        """The texts that are found in the files, as Latin-1 or UTF-16 strings."""
        texts = sorted(self.texts, key=len, reverse=True)
        expression = re.compile(b"|".join(re.escape(text.encode("latin-1")) for text in texts))
        expression16 = re.compile(b"|".join(re.escape(text.encode("utf-16-le")) for text in texts))
        found = set()
        for file in files:
            data = file.read_bytes()
            found |= {match.decode("latin-1") for match in expression.findall(data)}
            found |= {match.decode("utf-16-le") for match in expression16.findall(data)}
        return found

    def remove(self, path: Path):
        if not path.exists():
            return
        self.texts_in_removed_binaries |= self.find_texts(binaries(path))
        if path.is_dir():
            size = sum(file.stat().st_size for file in path.rglob("*") if file.is_file())
            shutil.rmtree(path)
        else:
            size = path.stat().st_size
            path.unlink()
        self.removed += size
        self.log(f"Removing {path.relative_to(self.archive)} ({size / 1024 / 1024:.1f} MB)")

    def qml_modules(self):
        """The QML modules below bin: their URIs with their folders and the modules they depend on according to qmldir."""
        modules = {}
        for qmldir in self.bin.rglob("qmldir"):
            uri = None
            dependencies = set()
            for line in qmldir.read_text(encoding="utf-8", errors="replace").splitlines():
                words = line.split()
                if len(words) > 1 and words[0] == "module":
                    uri = words[1]
                elif len(words) > 1 and words[0] in ("depends", "import"):
                    dependencies.add(words[1])
                elif len(words) > 2 and words[:2] == ["default", "import"]:
                    dependencies.add(words[2])
                # "optional import" lists the styles that can be chosen. Only the chosen one is loaded.
            if uri:
                modules[uri] = (qmldir.parent, dependencies)
        return modules

    def qml_imports(self, directory: Path, module_directories: set[Path]) -> set[str]:
        """The modules that the QML and JavaScript files of a module import, without the ones of its submodules."""
        imports = set()
        for path in directory.iterdir():
            if path.is_dir():
                style_variant = path.name.lstrip("+") in UNUSED_STYLES and (path.name.startswith("+") or path.parent.name == "styles")
                if path not in module_directories and not style_variant:
                    imports |= self.qml_imports(path, module_directories)
            elif path.suffix in (".qml", ".js", ".mjs"):
                imports |= set(IMPORT_PATTERN.findall(path.read_text(encoding="utf-8", errors="replace")))
        return imports

    def prune_qml(self) -> set[Path]:
        """Removes the QML modules that aren't imported, returns the folders of the ones that are."""
        modules = self.qml_modules()
        module_directories = {directory for directory, _ in modules.values()}

        roots = set(QML_ROOTS)
        for path in (self.source / "src").rglob("*.qml"):
            roots |= set(IMPORT_PATTERN.findall(path.read_text(encoding="utf-8")))
        used = set()
        pending = list(roots)
        while pending:
            uri = pending.pop()
            if uri in used or uri not in modules:
                continue
            used.add(uri)
            directory, dependencies = modules[uri]
            pending += dependencies | self.qml_imports(directory, module_directories)
            # Internal modules, e.g. QtQuick.Controls.Basic.impl or org.kde.kirigami.forms.private.cards,
            # aren't always listed as dependencies, the imports can be in QML files compiled into the plugin.
            pending += [other for other in modules if other.startswith((uri + ".impl", uri + ".private"))]

        used_directories = {modules[uri][0] for uri in used}
        for uri, (directory, _) in sorted(modules.items()):
            if uri in used or not directory.exists():
                continue
            if any(used_directory.is_relative_to(directory) for used_directory in used_directories):
                # Only the files of the module itself, its submodules that are used stay.
                for path in directory.iterdir():
                    if path.is_file():
                        self.remove(path)
            else:
                self.remove(directory)
        return used_directories

    def prune_binaries(self, qml_directories: set[Path]):
        dlls = {path.name.lower(): path for path in self.bin.glob("*.dll")}
        imports = {}

        def imports_of(path):
            if path not in imports:
                imports[path] = pe_imports(path)
            return imports[path]

        used = set()

        def use(paths):
            pending = []
            for path in paths:
                pending += imports_of(path)
            while pending:
                name = pending.pop()
                if name in used or name not in dlls:
                    continue
                used.add(name)
                pending += imports_of(dlls[name])

        # The programs and the QML modules that are used.
        use([path for path in self.bin.glob("*.exe")])
        use([path for directory in qml_directories for path in directory.glob("*.dll")])

        # The plugins, as long as Foton loads the Qt and KDE libraries they need.
        plugins = [
            path
            for path in self.bin.rglob("*.dll")
            if path.parent != self.bin and not any(path.is_relative_to(directory) for directory in qml_directories)
        ]
        kept = set()
        changed = True
        while changed:
            changed = False
            for plugin in plugins:
                if plugin in kept:
                    continue
                frameworks = {name for name in imports_of(plugin) if name in dlls and FRAMEWORK_PATTERN.match(name)}
                if frameworks <= used:
                    kept.add(plugin)
                    use([plugin])
                    changed = True
        for plugin in plugins:
            if plugin not in kept:
                self.remove(plugin)

        for name, path in sorted(dlls.items()):
            if name not in used or UCRT_PATTERN.match(name):
                self.remove(path)

    def prune_translations(self):
        # The languages of Foton's interface.
        languages = {path.name for path in (self.source / "po").iterdir() if path.is_dir()}

        def supported(language):
            return language in languages or language.split("_")[0] in languages

        # Translations of KDE Frameworks, in one folder per language.
        locale = self.bin / "data" / "locale"
        if locale.is_dir():
            for directory in locale.iterdir():
                if directory.is_dir() and not supported(directory.name):
                    self.remove(directory)
        # Translations of Qt, e.g. qtbase_pt_BR.qm.
        translations = self.bin / "translations"
        if translations.is_dir():
            for path in translations.glob("*.qm"):
                catalog, _, language = path.stem.partition("_")
                if catalog not in QT_TRANSLATIONS or not supported(language):
                    self.remove(path)

    def prune_library_data(self):
        """Removes the translations and data of libraries that were removed."""
        orphaned = self.texts_in_removed_binaries - self.find_texts(binaries(self.archive))
        for catalog in (self.bin / "data" / "locale").glob("*/LC_MESSAGES/*"):
            if catalog.suffix in (".mo", ".qm") and catalog_domain(catalog) in orphaned:
                self.remove(catalog)
        for path, text in LIBRARY_DATA.items():
            if text in orphaned:
                self.remove(self.archive / path)

    def trim_icu(self):
        """Builds the data library of ICU again without the data that Qt never asks for."""
        libraries = list(self.bin.glob("icudt*.dll"))
        match = re.fullmatch(r"icudt(\d+)\.dll", libraries[0].name, re.IGNORECASE) if len(libraries) == 1 else None
        if not match:
            return
        library = libraries[0]
        version = match.group(1)
        icupkg = self.tools / "icupkg.exe" if self.tools else None
        genccode = self.tools / "genccode.exe" if self.tools else None
        linker = shutil.which("link.exe")
        if not icupkg or not icupkg.exists() or not genccode.exists() or not linker:
            annotate("warning", "ICU data not trimmed", f"The tools are missing: icupkg {icupkg}, genccode {genccode}, link {linker}")
            return

        # The data is exported by the library as one block, which is the content of an ICU .dat file.
        pe = PeFile(library)
        symbol = f"icudt{version}_dat"
        address = pe.exports().get(symbol)
        section = pe.section_of(address) if address else None
        if section is None:
            annotate("warning", "ICU data not trimmed", f"{library.name} doesn't export {symbol}")
            return
        start = pe.offset(address)
        # The end of the section. Bytes after the data become part of its last item, which ignores them.
        data = pe.data[start : section[2] + min(section[1], section[3])]
        if data[2:4] != b"\xda\x27" or data[12:16] != b"CmnD":
            annotate("warning", "ICU data not trimmed", f"{symbol} isn't the common data of ICU")
            return

        with tempfile.TemporaryDirectory() as work:
            work = Path(work)

            def run(*command):
                result = subprocess.run([str(part) for part in command], cwd=work, capture_output=True, text=True, timeout=600)
                if result.returncode != 0:
                    raise RuntimeError(f"{Path(command[0]).name} failed: {result.stdout} {result.stderr}")
                return result.stdout

            try:
                name = f"icudt{version}l"
                (work / f"{name}.dat").write_bytes(data)
                (work / "remove.txt").write_text("\n".join(UNUSED_ICU_DATA) + "\n")
                for directory in ("trimmed", "objects", "new", "original"):
                    (work / directory).mkdir()
                items = run(icupkg, "-l", f"{name}.dat").split()
                run(icupkg, "-r", "remove.txt", f"{name}.dat", f"trimmed\\{name}.dat")
                remaining = run(icupkg, "-l", f"trimmed\\{name}.dat").split()
                run(genccode, "-o", "-e", f"icudt{version}", "-d", "objects", f"trimmed\\{name}.dat")
                objects = list((work / "objects").glob("*.obj"))
                run(linker, "/NOLOGO", "/DLL", "/NOENTRY", "/MACHINE:X64", "/RELEASE", f"/EXPORT:{symbol},DATA", f"/OUT:new\\{library.name}", *objects)

                # The same answers from ICU with the original and with the trimmed data.
                answers = []
                for directory, data_library in (("original", library), ("new", work / "new" / library.name)):
                    for file in [*self.bin.glob(f"icuuc{version}.dll"), *self.bin.glob(f"icuin{version}.dll"), *self.bin.glob("vcruntime140*.dll"), *self.bin.glob("msvcp140*.dll")]:
                        shutil.copy2(file, work / directory)
                    if directory == "original":
                        shutil.copy2(data_library, work / directory)
                    result = subprocess.run([sys.executable, "-c", ICU_CHECK, str(work / directory), version], capture_output=True, text=True, encoding="utf-8", timeout=600)
                    if result.returncode != 0:
                        raise RuntimeError(f"ICU couldn't be asked: {result.stderr}")
                    answers.append(json.loads(result.stdout))
                if answers[0] != answers[1]:
                    differences = [f"{a} != {b}" for a, b in zip(*answers) if a != b]
                    raise RuntimeError("ICU answers differently with the trimmed data: " + " ".join(differences[:10]))

                original_size = library.stat().st_size
                new_size = (work / "new" / library.name).stat().st_size
                shutil.copy2(work / "new" / library.name, library)
                self.removed += original_size - new_size
                message = (
                    f"{library.name}: {original_size / 1024 / 1024:.1f} MB -> {new_size / 1024 / 1024:.1f} MB, "
                    f"{len(items) - len(remaining)} of {len(items)} items removed, {len(answers[0])} answers of ICU are the same"
                )
                self.log(message)
                annotate("notice", "ICU data trimmed", message)
            except Exception as error:
                annotate("warning", "ICU data not trimmed", str(error))

    def remove_empty_directories(self):
        for directory in sorted((path for path in self.archive.rglob("*") if path.is_dir()), key=lambda path: len(path.parts), reverse=True):
            if not any(directory.iterdir()):
                directory.rmdir()

    def run(self):
        for pattern in UNUSED_PLUGINS:
            for path in self.bin.glob(pattern):
                self.remove(path)
        for pattern in UNUSED_DATA:
            for path in self.archive.glob(pattern):
                self.remove(path)
        self.prune_translations()
        qml_directories = self.prune_qml()
        # Last, when only the plugins and QML modules that are used are left.
        self.prune_binaries(qml_directories)
        self.prune_library_data()
        self.trim_icu()
        self.remove_empty_directories()
        self.log(f"Removed {self.removed / 1024 / 1024:.1f} MB of files that Foton doesn't use")


def prune(archive: Path, source: Path, log=print, tools: Path = None) -> bool:
    Pruner(Path(archive), Path(source), log, Path(tools) if tools else None).run()
    return True


if __name__ == "__main__":
    prune(Path(sys.argv[1]), Path(sys.argv[2]), tools=Path(sys.argv[3]) if len(sys.argv) > 3 else None)
