# Fails the build when a PDF can be written past the history manager (CLAUDE.md, rule 6).
# Run as: cmake -DROOT=<source dir> -P cmake/CheckWritePaths.cmake

# Files that may use the low-level writers. Each takes a WriteGrant or writes no PDF.
set(WRITERS
    src/app/SignatureProfileStore.cpp
    src/app/SignatureStore.cpp
    src/app/TrustedSigners.cpp
    src/engine/document/BookmarkWriter.cpp
    src/engine/document/KeptText.cpp
    src/engine/document/OrganizerWriter.cpp
    src/engine/document/OriginalContent.cpp
    src/engine/document/PdfConformance.cpp
    src/engine/document/PdfiumWriter.cpp
    src/engine/document/ProtectionWriter.cpp
    src/engine/export/PdfExporter.cpp
    src/engine/import/TextDocumentPdf.cpp
    src/engine/redact/RedactionWriter.cpp
    src/engine/sign/FileCertificates.cpp
    src/engine/sign/SignatureManager.cpp
    src/engine/sign/SignatureRemover.cpp
    src/ui/DocumentView_Document.cpp
    src/ui/export/Exporting.cpp
    modules/rich-media/engine/RichMediaWriter.cpp
)

# Files that may hand out a WriteGrant.
set(ISSUERS
    src/engine/document/WriteGrant.hpp
    src/engine/document/WriteGrant.cpp
    src/ui/DocumentView.cpp
    src/ui/DocumentView_Document.cpp
    src/ui/DocumentView_Edit.cpp
    src/ui/export/Exporting.cpp
    src/ui/import/Importing.cpp
    src/cli/CliWrites.hpp
)

file(GLOB_RECURSE sources RELATIVE "${ROOT}"
     "${ROOT}/src/*.cpp" "${ROOT}/src/*.hpp" "${ROOT}/modules/*.cpp" "${ROOT}/modules/*.hpp")
list(FILTER sources EXCLUDE REGEX "^src/3rdparty/")

set(failures "")
foreach(file IN LISTS sources)
    file(READ "${ROOT}/${file}" text)
    if(text MATCHES "QPDFWriter|QPdfWriter|FPDF_SaveAsCopy|FPDF_SaveWithVersion|SafeWrite::commit")
        if(NOT file IN_LIST WRITERS)
            string(APPEND failures "\n  ${file}: writes a file with a low-level writer")
        endif()
    endif()
    if(text MATCHES "(^|[^:A-Za-z_&])WriteGrant\\(")
        if(NOT file IN_LIST ISSUERS)
            string(APPEND failures "\n  ${file}: hands out a WriteGrant")
        endif()
    endif()
    # An issuer's name is only ever defined by the issuer itself.
    foreach(issuer IN ITEMS "struct CliWrites:src/cli/CliWrites.hpp"
                            "class DocumentView:src/ui/DocumentView.hpp"
                            "class Exporting:src/ui/export/Exporting.hpp"
                            "class Importing:src/ui/import/Importing.hpp")
        string(REPLACE ":" ";" pair "${issuer}")
        list(GET pair 0 name)
        list(GET pair 1 home)
        if(text MATCHES "${name}[ \t\r\n]*[:{]" AND NOT file STREQUAL home)
            string(APPEND failures "\n  ${file}: defines ${name}, an issuer of WriteGrant")
        endif()
    endforeach()
endforeach()

file(READ "${ROOT}/src/engine/document/WriteGrant.hpp" grantHeader)
string(REGEX MATCHALL "friend [a-z]+ [A-Za-z_]+" friends "${grantHeader}")
set(expected "friend class DocumentView" "friend class Exporting" "friend class Importing"
             "friend struct CliWrites")
if(NOT friends STREQUAL expected)
    string(APPEND failures "\n  src/engine/document/WriteGrant.hpp: the issuers changed")
endif()

if(failures)
    message(FATAL_ERROR
        "PDF writes must go through the history manager (CLAUDE.md, rule 6):${failures}\n"
        "Express the change as journal state and let the save write it (PageOverlay::writeTo, "
        "openWorkingCopy). Adding a writer or an issuer needs the owner's explicit consent.")
endif()
