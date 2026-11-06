# DesignRC

DesignRC is a parametric desktop application for designing built-up RC airplane wings. It creates
manufacturing geometry and a complete mirrored-wing preview from a half-wing definition. The
source version is **1.9.0**, dated **November 6, 2026**.

> **Platform targets:** Windows 11 x64, Ubuntu 24.04/26.04 x86-64, and Fedora 44 x86-64.

## What DesignRC does

- Designs one or more connected half-wing panels and mirrors them into a complete wing.
- Imports root and tip airfoils from Selig-style `.dat` coordinate files.
- Imports dimensioned SVG/DXF leading- and trailing-edge curves per panel, preserving chords and normalizing span.
- Interpolates airfoil profiles, chord, sweep, twist, and rib positions across each panel.
- Adds rib build tabs for assembling wing panels on a flat surface, with or without twist.
- Adds independent top and bottom rib caps, recessed into clear rib sections and trimmed around existing structure.
- Generates solid ribs, spars, shear webs, sheeting, leading and trailing edges, turbulators,
  solid or built-up ailerons and flaps, hinge posts or beveled hinge spars, and wing joiners.
- Displays the assembled wing in an interactive OpenCascade 3D viewport.
- Produces a flattened, annotated, full-scale technical plan of both wing halves.
- Exports full-scale plans as custom-page vector PDF, colored DXF, or colored SVG.
- Exports selected laser-cut parts as full-scale DXF, SVG, and/or PDF files.
- Exports the material-colored 3D assembly as STEP.
- Saves editable projects in the `.designrc` format.
- Supports global millimeter or inch display units and per-parameter unit overrides. Inch spinner
  values advance in 1/32-inch increments and display exact 1/32-inch values as fractions.

The application contains detailed installed HTML help. After building, select **Help > Help** or
press **F1**.

## What's new in 1.9.0 (November 6, 2026)

- **Sheeting boundaries:** Front/Rear Top/Bottom sheeting each offer mutually exclusive
  **Stop Chord Location**, **Up To Spar**, and **Mid Spar** radio buttons. The spar modes
  use the first spar from the LE on the matching Top/Bottom side. Mid Spar means the
  center of that spar's width.
- **Cover Ribs:** recesses covered spars so sheeting lies above them. Up To Spar covers
  the full selected spar; Mid Spar retains its centerline boundary. Straight stock moves
  as one member, including beyond the sheet's Stop Rib.
- **Rib caps and sheeting:** caps fill exposed areas and butt against the sheet edge,
  including the uncovered half of a recessed spar at Mid Spar. Clearance accounts for
  cap width, airfoil slope, and the actual wood or tube cross-section.
- **Spar direction:** newly added spars default to **Perpendicular to Ribs**, mutually
  exclusive with **Parallel to**. The Tip Chord Location title and field appear only
  under Parallel to with **Manual (Tip Chord Location)** selected. Switching back to
  Manual restores its previous value; saved projects retain their chosen direction.
- **Straight joiners:** joiners stay straight across mirrored wing halves and adjoining
  panels. Plywood joiners stay in one flat chordwise plane instead of following a swept
  chord-percentage line. Parallel to and the Generate Wing warning use the same direction,
  avoiding false warnings for perpendicular spars at the joiner's root location.
- **Metric install defaults:** both wood spars and their joiner now start at 35% root
  chord to clear the swept tip. Inch install defaults retain 25%; existing projects and
  saved User Defaults retain their settings.
- Added GUI, sheeting/rib-cap geometry, and mirrored/cross-panel joiner regressions.

## Earlier: 1.8.0 (November 5, 2026)

- **Sheet Entire Panel:** one press enables all four sheeting options and sets their stop ribs
  to the panel's last rib. Press again after changing the rib count to apply the new last rib.
- **Parallel to:** align a spar with other spanwise geometry in its current panel. The spar
  stays straight, curved references use their end-to-end direction, and Top/Bottom/Mid
  placement is retained. The calculated Tip Chord Location is read-only; Manual restores
  its previous editable value. References are saved and checked for missing or circular links.
- **Spar/joiner alignment:** Generate Wing shows one Continue/Cancel warning when matching
  root chord locations have different plan-view directions. New-install defaults link both
  wood spars parallel to the joiner.
