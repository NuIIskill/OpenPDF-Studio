Name:           openpdf-studio
Version:        @VERSION@
Release:        1%{?dist}
Summary:        Modern PDF editor built with Qt6

License:        MIT
URL:            https://github.com/NuIIskill/OpenPDF-Studio

# Runtime dependencies only — binary is pre-built by build.sh
Requires:       qt6-qtbase%{?_isa}
Requires:       qt6-qtbase-gui%{?_isa}
Requires:       qt6-qtsvg%{?_isa}
Requires:       (qt6-qtpdf%{?_isa} or poppler-qt6%{?_isa})

%description
OpenPDF Studio is a modern, cross-platform PDF editor built with Qt6.
It allows you to open, view, annotate and edit text in PDF documents.

%install
cp -a %{pkgroot}/. %{buildroot}/

%files
%license %{_datadir}/doc/OpenPDFStudio/LICENSE
%doc %{_datadir}/doc/OpenPDFStudio/README.md
%{_bindir}/OpenPDFStudio
%{_datadir}/applications/openpdf-studio.desktop
%{_datadir}/icons/hicolor/256x256/apps/openpdf-studio.png
%{_datadir}/metainfo/io.openpdfstudio.OpenPDFStudio.metainfo.xml

%changelog
* Wed Jun 17 2026 OpenPDF Studio Team <noreply@openpdfstudio.io> - @VERSION@-1
- Initial package release
