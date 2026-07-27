# DesignRC

DesignRC is a parametric desktop application for designing built-up RC airplane wings. It creates
manufacturing geometry and a complete mirrored-wing preview from a half-wing definition. The
current release is **1.0**.

> **Platform status:** DesignRC is available for Windows 11 x64, Debian/Ubuntu x86-64, and Fedora
> x86-64. The Debian package has been tested on Ubuntu 24.04 under WSL 2 with WSLg, and the RPM
> package has been tested in Fedora 44.

## What DesignRC does

- Designs one or more connected half-wing panels and mirrors them into a complete wing.
- Imports root and tip airfoils from Selig-style `.dat` coordinate files.
- Interpolates airfoil profiles, chord, sweep, twist, and rib positions across each panel.
- Generates solid ribs, spars, shear webs, sheeting, leading and trailing edges, turbulators,
  ailerons, flaps, hinge posts, and wing joiners.
- Displays the assembled wing in an interactive OpenCascade 3D viewport.
- Produces a flattened, annotated, full-scale technical plan of both wing halves.
- Exports the plan as a custom-page vector PDF at 1:1 scale.
- Exports selected laser-cut parts as full-scale DXF, SVG, and/or PDF files.
- Exports the material-colored 3D assembly as STEP.
- Saves editable projects in the `.designrc` format.
- Supports global millimeter or inch display units and per-parameter unit overrides. Inch spinner
  values advance in 1/32-inch increments and display exact 1/32-inch values as fractions.

The application contains detailed installed HTML help. After building, select **Help > Help** or
press **F1**.

## Download

