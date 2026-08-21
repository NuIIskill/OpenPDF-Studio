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

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui)
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
        target_include_directories(OpenPDFStudio PRIVATE ${QPDF_INCLUDE_DIRS})
        target_link_libraries(OpenPDFStudio PRIVATE ${QPDF_LIBRARIES})
    else()
        message(STATUS "qpdf: not found — sudo dnf install qpdf-devel for vector PDF output")
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
    # Core-Lizenz. Die Business-lizenzierten Module (modules/) sind hier noch
    # nicht enthalten — sobald eines mitpaketiert wird, gehört es in die Angabe.
    set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-only OR LicenseRef-OpenPDF-Commercial")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Productivity")
    set(CPACK_RPM_PACKAGE_URL "${CPACK_PACKAGE_HOMEPAGE_URL}")
    # Der RPM-Generator übernimmt CPACK_PACKAGE_DESCRIPTION nicht automatisch.
    set(CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION}")
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
    # rpmbuild leitet Requires automatisch aus den ELF-Sonames ab. Die
    # mitgelieferte libpdfium.so darf dabei nicht als fremde Abhängigkeit
    # auftauchen — sie liegt im Paket selbst.
    set(CPACK_RPM_PACKAGE_AUTOREQ ON)
    set(CPACK_RPM_PACKAGE_AUTOPROV OFF)
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
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libqt6core6 | libqt6core6t64, libqt6gui6, libqt6widgets6, libqt6printsupport6")
    # libqt6pdf6 ist raus: die PDF-Engine ist PDFium und liegt im Paket.
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS
        "libqpdf29 | libqpdf29t64, libtesseract5 | libtesseract5t64")

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
