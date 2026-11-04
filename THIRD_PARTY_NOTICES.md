# DesignRC Third-Party Notices

DesignRC is Copyright (C) 2026 Barry Foust and is licensed under the GNU General Public License
version 3 only (`GPL-3.0-only`). See `LICENSE`.

DesignRC dynamically links the following separately licensed libraries. The distributed DLLs must
remain replaceable by compatible, user-modified builds. Distribution terms must not prohibit
reverse engineering when it is necessary to debug modifications to these libraries.

## Qt 6

DesignRC uses Qt Core, GUI, Widgets, OpenGL support, and platform runtime plugins. Windows releases
bundle Qt 6.11.1. The Ubuntu 26.04 package bundles Qt 6.10.2 from Ubuntu's `qt6-base` package;
the Fedora 44 package bundles Qt 6.11.2. Qt is
Copyright (C) The Qt Company Ltd. and other contributors. The Qt components used by DesignRC are
distributed under the GNU Lesser General Public License version 3 (`LGPL-3.0-only`). DesignRC does
not modify Qt.

- Project: https://www.qt.io/
- Windows QtBase source: https://code.qt.io/cgit/qt/qtbase.git/tag/?h=v6.11.1
- Ubuntu QtBase source package: https://packages.ubuntu.com/source/resolute/qt6-base
- Fedora QtBase source package: https://src.fedoraproject.org/rpms/qt6-qtbase
- License: `licenses/QT-LGPL-3.0.txt`
- GNU GPL version 3 incorporated by the LGPL: `licenses/DESIGNRC-GPL-3.0.txt`

Qt contains third-party components under their own licenses. The exact components deployed with a
release are documented by Qt's generated SBOM. The build copies the QtBase and QtSvg SPDX documents
to `licenses/qt-sbom` and Qt's relevant attribution pages to `licenses/qt-attributions`.

The Windows deployment includes `opengl32sw.dll`, the Mesa llvmpipe software OpenGL renderer,
under its permissive licenses. Its complete Qt-supplied attribution is installed as
`licenses/qt-attributions/qt-attribution-llvmpipe.html`. `D3Dcompiler_47.dll`, `dxcompiler.dll`,
and `dxil.dll` are Microsoft redistributable components collected by `windeployqt` and remain under
Microsoft's applicable terms.

## Open CASCADE Technology

DesignRC uses Open CASCADE Technology (OCCT), Copyright (C) 1999-2026 OPEN CASCADE S.A.S. OCCT is
distributed under the GNU Lesser General Public License version 2.1 with the Open CASCADE exception.
Windows and Ubuntu packages use OCCT 8.0.0; Fedora uses OCCT 7.9.3.
DesignRC does not modify OCCT.

- Project: https://dev.opencascade.org/
- Exact source: https://github.com/Open-Cascade-SAS/OCCT/tree/V8_0_0
- Fedora version source: https://github.com/Open-Cascade-SAS/OCCT/tree/V7_9_3
- License: `licenses/OCCT-LGPL-2.1.txt`
- Exception: `licenses/OCCT-LGPL-EXCEPTION.txt`

## FreeType

Windows releases distribute FreeType 2.13.3 for OCCT font support. The Ubuntu package uses the
system-provided FreeType 2.14.2 shared library instead of bundling it; Fedora uses FreeType 2.14.3.
FreeType is Copyright (C)
1996-2024 David Turner, Robert Wilhelm, Werner Lemberg, and other contributors. The Windows
distribution uses the FreeType License (FTL), a BSD-style license with an attribution requirement.
DesignRC does not modify FreeType.

- Project: https://freetype.org/
- Exact source: https://download.savannah.gnu.org/releases/freetype/freetype-2.13.3.tar.xz
- License overview: `licenses/FREETYPE-LICENSE.txt`
- FreeType License: `licenses/FREETYPE-FTL.txt`

## International Components for Unicode

The Debian/Ubuntu package bundles the ICU 78 runtime libraries required by its Qt 6.10.2 build so
the package remains installable on Debian releases whose repositories provide a different ICU ABI.
The Fedora package bundles ICU 77 for its Qt build.
ICU is Copyright (C) 1991-2022 Unicode, Inc. and other contributors and is distributed under its
permissive license. DesignRC does not modify ICU.

- Project: https://icu.unicode.org/
- Ubuntu source package: https://packages.ubuntu.com/source/resolute/icu
- Fedora source package: https://src.fedoraproject.org/rpms/icu
- License: `licenses/ICU-LICENSE.txt`

Portions of Qt and OCCT may use additional third-party components. Before publishing an installer,
compare its complete DLL and plugin inventory with the packaged Qt and OCCT SBOM/license data and
add any required notices. Microsoft runtime components, when included, are redistributed under
Microsoft's applicable terms and are not covered by DesignRC's GPL license.