- **Rear-spar collision recovery:** linked spars can be relocated ahead of TE, aileron, or
  flap geometry. Relocation switches the affected spar to Manual; rejected moves restore
  its original reference and locations.
- **Spoiler placement:** manual Start Chord Location allows spoilers between front and rear
  spars. Immediately Behind Spar now appears below that field and disables it when checked.
  A dropdown beside the checkbox selects among multiple top spars, and the choice is saved.
  Selected-spar clearance accounts for tilted ribs and rail ends at the rib faces.
- Updated help and focused GUI and geometry regression coverage.

## Earlier: 1.7.1 (November 4, 2026)

- Ailerons and flaps support **Perpendicular to Ribs** with a **Root Width** field.
  Their skins extend from the hinge post/spar to the TE without a separate width.
- New-project hinge spars default to **3/8-inch Width**, **1/4-inch Height**, and **35 degrees**.
- Sleeve joiners expose matching OD/ID defaults for both panels, with ID initially
  1 mm below OD. Root sleeves retain both diameters when mirrored.
- Rear-spar clearance corrections follow the intersecting geometry's front edge when
  possible and are rejected if they introduce another collision.
- Global-unit changes round defaults to practical millimeter or fractional-inch values;
  closing Defaults updates project fields displayed in different units.
- Geometry generation reports detailed aileron/flap stages before panel completion.

## What's new in 1.7.0 (November 3, 2026)

- **Built-up ailerons and flaps:** independently enable separate control ribs, top/bottom
  sheeting, and additional wood or CF tube/rod/strip spars. Control spars use Top or Bottom
  placement, with root/tip locations measured within the control chord. The Ailerons/Flaps
  page has a divider between the two controls, and all new settings are available in
  **Edit > Defaults** and saved in projects.
- **Hinge construction:** choose **Hinge Post** for matching fixed and moving posts, or
  **Top/Bottom Spar** for four wood hinge spars with shared Width, Height, and Hinge Angle.
  Angle is measured aft from vertical, leaving the top of the control nearest the wing.
  Ribs and moving spars share the bevel; spars finish at the outside moving end-rib faces.
  Only the selected construction's parameter fields appear.
- **Control sheeting:** selected skins recess the ribs and butt against inherited wing TE
  sheeting or stock without changing the wing settings. In hinge-spar mode, skins reach
  their respective hinge posts or spars automatically and extend to the TE, with no
  skin Width parameter. Manual control-skin taper fields are removed; thickness at a
  shared thin TE is resolved automatically. Wing and control sheets stop on both sides
  of hinge and spanwise gaps, including shared flap/aileron joints.
- **Manufacturing geometry:** control spars fit changing airfoils, curves, and twist.
  Exported ribs include the TE and control-skin recesses and exact bevel/notch edges,
  without the extra backtracking line at the hinge. An LE or hinge-post height equal
  to its rib cut is accepted; undersized posts report the required minimum.
- **Consistent names and plans:** aileron/flap ribs use **ARn/FRn**, and their stock TE
  parts use **ATE1, ATE2, ... / FTE1, FTE2, ...** throughout plans, rib drawings, the
  parts dialog, exported labels/files, and STEP assemblies. Leaders identify stock,
  spars, and sheeting dimensions/thickness. Shared rib labels are separated, false
  full-chord boundary lines are removed, and moving posts reach their end gaps.
- **Joiner span:** **Number of Rib Bays** controls all fixed joiner materials and removable
  sleeves/rods. One bay crosses two ribs; optional R1a counts as a physical rib. Both
  adjoining panels must have enough bays. Wood joiners cut every traversed rib and
  remove intersecting shear webs. Alignment pins retain their existing lengths.
- **Joiner clearance:** geometry checks reject joiners that leave the crossed rib envelope,
  including the rib faces for circular sections. The error identifies the joiner and rib
  and requests a shorter joiner or less dihedral. Deleting all spars now leaves the wing
  without spars, including when reopening files with stale legacy spar flags.
