cmake_minimum_required(VERSION 3.20)

# Zentrale Versionsquelle: version.txt im Repo-Root (auch von build.sh und
# packaging/windows/package-win.sh gelesen).
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" OPENPDF_VERSION)
string(STRIP "${OPENPDF_VERSION}" OPENPDF_VERSION)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS version.txt)

project(OpenPDFStudio
    VERSION ${OPENPDF_VERSION}
    DESCRIPTION "OpenPDF Studio - Modern PDF Editor"
    LANGUAGES CXX
)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui Network)
find_package(Qt6 OPTIONAL_COMPONENTS PrintSupport LinguistTools)
qt_standard_project_setup()

set(RESOURCES
    resources/openpdf-studio.qrc
)

# The executable starts empty; every source folder adds its own files through
# target_sources() in its CMakeLists.txt. Keeping the list next to the code
# means a new file is registered where it is created, and two branches adding
# files in different folders no longer collide in one central list.
add_executable(OpenPDFStudio
    ${RESOURCES}
)

add_subdirectory(src)

target_compile_definitions(OpenPDFStudio PRIVATE
    QT_DISABLE_DEPRECATED_BEFORE=0x060000
    QT_NO_KEYWORDS
    APP_VERSION="${PROJECT_VERSION}"
)

target_include_directories(OpenPDFStudio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(OpenPDFStudio PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
    # Nur für die Update-Prüfung (app/UpdateChecker) — der einzige Netzzugriff
    # im Programm. Teil von qtbase, deshalb REQUIRED statt optional.
    Qt6::Network
)

# ── PDFium ───────────────────────────────────────────────────────────────────
# Bezogen über packaging/fetch-pdfium.sh (Version gepinnt, Prüfsumme geprüft).
# Dieselbe Engine, die auch in Qt6::Pdf steckt — hier ohne Qt-Hülle und auf
# beiden Plattformen in derselben Version.
#
# Seit Schritt 10 das EINZIGE Backend: Qt6::Pdf und Poppler sind draußen. Ohne
# PDFium kann dieser Build keine PDFs anzeigen — die Anwendung startet, aber
# HAVE_PDF_RENDERING ist nicht gesetzt und jede Dokumentfunktion fehlt.
#
# PDFium_DIR wird direkt gesetzt statt über CMAKE_PREFIX_PATH: beim Cross-Build
# sucht find_package() wegen CMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY nur noch im
# Sysroot — ein direkt gesetztes <Paket>_DIR gilt trotzdem.
if(NOT DEFINED PDFium_DIR)
    if(WIN32)
        set(PDFium_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pdfium/win-x64")
    else()
        set(PDFium_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pdfium/linux-x64")
    endif()
endif()
# PDFiumConfig.cmake sucht seine Dateien mit find_path()/find_file(). Beim
# Cross-Build beschränkt die mingw-Toolchain beide auf CMAKE_FIND_ROOT_PATH
# (MODE_INCLUDE/LIBRARY = ONLY), und der projektlokale Prefix liegt außerhalb —
# gefunden würde dort nichts. Für diesen einen Aufruf auf BOTH stellen und
# danach zurücksetzen; die gesuchten Namen (fpdfview.h, pdfium.dll) gibt es auf
# dem Host nicht, es kann also nichts Falsches einsammeln.
set(_openpdf_saved_include_mode "${CMAKE_FIND_ROOT_PATH_MODE_INCLUDE}")
set(_openpdf_saved_library_mode "${CMAKE_FIND_ROOT_PATH_MODE_LIBRARY}")
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
find_package(PDFium QUIET)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE "${_openpdf_saved_include_mode}")
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY "${_openpdf_saved_library_mode}")
if(PDFium_FOUND)
    message(STATUS "PDFium ${PDFium_VERSION}: Backend gebaut")
    target_compile_definitions(OpenPDFStudio PRIVATE HAVE_PDFIUM HAVE_PDF_RENDERING)
    target_link_libraries(OpenPDFStudio PRIVATE pdfium)
    # Die Bibliothek liegt unter third_party/ und nicht im Systempfad; ohne
    # RPATH findet das gebaute Programm sie beim Start nicht. Für die
    # Auslieferung setzt das Packaging seinen eigenen Pfad (Schritt 11).
    if(UNIX AND NOT APPLE)
        get_filename_component(_pdfium_libdir "${PDFium_LIBRARY}" DIRECTORY)
        set_property(TARGET OpenPDFStudio APPEND PROPERTY BUILD_RPATH "${_pdfium_libdir}")
    endif()
else()
    message(WARNING "PDFium fehlt — dieser Build kann keine PDFs anzeigen.\n"
                    "    Beheben mit: packaging/fetch-pdfium.sh")
endif()

if(Qt6PrintSupport_FOUND)
    target_compile_definitions(OpenPDFStudio PRIVATE HAVE_QT_PRINT)
    target_link_libraries(OpenPDFStudio PRIVATE Qt6::PrintSupport)
endif()

target_include_directories(OpenPDFStudio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/3rdparty
)

# PkgConfig is used as a fallback for qpdf and Tesseract on native builds only.
# Cross-compilation must not use host pkg-config — it returns Linux headers that
# conflict with the Windows sysroot (e.g. /usr/include/stdint.h vs MinGW types).
if(NOT CMAKE_CROSSCOMPILING)
    find_package(PkgConfig QUIET)
endif()

# ── qpdf: vector PDF content-stream overlay (true text replacement) ───────────
find_package(qpdf QUIET CONFIG)
if(qpdf_FOUND)
    message(STATUS "qpdf ${qpdf_VERSION}: vector PDF save enabled")
    target_compile_definitions(OpenPDFStudio PRIVATE HAVE_QPDF)
    set(OPENPDF_HAVE_QPDF TRUE)
    # qpdf::libqpdf, nicht "qpdf": der Config-Export definiert nur das Ziel mit
    # Namensraum. Ein blankes "qpdf" ist für CMake bloß ein Bibliotheksname und
    # bringt keine Include-Pfade mit — auf Linux fällt das nicht auf, weil die
    # Header ohnehin unter /usr/include liegen, beim Cross-Build fehlen sie.
    target_link_libraries(OpenPDFStudio PRIVATE qpdf::libqpdf)
elseif(PkgConfig_FOUND)
    pkg_check_modules(QPDF QUIET libqpdf)
    if(QPDF_FOUND)
        message(STATUS "qpdf (pkg-config): vector PDF save enabled")
        target_compile_definitions(OpenPDFStudio PRIVATE HAVE_QPDF)
        set(OPENPDF_HAVE_QPDF TRUE)
        target_include_directories(OpenPDFStudio PRIVATE ${QPDF_INCLUDE_DIRS})
        target_link_libraries(OpenPDFStudio PRIVATE ${QPDF_LIBRARIES})
    else()
        message(STATUS "qpdf: not found — sudo dnf install qpdf-devel for vector PDF output")
    endif()
endif()

# ── Rich Media: modules/rich-media (source-available, siehe LICENSES/) ───────
# Licensed separately and therefore its own directory. A checkout without
# modules/ configures and builds unchanged; only playback and embedding are
# missing then.
#
# qpdf is required: PDFium can neither read nor write the nested dictionaries
# of a RichMedia annotation, so without it the module stays out entirely.
option(OPENPDF_RICH_MEDIA "Build the rich-media module when it is present" ON)

if(OPENPDF_RICH_MEDIA AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/modules/rich-media/CMakeLists.txt")
    # Qt Multimedia is what decodes everywhere except Windows. There the
    # module brings its own engine (MFPlay, DirectShow behind it), because the
    # backend packaged for MinGW refuses files Windows itself plays, so Qt
    # Multimedia is neither needed nor linked.
    if(NOT WIN32)
        find_package(Qt6 QUIET OPTIONAL_COMPONENTS Multimedia)
        set(_openpdf_media_ok ${Qt6Multimedia_FOUND})
    else()
        set(_openpdf_media_ok TRUE)
    endif()

    # qpdf is required either way: PDFium can neither read nor write the nested
    # dictionaries of a RichMedia annotation. A build missing one of these used
    # to succeed and then behave differently from every other build, with
    # settings that quietly did nothing. Half a module is worse than none.
    if(OPENPDF_HAVE_QPDF AND _openpdf_media_ok)
        message(STATUS "Rich Media: Modul gebaut")
        target_compile_definitions(OpenPDFStudio PRIVATE HAVE_RICH_MEDIA)
        target_include_directories(OpenPDFStudio PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/modules)
        if(WIN32)
            # DirectShow, dahinter MFPlay. Alles Teil von Windows, es wird
            # dafür nichts mitgeliefert. mfplay/mfplat fehlen hier absichtlich:
            # die beiden DLLs werden zur Laufzeit geladen, damit ein Windows N
            # ohne Media Feature Pack das Programm noch starten kann.
            target_link_libraries(OpenPDFStudio PRIVATE
                mfuuid strmiids ole32 oleaut32 uuid)
        else()
            target_link_libraries(OpenPDFStudio PRIVATE Qt6::Multimedia)
        endif()
        add_subdirectory(modules/rich-media)
    else()
        set(_openpdf_missing "")
        if(NOT OPENPDF_HAVE_QPDF)
            list(APPEND _openpdf_missing "qpdf-devel")
        endif()
        if(NOT _openpdf_media_ok)
            list(APPEND _openpdf_missing "qt6-qtmultimedia-devel")
        endif()
        list(JOIN _openpdf_missing " " _openpdf_missing_line)
        message(WARNING
            "Rich Media: nicht gebaut — dieser Build kann Medien weder "
            "abspielen noch einbetten.\n"
            "    Beheben mit: sudo dnf install ${_openpdf_missing_line}")
    endif()
endif()

# ── OCR via Tesseract ─────────────────────────────────────────────────────────
# Optional dependency: install tesseract-devel and tesseract-langpack-deu|eng
if(PkgConfig_FOUND)
    pkg_check_modules(TESSERACT QUIET tesseract)
endif()
if(TESSERACT_FOUND)
    message(STATUS "OCR backend: Tesseract ${TESSERACT_VERSION}")
    target_compile_definitions(OpenPDFStudio PRIVATE HAVE_TESSERACT)
    target_include_directories(OpenPDFStudio PRIVATE ${TESSERACT_INCLUDE_DIRS})
    target_link_libraries(OpenPDFStudio PRIVATE ${TESSERACT_LIBRARIES})
else()
    message(STATUS "OCR backend: none  (sudo dnf install tesseract-devel tesseract-langpack-deu)")
endif()

if(UNIX AND NOT APPLE)
    # Value is hardcoded in main.cpp to avoid CMake semicolon-as-list-separator issues.
    target_compile_definitions(OpenPDFStudio PRIVATE DEFAULT_QPA_PLATFORM)
endif()

if(WIN32)
    set_target_properties(OpenPDFStudio PROPERTIES WIN32_EXECUTABLE TRUE)
    configure_file(resources/windows/openpdf-studio.rc.in
                   "${CMAKE_CURRENT_BINARY_DIR}/openpdf-studio.rc" @ONLY)
    target_sources(OpenPDFStudio PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/openpdf-studio.rc")
    # windres embeds the .ico into the .res, but CMake does not scan .rc files
    # for their includes — without this the resource is never recompiled when
    # only the icon changes, and the exe silently keeps the previous icon.
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/openpdf-studio.rc"
        PROPERTIES OBJECT_DEPENDS
                   "${CMAKE_CURRENT_SOURCE_DIR}/resources/windows/openpdf-studio.ico")
endif()

if(MINGW)
    # MinGW <ratio>/<chrono_io.h> system headers use 'long int' (32-bit, Windows LLP64)
    # for ratio template params, but atto/femto/pico/tera/peta/exa need 64-bit values.
    # GCC reports this as [-Wnarrowing]/[-Wtemplate-body] errors in C++20. Our code is
    # fine — suppress only these system-header diagnostics.
    target_compile_options(OpenPDFStudio PRIVATE -Wno-narrowing -Wno-template-body)
endif()

install(TARGETS OpenPDFStudio
    RUNTIME DESTINATION bin
)

# PDFium wird mitgeliefert: es gibt dafür kein Systempaket, weder in Fedora noch
# in Debian. Die Bibliothek landet in einem eigenen Verzeichnis und wird über
# RPATH gefunden — nicht in /usr/lib, wo sie mit nichts kollidieren soll.
if(PDFium_FOUND AND UNIX AND NOT APPLE)
    install(FILES "${PDFium_LIBRARY}"
        DESTINATION lib/openpdf-studio
    )
    set_target_properties(OpenPDFStudio PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib/openpdf-studio"
    )
endif()

install(FILES packaging/appimage/openpdf-studio.desktop
    DESTINATION share/applications
    OPTIONAL
)

# Die Lizenzübersicht allein reicht nicht: sie verweist auf LICENSES/, also
# muss das Verzeichnis mit ausgeliefert werden.
install(FILES LICENSE
    DESTINATION share/doc/OpenPDFStudio
)
install(DIRECTORY LICENSES
    DESTINATION share/doc/OpenPDFStudio
)

install(FILES resources/icons/openpdf-studio.png
    DESTINATION share/icons/hicolor/256x256/apps
    OPTIONAL
)

# tests/ is not part of the repository. Built only when it happens to be
# present locally, so a clone without it still configures.
if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()

install(FILES packaging/appimage/README.md
    DESTINATION share/doc/OpenPDFStudio
    OPTIONAL
)

# ── CPack: RPM + DEB ─────────────────────────────────────────────────────────
# Gebaut über packaging/linux/package-linux.sh (nicht direkt cpack aufrufen —
# das Skript setzt Prefix/Ausgabepfad und schiebt die Artefakte nach dist/).
if(UNIX AND NOT APPLE)
    set(CPACK_PACKAGE_NAME "openpdf-studio")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
    set(CPACK_PACKAGE_VENDOR "nullskill")
    set(CPACK_PACKAGE_CONTACT "nullskill <nullskilll@gmail.com>")
    set(CPACK_PACKAGE_DESCRIPTION
"OpenPDF Studio is a native Qt6 PDF viewer and editor: inline text editing,
page organizer, annotations, OCR and export to PDF, DOCX and image formats.")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://openpdf-studio.nullskill.de")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_PACKAGE_FILE_NAME
        "openpdf-studio-${PROJECT_VERSION}-${CMAKE_SYSTEM_PROCESSOR}")
    set(CPACK_STRIP_FILES ON)

    # ── RPM (Fedora/RHEL/openSUSE) ───────────────────────────────────────────
    # Two licenses because two parts are in the package: the Core and, when
    # built along, modules/rich-media under the Business license.
    if(TARGET OpenPDFStudio AND OPENPDF_RICH_MEDIA
       AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/modules/rich-media/CMakeLists.txt")
        set(CPACK_RPM_PACKAGE_LICENSE
            "(GPL-3.0-only OR LicenseRef-OpenPDF-Commercial) AND LicenseRef-OpenPDF-Business")
    else()
        set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-only OR LicenseRef-OpenPDF-Commercial")
    endif()
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Productivity")
    # qt6-qtmultimedia is not listed: AUTOREQ derives it from the binary's
    # link against libQt6Multimedia.so.6, and the backend plugins ship in that
    # same package. ffmpeg only converts video on insert and stays optional.
    set(CPACK_RPM_PACKAGE_SUGGESTS "ffmpeg-free")
    set(CPACK_RPM_PACKAGE_URL "${CPACK_PACKAGE_HOMEPAGE_URL}")
    # Der RPM-Generator übernimmt CPACK_PACKAGE_DESCRIPTION nicht automatisch.
    set(CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION}")
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
    # rpmbuild leitet Requires automatisch aus den ELF-Sonames ab. Die
    # mitgelieferte libpdfium.so darf dabei nicht als fremde Abhängigkeit
    # auftauchen — sie liegt im Paket selbst und wird über RPATH gefunden.
    # AUTOPROV OFF allein genügt dafür nicht: das unterdrückt nur das Provides
    # der Bibliothek, das aus dem Binary abgeleitete Requires bleibt stehen und
    # ist dann von nichts erfüllbar — dnf bricht mit "nothing provides
    # libpdfium.so()(64bit)" ab. Die Requires-Seite muss deshalb zusätzlich
    # über den rpm-Filter raus. Der doppelte Backslash ist Absicht: der
    # Makro-Expander von rpm frisst eine Ebene, in der Spec landet \\. als \.
    set(CPACK_RPM_PACKAGE_AUTOREQ ON)
    set(CPACK_RPM_PACKAGE_AUTOPROV OFF)
    set(CPACK_RPM_SPEC_MORE_DEFINE "%global __requires_exclude ^libpdfium\\\\.so")
    # Diese Verzeichnisse gehören filesystem/hicolor-icon-theme — nicht mitpaketieren.
    set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
        /usr/share/applications
        /usr/share/icons
        /usr/share/icons/hicolor
        /usr/share/icons/hicolor/256x256
        /usr/share/icons/hicolor/256x256/apps
        /usr/share/doc
    )

    # ── DEB (Debian/Ubuntu) ──────────────────────────────────────────────────
    # dpkg-shlibdeps funktioniert beim Bauen auf Fedora nicht (keine Debian-
    # Shlib-Datenbank) → Depends werden explizit gepflegt. Alternativen-Syntax
    # deckt die t64-Umbenennung ab Ubuntu 24.04 mit ab.
    set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
    # Everything the binary links, and only that. This list stood short of
    # qpdf and Tesseract for a while: both are linked when they are found at
    # build time, so a package without them does not start, and listing them
    # as Recommends left dpkg free to leave them out. The RPM never had the
    # problem because AUTOREQ reads the sonames out of the binary.
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libqt6core6 | libqt6core6t64, libqt6gui6, libqt6widgets6, libqt6printsupport6, libqt6network6, libqt6multimedia6, libqpdf29 | libqpdf29t64, libtesseract5 | libtesseract5t64")
    # libqt6pdf6 ist raus: die PDF-Engine ist PDFium und liegt im Paket.
    # ffmpeg only converts video while embedding it and is genuinely optional.
    set(CPACK_DEBIAN_PACKAGE_SUGGESTS "ffmpeg")

    include(CPack)
endif()

# Translation update helper — run: cmake --build . --target update_translations
# Pre-compiled .qm files are already embedded via resources/openpdf-studio.qrc.
# This target only updates the .ts source files for translators.
if(Qt6LinguistTools_FOUND AND NOT CMAKE_CROSSCOMPILING)
    set(TS_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/openpdf_de.ts
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/openpdf_fr.ts
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/openpdf_es.ts
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/openpdf_it.ts
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/openpdf_pt.ts
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/openpdf_nl.ts
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/openpdf_pl.ts
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/openpdf_ru.ts
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/openpdf_zh.ts
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/openpdf_ja.ts
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/openpdf_ko.ts
    )
    qt_add_lupdate(OpenPDFStudio
        TS_FILES ${TS_FILES}
        LUPDATE_TARGET update_translations
        NO_GLOBAL_TARGET
    )
endif()
