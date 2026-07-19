# DesignRC

DesignRC is a parametric desktop application for designing built-up RC airplane wings. It creates
manufacturing geometry and a complete mirrored-wing preview from a half-wing definition. The
current release is **0.9.0**.

> **Platform status:** DesignRC has only been built and tested on Windows 11.

## What DesignRC does

- Designs one or more connected half-wing panels and mirrors them into a complete wing.
- Imports root and tip airfoils from Selig-style `.dat` coordinate files.
- Interpolates airfoil profiles, chord, sweep, twist, and rib positions across each panel.
- Generates solid ribs, spars, shear webs, sheeting, leading and trailing edges, turbulators,
  ailerons, flaps, hinge posts, and wing joiners.
- Displays the assembled wing in an interactive OpenCascade 3D viewport.
- Produces a flattened, annotated, full-scale technical plan of both wing halves.
- Exports the plan as a custom-page vector PDF at 1:1 scale.
- Exports selected laser-cut parts as full-scale DXF and/or SVG files.
- Saves editable projects in the `.designrc` format.
- Supports global millimeter or inch display units and per-parameter unit overrides. Inch spinner
  values advance in 1/32-inch increments and display exact 1/32-inch values as fractions.

The application contains detailed installed HTML help. After building, select **Help > Help** or
press **F1**.

## Typical use

1. Select the number of wing panels.
2. Configure each panel on the **Specs**, **Spars**, **LE/TE**, **Ailerons/Flaps**, and **Joiner**
   tabs.
3. Import root and tip airfoil `.dat` files where required.
4. Press **Update View** to validate the design and build the 3D geometry.
5. Inspect the wing with left-drag orbit, right-drag pan, and mouse-wheel zoom.
6. Press **Generate Plan** to create the flattened technical drawing.
7. Export the plan to PDF or export selected cutting parts to DXF/SVG.
8. Save the editable design as a `.designrc` project.

Parameter edits do not automatically rebuild geometry. This allows several values to be changed
before running the potentially expensive **Update View** operation.

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
**Update View** is pressed.

## Main dependencies

- [Qt 6](https://www.qt.io/) provides the desktop interface, plan scene, and PDF writer.
- [Open CASCADE Technology](https://dev.opencascade.org/) provides solid modeling, boolean
  operations, meshing, and 3D visualization.

DXF and SVG part export are written directly from DesignRC's two-dimensional manufacturing
geometry and do not require an additional export library.