- **Plan formats:** **Export Plan** replaces **Export Plan PDF**. Choose PDF, DXF, or SVG,
  click OK, then choose a destination and filename. DXF retains line/label RGB colors,
  outlines, leaders, and borders in millimeters, using complete AutoCAD 2004 class records
  for Autodesk import compatibility. SVG retains line/fill colors and transparency.
  Both use outlined labels; all three formats preserve the 1:1 drawing layout. Fusion
  uses its own sketch-status colors when importing geometry, even from a colored file.

## What's new in 1.6.0 (October 5, 2026)

- **Imported LE/TE curves:** each panel's LE/TE tab can import exactly two open paths
  from a dimensioned SVG or ASCII DXF. The filename appears below **Import LE/TE Curves**;
  **Delete LE/TE Curves** restores the straight planform controls.
- Files must declare millimeters or inches. X is chord and positive Y is span; axes are
  swapped automatically when the drawing is longer along X. Exactly one root must be
  within 1/4 inch (6.35 mm) of the origin to identify the LE. Both roots must be within
  that tolerance of Y=0, and each path's span must be within it of the Specs tab's Panel
  Span. DXF curves must have Z=0. Standalone DXF POINT markers are ignored.
- Import aligns the roots and scales only the span, preserving the imported chords.
  Root and tip chords become read-only; Tip Sweep is disabled and displays `--` because
  the curves define sweep. Invalid units, path counts, crossings, and backward span
  segments produce an explanatory error.
- Curved stock uses smooth 3D guide sweeps with changing airfoil shape and panel twist,
  including the upright center-root transition. Straight geometry uses one root-to-tip
  loft when it reproduces the profiles. A bounded smooth-loft fallback handles cases
  where a valid sweep cannot be formed. CF Tube LE selection warns about bending the
  chosen stock, and curved assemblies receive additional clearance checks.
- **Multi-panel chord matching:** conventional outer panels inherit the inner tip chord.
  Imported outer roots within 1/4 inch (6.35 mm) are matched by a TE correction that fades
  to zero at the tip, preserving the LE and tip chord. Larger mismatches or corrections
  that cross the curves stop generation. Either or both panels may use imported curves.
- Ailerons and flaps retain straight hinges with curved TEs. **Hinge Parallel with Y Axis**
  uses Width as the maximum chordwise width; otherwise Width sets the TE-to-hinge distance
  at both ends, so the interior may be wider.
- Projects embed the curve geometry and filename, including joint corrections. Reopening
  restores the curves and filename without needing or rereading the original SVG/DXF.
  Imports, deletions, and edits retain the existing Generate Wing workflow.

## What's new in 1.5.0 (October 3, 2026)

- **Molded LE Sheet** wraps the airfoil nose with recessed curved sheeting. Width is the
  total wrap distance, split equally above and below the nose (default 5/8 inch), with
  1/16-inch default Thickness.
- **Notched LE** adds a centered, rounded stick and matching rib notch, defaulting to
  3/8-inch Width and 3/16-inch Height. Both new LE types include unit-aware fields,
  project persistence, Defaults, and Help.

- **Diamond LE (Square Balsa Rotated 45 Degrees)** adds square balsa leading-edge stock
  and a straight V notch in each rib. The exposed nose follows the interpolated airfoil.
  Undersized Width values increase automatically to fit every rib in the panel; the corrected
  unit-aware Width is saved in the project. Defaults also includes the option.
- Build-tab/rib-cap overlaps now produce construction warnings instead of stopping generation.
  Temporary tabs pass through unchanged cap solids, including in STEP exports.

- Four independent rib-tab choices: Bottom Rear, Bottom Front, Top Rear, and Top Front.
  Each has its own Location; front defaults to 15% and rear to 75% chord. Width,
  work-table clearance, and separation-line settings are shared.
- Top tabs support upside-down construction using an upper support plane without changing
  the wing's twist. Top tabs avoid top spars and spoilers.
- **Add Tabs to Ribs With Sheeting** retains temporary tabs where sheeting will be installed
  after construction. Tabs intentionally overlap intact sheeting in the preview and STEP;
  no sheeting slots are cut. Remove tabs before finishing the wing.
- All new options are available in **Edit > Defaults**. Older projects keep their bottom
  tabs, with top tabs and the sheeting override disabled.

## What's new in 1.4.0

- Separate **Add Front Build Tab** and **Add Rear Build Tab** options below a divider in Ribs.
  Each has an editable Location, defaulting to 15% and 75% chord.
