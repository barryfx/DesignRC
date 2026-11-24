# DesignRC 1.9.1 - November 23, 2026

- Wiring Holes and its parameters now appear only on Ribs, below Lightening Holes.
- Removed the duplicate wiring controls from Spoilers and Ailerons/Flaps.
- Edit > Defaults uses the same Ribs-only controls for every panel.
- Older projects and saved defaults preserve enabled/disabled wiring-hole states,
  rib ranges, chord locations, dimensions, and units. Hole generation is unchanged.
- Focused GUI tests cover project/default editors, both unit systems, root/outer
  panels, parameter edits, saved settings, and an older three-panel project.

Packages use the 1.9.1 application version (Linux packaging revision 1). Build and
installer validation results accompany the locally generated release artifacts.

## Portable Linux AppImage added November 24, 2026

- `DesignRC-1.9.1-x86_64.AppImage` runs without installing a DesignRC package.
- Uses the tested Ubuntu 24.04 release executable with Qt 6.4.2, OCCT 8.0,
  ICU 74, and additional runtime libraries bundled. Application version and
  release date remain 1.9.1 / November 23, 2026.
- Passed library, resource, relocation, backend regression, curved-wing
  generation, and XCB GUI startup checks in fresh Ubuntu 24.04 and Fedora 44
  containers using an unprivileged user.
- Container tests used the AppImage runtime's extract-and-run mode. FUSE mounting
  and physical GPU rendering were not tested.

```bash
chmod +x DesignRC-1.9.1-x86_64.AppImage
./DesignRC-1.9.1-x86_64.AppImage
```

If FUSE is unavailable, add `--appimage-extract-and-run`. The AppImage checksum,
file/tool inventories, and validation reports are included with the release.
