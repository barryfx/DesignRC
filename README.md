# DesignRC

DesignRC is a focused desktop application for wizard-driven RC airplane design. It will generate
manufacturable two-dimensional parts, assemble a three-dimensional preview, and export DXF cutting
files.

This initial vertical slice contains:

- a Qt 6 desktop shell;
- an OpenCascade viewport adapted from the reusable FormForgeCAD viewer work;
- kernel-independent airfoil `.dat` parsing, normalization, resampling, and interpolation;
- a half-span tapered wing assembled from morphing, extruded ribs;
- live span, area, aspect-ratio, and taper metrics;
- linear root-to-tip rib twist control;
- selective per-rib DXF export in millimetres; and
- unit tests for the airfoil domain code.

The wing editor supports multiple parameter panels, vertical Specs/Spars/LE-TE/Ailerons-Flaps
sections, DesignRC project files, separate persistent metric and inch defaults, and global
millimetre/inch display units with per-length overrides. Inch dimensions use fractional-inch entry
stops while geometry and saved numeric values remain internally normalized to millimetres.

The design engine owns 2D outlines. OpenCascade consumes those outlines only to construct the 3D
preview, so future DXF output can use the exact same manufacturing geometry.

## Build on Windows

Requirements used by the checked-in preset:

- Visual Studio 2026 C++ tools;
- Qt 6.11.1 at `C:/Qt/6.11.1/msvc2022_64`; and
- OpenCascade installed at `../third_party/occt/install-debug`.

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

The application is produced at `build/debug/Debug/designrc.exe` with its Qt and OCCT runtime DLLs
deployed beside it.