- One shared **Tab Width** field controls both tabs, defaulting to the original 3/16 inch
  (4.7625 mm). **Height Above Work Table** defaults to zero and extends every enabled tab.
  Both fields use global units and accept explicit inch/mm entry.
- Tabs still relocate around spars. A collision with bottom sheeting now omits only the
  affected tab on that rib instead of stopping generation.
- Optional **Add Tab/Rib Separation Line** follows the original airfoil contour through actual
  tabs in 2D exports. Open lines have 0.25 mm end gaps (reduced for very narrow tabs), a separate
  `TAB_RIB_SEPARATION` DXF layer, and blue SVG/PDF strokes for separate laser settings.
  These markings do not change the 3D solid.
- All tab settings are available in **Edit > Defaults** and saved in projects. Older files
  retain both tabs at 15%/75%, the original width, zero extra height, and separation lines off.
- Help and About identify version 1.4.0 dated October 2, 2026; About has a shorter description.

## What's new in 1.3.0

**Rib Caps** is a new panel tab, also available in **Edit > Defaults**.

- Independent **Top Rib Caps** and **Bottom Rib Caps** start unchecked. Each reveals Thickness
  and Width fields when enabled, supporting global units and explicit inch/mm entry.
- Default thickness is 1/16 inch (1.5875 mm); default width is 1/4 inch (6.35 mm).
- Caps follow every clear full-rib section, with matching recesses in the rib. They avoid
  spars, sheeting, spoilers, and other occupied geometry. Riblets receive neither caps nor recesses.
- Caps stop at outer panel end-rib faces, including angled multi-panel joints. Bottom caps that
  overlap build tabs produce a named geometry collision instead of leaving gaps around the tabs.
- Cap collision checks run across ribs in parallel within the thread limit. Status messages
  identify the rib number and whether the Top or Bottom cap is being checked.
- Straight spars now terminate at the outer root and tip rib faces, including angled joints.

**Add Build Tabs**, on the **Ribs** tab and in **Defaults**, now works with or without
**Specs > Tip Twist**. It starts unchecked, and changing Tip Twist preserves the selection.

- Adds two 3/16-inch-wide supports to each full rib, normally at 15% and 75% chord, for building
  the panel with its intended twist on a flat surface.
- Takes the build-plane slope from the panel's untwisted root and tip undersides, then checks
  twisted ribs and their full material thickness for clearance. Positive twist raises the
  trailing edge; negative twist raises the leading edge.
- Checks finished tabbed outlines and solids against the build plane, removing protruding
  lips. Tabbed manufacturing contours use a 0.005 mm curve approximation so spline smoothing
  cannot recreate a protrusion in an export.
- Moves tabs clear of spar notches and, where possible, front sheeting or trailing-edge stock.
  Unresolvable collisions stop generation with an error. Tabs appear in rib DXF/SVG/PDF exports
  and STEP solids.
- The support plane remains airfoil-based, not spar-based. Twisted rib bodies can have clearance
  above the tab-foot line; the check prevents protrusions below it rather than making every
  underside tangent to the building surface.

**Usability improvements**

- Cancel interrupts lightening-hole layout, hole cutting, and rib meshing. Other individual
  geometry operations may finish before cancellation takes effect.
- The splash screen and main window open on the same display in multi-monitor setups.
- About points to the installed `licenses` folder for license texts and third-party notices.

## What's new in 1.2.0

- Introduced **Add Build Tabs** on the **Ribs** tab and in **Defaults** for twisted panels.

## What's new in 1.1.0

- Spars now have independent root and tip chord locations, producing a straight extrusion between
  those two points while retaining spar, sheeting, spoiler, and joiner collision checks.
- Front top and bottom sheeting can stop at a specified chord location or extend up to a spar.
  Covered spars are recessed so the sheeting follows the airfoil surface. Existing aft sheets are
  identified as Rear Top and Rear Bottom Sheeting.
- Independent Top and Bottom TE Sheeting uses a constant panel width, configurable thickness, and
  optional taper start.
- Spoilers can be placed immediately behind the top spar. Collision reporting now identifies the
  actual spoiler part and conflicting spar or trailing-edge component.
