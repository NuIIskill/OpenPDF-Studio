# Alles, was mit der Business-Lizenz zu tun hat, in einem Ordner: der Zustand
# (LicenseStore), die Einstellungsseite (LicensePage) und die beiden Hinweise
# (LicenseNotice). Ausnahmsweise liegen hier Logik und Widget zusammen — die
# Lizenzgrenze wird um ein Verzeichnis gezogen, nicht quer durch mehrere.
target_sources(OpenPDFStudio PRIVATE
    LicenseNotice.cpp
    LicenseNotice.hpp
    LicensePage.cpp
    LicensePage.hpp
    LicenseStore.cpp
    LicenseStore.hpp
)