Release 1.0 provides three installers on the
[DesignRC GitHub Releases page](https://github.com/barryfx/DesignRC/releases/tag/1.0):

- `DesignRC-1.0-Windows-x64-Setup.exe` for Windows 11 x64. Download and run the installer. Because
  it is not code-signed, Windows may display a warning before allowing it to run.
- `designrc_1.0_amd64.deb` for Debian/Ubuntu x86-64. From the download directory, install and run it
  with:

  ```bash
  sudo apt install ./designrc_1.0_amd64.deb
  designrc
  ```

  APT installs the required distribution runtime libraries automatically. Qt 6.4.2, OCCT 8.0,
  and the ICU 74 runtime required by the bundled Qt build are included. Bundling ICU avoids an
  unavailable `libicu74` dependency on Debian releases that provide a different ICU ABI. On WSL,
  install `wslu` and `xdg-utils` if **Help > Help** should open in the Windows default browser:

  ```bash
  sudo apt install wslu xdg-utils
  ```

- `designrc-1.0-1.x86_64.rpm` for Fedora x86-64. From the download directory, install and run it
  with:

  ```bash
  sudo dnf install ./designrc-1.0-1.x86_64.rpm
  designrc
  ```

The release also includes source archives and `DesignRC-1.0-SHA256SUMS.txt`. Use the checksum file
to verify a download before installing it.

## Typical use

1. Select the number of wing panels.
2. Configure each panel on the **Specs**, **Spars**, **LE/TE**, **Ailerons/Flaps**, and **Joiner**
   tabs.
3. Import root and tip airfoil `.dat` files where required.
4. Press **Generate Wing** to validate the design and build the 3D geometry.
5. Inspect the wing with left-drag orbit, right-drag pan, and mouse-wheel zoom.
6. Press **Generate Plan** to create the flattened technical drawing.
7. Export the plan to PDF or export selected cutting parts to DXF/SVG.
8. Save the editable design as a `.designrc` project.

Parameter edits do not automatically rebuild geometry. This allows several values to be changed
before running the potentially expensive **Generate Wing** operation.

## Source layout

```text
DesignRC/
|-- resources/help/     Installed HTML user help
|-- src/domain/         Airfoil, wing structure, and DXF/SVG export logic
|-- src/geometry/       OpenCascade solid and preview construction
|-- src/gui/            Qt main window, editors, 3D viewport, and plan drawing
|-- tests/              Domain, GUI, and OpenCascade geometry regression tests
|-- CMakeLists.txt
`-- CMakePresets.json
```

Generated build trees, runtime deployments, IDE state, and exported CAD files are excluded by
`.gitignore`.

## Building on Windows 11

### Prerequisites

The checked-in preset currently targets:

- Windows 11 x64;
- Visual Studio 2026 with the **Desktop development with C++** workload;
- CMake 3.24 or newer;
- Qt 6.11.1 for MSVC 2022 x64 at `C:\Qt\6.11.1\msvc2022_64`; and
- OpenCascade built and installed under the sibling `third_party` folder described below.

The expected directory arrangement is:

```text
projects/
|-- DesignRC/
`-- third_party/
    `-- occt/
        `-- install-debug/
```

`CMakeLists.txt` automatically looks for OpenCascade's package configuration at:

```text
../third_party/occt/install-debug/cmake/OpenCASCADEConfig.cmake
```

The Windows post-build step also copies the OpenCascade runtime DLLs and FreeType from the OCCT
tree. If Qt or OpenCascade is installed elsewhere, update `CMAKE_PREFIX_PATH`, `OpenCASCADE_DIR`,
and the OCCT runtime paths in `CMakeLists.txt` or provide equivalent cache values.

### Configure and build

Open a PowerShell terminal with the Visual Studio C++ environment available, change to the
DesignRC source directory, and run:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
```

The Debug application and its deployed dependencies are written to:

```text
build/debug/Debug/designrc.exe
```

The build also installs the user help document at:

```text
build/debug/Debug/help/index.html
```

### Run the regression tests

Run the complete suite with:

```powershell
ctest --preset windows-debug
```

The suite contains:

- `designrc_domain_tests` - airfoil, structure, naming, DXF, and SVG behavior;
- `designrc_gui_tests` - defaults, parameter controls, plans, and PDF generation; and
- `designrc_geometry_tests` - OpenCascade solid construction and boolean operations.

The geometry suite is substantially slower than the domain and GUI suites. To run one suite only:

```powershell
ctest --test-dir build/debug -C Debug -R designrc_geometry_tests --output-on-failure
```

### Run the application

```powershell
.\build\debug\Debug\designrc.exe
```

The application starts maximized. A new project intentionally leaves the 3D viewport blank until
**Generate Wing** is pressed.

### Build the Windows installer

An installer requires the optimized OCCT libraries at `../third_party/occt/install-release` and
Inno Setup 6. The OCCT Release installation must be configured with both
`BUILD_MODULE_ApplicationFramework=ON` and `BUILD_MODULE_DataExchange=ON` because DesignRC's STEP
assembly exporter uses XCAF and STEP data-exchange support.

Install the installer compiler once with:

```powershell
winget install --id JRSoftware.InnoSetup -e
```

Then build the Release application and installer with:

```powershell
.\installer\build-installer.ps1
```

The packaging script copies Microsoft's redistributable Visual C++ runtime DLLs beside the
application, creates a corresponding-source archive for GPL compliance, and writes the installer
to `dist`. The resulting installer does not require administrator privileges.

## Building on Ubuntu 24.04

These instructions are for Ubuntu 24.04 LTS x86-64, the only Linux environment currently tested.
They work in a native Ubuntu installation or in WSL 2. WSL users need WSLg to run the graphical
application.

### Build dependencies

Install the compiler, CMake, Ninja, Qt development files, Debian packaging tools, and the system
development libraries needed to build OCCT:

```bash
sudo apt update
sudo apt install \
  build-essential cmake ninja-build \
  qt6-base-dev qt6-base-dev-tools \
  libgl-dev libglu1-mesa-dev \
  libx11-dev libxext-dev libxmu-dev libxi-dev \
  libfreetype-dev libfontconfig1-dev \
  dpkg-dev fakeroot gzip
```

DesignRC requires Qt 6.4 or newer and OCCT 8.0. Ubuntu 24.04 supplies Qt 6.4.2. OCCT is built from
source because the project requires OCCT 8.0 and uses separate Debug and Release installations.
Place the OCCT 8.0 source tree beside DesignRC as follows:

```text
projects/
|-- DesignRC/
`-- third_party/
    `-- occt/
        `-- OCCT-8_0_0/
```

From the DesignRC source directory, build and install the OCCT Debug libraries:

```bash
occt_source="$(realpath ../third_party/occt/OCCT-8_0_0)"
occt_debug_prefix="$(realpath -m ../third_party/occt/install-linux-debug)"

cmake -S "$occt_source" -B "$HOME/build/designrc-occt-debug" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_INSTALL_PREFIX="$occt_debug_prefix" \
  -DINSTALL_DIR_LAYOUT=Unix -DINSTALL_DIR_CMAKE=cmake \
  -DBUILD_MODULE_ApplicationFramework=ON \
  -DBUILD_MODULE_DataExchange=ON -DBUILD_MODULE_Draw=OFF \
  -DUSE_FREETYPE=ON -DUSE_OPENGL=ON -DUSE_XLIB=ON \
  -DUSE_FFMPEG=OFF -DUSE_FREEIMAGE=OFF -DUSE_TBB=OFF -DUSE_VTK=OFF
cmake --build "$HOME/build/designrc-occt-debug"
cmake --install "$HOME/build/designrc-occt-debug"
```

### Configure, build, and test DesignRC

The `linux-debug` preset uses the OCCT Debug installation created above:

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

Run the application through X11 or XWayland with:

```bash
./build/linux-debug/designrc
```

### Build the Debian/Ubuntu package

Build and install a separate optimized OCCT copy for the distributable package:

```bash
occt_source="$(realpath ../third_party/occt/OCCT-8_0_0)"
occt_release_prefix="$(realpath -m ../third_party/occt/install-linux-release)"

cmake -S "$occt_source" -B "$HOME/build/designrc-occt-release" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$occt_release_prefix" \
  -DINSTALL_DIR_LAYOUT=Unix -DINSTALL_DIR_CMAKE=cmake \
  -DBUILD_MODULE_ApplicationFramework=ON \
  -DBUILD_MODULE_DataExchange=ON -DBUILD_MODULE_Draw=OFF \
  -DUSE_FREETYPE=ON -DUSE_OPENGL=ON -DUSE_XLIB=ON \
  -DUSE_FFMPEG=OFF -DUSE_FREEIMAGE=OFF -DUSE_TBB=OFF -DUSE_VTK=OFF
cmake --build "$HOME/build/designrc-occt-release"
cmake --install "$HOME/build/designrc-occt-release"
```

Then build the DesignRC Release executable and Debian package:

```bash
sh packaging/linux/build-package.sh
```

The script stages the package on the native Linux filesystem so file permissions remain correct
when the source tree is hosted on a WSL `/mnt/c` mount. It writes the Ubuntu 24.04 x86-64 `.deb`,
corresponding source archive, and SHA-256 checksums to:

```text
dist/designrc_1.0_amd64.deb
dist/DesignRC-1.0-source.tar.gz
dist/DesignRC-1.0-Linux-x64.sha256
```

Install the locally built package with:

```bash
sudo apt install ./dist/designrc_1.0_amd64.deb
```

## Building on Fedora

These instructions are for Fedora 44 x86-64. Install the compiler, CMake, Ninja, Qt 6 and
OpenCascade development packages, RPM packaging tools, and graphics development libraries:

```bash
sudo dnf install \
  gcc-c++ cmake ninja-build rpm-build \
  qt6-qtbase-devel qt6-qttools-devel \
  opencascade-devel \
  libX11-devel libXext-devel libXi-devel \
  mesa-libGL-devel mesa-libGLU-devel \
  freetype-devel fontconfig-devel
```

### Configure, build, and test DesignRC

From the DesignRC source directory, configure a Debug build against Fedora's system OpenCascade,
then build and run the regression tests:

```bash
cmake -S . -B build/fedora-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DOpenCASCADE_DIR=/usr/lib64/cmake/opencascade
cmake --build build/fedora-debug
ctest --test-dir build/fedora-debug --output-on-failure
```

Run the Debug application with:

```bash
./build/fedora-debug/designrc
```

### Build and install the RPM package

Build the Fedora Release executable and RPM with the checked-in packaging script:

```bash
sh packaging/linux/build-rpm.sh
```

The script writes the x86-64 RPM, corresponding source archive, and SHA-256 checksums to:

```text
dist/designrc-1.0-1.x86_64.rpm
dist/DesignRC-1.0-source.tar.gz
dist/DesignRC-1.0-Linux-RPM-x64.sha256
```

Install the locally built package and start DesignRC with:

```bash
sudo dnf install ./dist/designrc-1.0-1.x86_64.rpm
designrc
```

## Main dependencies

- [Qt 6](https://www.qt.io/) provides the desktop interface, plan scene, and PDF writer.
- [Open CASCADE Technology](https://dev.opencascade.org/) provides solid modeling, boolean
  operations, meshing, and 3D visualization.

DXF, SVG, and PDF part export are written directly from DesignRC's two-dimensional manufacturing
geometry and do not require an additional export library.

## License

DesignRC is Copyright (C) 2026 Barry Foust and is licensed under the **GNU General Public License
version 3 only** (`GPL-3.0-only`). See [LICENSE](LICENSE) for the complete terms. DesignRC comes
with absolutely no warranty.

DesignRC dynamically links Qt 6, Open CASCADE Technology, and FreeType. Their licenses, attribution,
exact version source locations, and redistribution notes are documented in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Complete dependency license texts are stored in
`resources/licenses` and copied into the application's `licenses` directory during every Windows
build. Matching Qt SPDX software bills of materials and attribution pages are also copied from the
installed Qt distribution.

When distributing an installer, publish the corresponding DesignRC source archive and build scripts
beside that installer. Users must be allowed to replace the LGPL-covered DLLs with compatible
modified versions and to reverse engineer the application when needed to debug those modifications.