- Fixed and removable joiners have improved dihedral alignment, balanced rib openings, and
  wood-to-wood and removable-joiner collision detection. Sleeve/rod joiner rods extend through the
  adjoining sleeve as they do in the assembled wing.
- Percent controls accept manually entered values to two decimal places while their arrows retain
  one-percent increments. LE/TE stock height errors restore the minimum valid cut-face height.

## Published downloads (1.8.0)

The source version described above is 1.9.0. Published installers, source archives, and checksums are available from the
[DesignRC 1.8.0 release](https://github.com/barryfx/DesignRC/releases/tag/1.8.0):

- `DesignRC-1.8.0-Windows-x64-Setup.exe` for Windows 11 x64. Download and run the installer. Because
  it is not code-signed, Windows may display a warning before allowing it to run.
- `designrc_1.8.0-1_amd64.deb` for Ubuntu 24.04 and 26.04 x86-64. From the download directory, install and run it
  with:

  ```bash
  sudo apt install ./designrc_1.8.0-1_amd64.deb
  designrc
  ```

  APT installs the required distribution runtime libraries automatically. Qt 6.4.2, OCCT 8.0,
  and ICU 74 are included. This build requires `libc6 >= 2.38`. Use its matching
  `DesignRC-1.8.0-1-source.tar.gz` source archive and `DesignRC-1.8.0-1-Linux-x64.sha256`
  checksum manifest.
  On WSL, install `wslu` and `xdg-utils` if **Help > Help** should open in the Windows default browser:

  ```bash
  sudo apt install wslu xdg-utils
  ```

- `designrc-1.8.0-1.x86_64.rpm` for Fedora 44 x86-64. From the download directory, install and run it
  with:

  ```bash
  sudo dnf install ./designrc-1.8.0-1.x86_64.rpm
  designrc
  ```

Release packaging also produces source archives and `DesignRC-1.8.0-SHA256SUMS.txt`. Use the checksum file
to verify a download before installing it.

## Typical use

1. Select the number of wing panels.
2. Configure each panel on the **Specs**, **Ribs**, **Rib Caps**, **Spars**, **LE/TE**, **Ailerons/Flaps**, and **Joiner**
   tabs.
3. Import root and tip airfoil `.dat` files where required.
4. Press **Generate Wing** to validate the design and build the 3D geometry.
5. Inspect the wing with left-drag orbit, right-drag pan, and mouse-wheel zoom.
6. Press **Generate Plan** to create the flattened technical drawing.
7. Select **Export Plan**, choose PDF/DXF/SVG, and save the plan; use **Export Parts**
   for selected cutting parts in DXF/SVG/PDF.
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

The complete suite contains 21 tests:

- `designrc_domain_tests` - airfoil, structure, naming, DXF, and SVG behavior;
- `designrc_gui_tests` - defaults, parameter controls, plans, and PDF/DXF/SVG generation;
- `designrc_geometry_tests` - OpenCascade solid construction and boolean operations;
- `designrc_step_export_tests` - STEP assembly structure, names, and component grouping;
- `designrc_joiner_backend_tests` - straight mirrored/cross-panel joiners, placement, bay counts, and rib clearance;
- `designrc_spar_directions_gui_tests` - perpendicular/reference/manual modes and collision recovery;
- `designrc_spar_joiner_warning_gui_tests` - alignment warnings and straight plywood references;
- `designrc_sheeting_boundaries_gui_tests` - exclusive boundary controls, Cover Ribs, and saved defaults;
- `designrc_sheeting_boundaries_geometry_tests` - sheeting boundaries, rib-cap fitting, and recessed-stock clearance;
- `designrc_spoiler_placement_gui_tests` - manual placement and selected-spar controls;
- `designrc_spoiler_between_spars_tests` - spoiler solids between front and rear spars;
- `designrc_stock_fit_tests` - stock height fits, sheeting recesses, hinge-sheet coverage,
  and exported rib contours;
- `designrc_hinge_spar_tests` - beveled hinge ribs/spars, sheeting, curves, and end-face alignment;
- `designrc_control_direction_tests` - perpendicular control hinges, root widths, and solid construction;
- `designrc_builtup_control_tests` - built-up control solids and spar fitting;
- `designrc_planform_tests` - curve import, normalization, hinges, and exports;
- `designrc_sweep_tests` and `designrc_sweep_assembly_tests` - curved solid sweeps and assemblies;
- `designrc_curved_project_tests` - the saved curved-wing fixture;
- `designrc_panel_chord_tests` - adjoining panel chord matching; and
- `designrc_cancellation_backend_tests` - cancellation of geometry generation.

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

The script uses `build/release`, copies Microsoft's redistributable Visual C++ runtime DLLs beside
the application, creates a corresponding-source archive for GPL compliance, and writes the
installer to `dist`. Package versions are read from `CMakeLists.txt`; versions use three numeric components. The resulting installer does not require administrator
privileges.

## Building on Ubuntu 24.04

These source-build instructions use Ubuntu 24.04 LTS x86-64 dependency versions. Native Ubuntu and WSL 2 are
supported build environments; WSL users need WSLg to run the graphical application.

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
  dpkg-dev fakeroot gzip file
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

The script uses `build/linux-release` and stages the package on the native Linux filesystem so file
permissions remain correct when the source tree is hosted on a WSL `/mnt/c` mount. It writes the
x86-64 `.deb` targeting the build host's Ubuntu version, corresponding source archive, and
SHA-256 checksums to:

```text
dist/designrc_1.9.0-1_amd64.deb
dist/DesignRC-1.9.0-1-source.tar.gz
dist/DesignRC-1.9.0-1-Linux-x64.sha256
```

Install the locally built package with:

```bash
sudo apt install ./dist/designrc_1.9.0-1_amd64.deb
```

### Reproducible Ubuntu 24.04 compatibility build

To build a DEB for Ubuntu 24.04 without changing the host OS, use the dedicated Docker builder.
It installs Ubuntu 24.04's Qt/toolchain, rebuilds OCCT from source in a separate Linux volume,
builds DesignRC in Release with assertions enabled in its tests, runs all regression suites,
and packages the application with the matching Qt/ICU/OCCT libraries. It never uses the host's
compiled libraries. Changing only the DEB's dependency metadata cannot provide compatibility.

With Docker available, run the following from the project directory in a Linux/WSL shell.
The builder downloads the official OCCT `V8_0_0` source archive into the Linux volume and records
its SHA-256 digest:

```bash
mkdir -p dist/ubuntu-24.04
docker build -t designrc-ubuntu2404-builder -f packaging/linux/ubuntu-24.04.Dockerfile .
docker run --rm \
  --mount "type=bind,source=$PWD,target=/workspace,readonly" \
  --mount type=volume,source=designrc-ubuntu2404-build,target=/build \
  --mount "type=bind,source=$PWD/dist/ubuntu-24.04,target=/output" \
  -e BUILD_JOBS=8 designrc-ubuntu2404-builder
```

The separate `dist/ubuntu-24.04` output contains the DEB, matching source archive, checksums,
test results, dependency metadata, and exact build-package inventory. Existing release artifacts
are preserved. Set `DEBIAN_PACKAGE_RELEASE` in the container to override the default packaging
revision of `1`; the application version remains unchanged. Use a fresh named volume when
changing the OCCT source version. Validate the DEB
in clean Ubuntu 24.04 and 26.04 environments before replacing a published download.

For each target version, create a fresh disposable test container with only the project mounted
read-only. The script installs the DEB and its declared dependencies, checks the installed files
and libraries, runs non-root generation regressions and an XCB GUI smoke test under Xvfb, then
uninstalls the package. For example:

```bash
docker run --rm \
  --mount "type=bind,source=$PWD,target=/workspace,readonly" \
  ubuntu:24.04 bash /workspace/packaging/linux/test-ubuntu-package.sh \
  /workspace/dist/ubuntu-24.04/designrc_1.9.0-1_amd64.deb
# Repeat with ubuntu:26.04.
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

The script uses `build/fedora-release` and writes the x86-64 RPM, corresponding source archive, and
SHA-256 checksums to:

```text
dist/designrc-1.9.0-1.x86_64.rpm
dist/DesignRC-1.9.0-source.tar.gz
dist/DesignRC-1.9.0-Linux-RPM-x64.sha256
```

Install the locally built package and start DesignRC with:

```bash
sudo dnf install ./dist/designrc-1.9.0-1.x86_64.rpm
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
