# Linux AppImage

`DesignRC-1.9.1-x86_64.AppImage` is a portable alternative to the DEB and RPM.
Download it from the [DesignRC 1.9.1 release](https://github.com/barryfx/DesignRC/releases/tag/1.9.1).
It contains the tested Ubuntu 24.04 release executable, Qt 6.4.2, OCCT 8.0,
ICU 74, and additional runtime libraries. It uses the host's graphics drivers,
display server, fonts, and glibc. The supported test targets are Ubuntu 24.04
and Fedora 44 x86-64 desktop systems, including Wayland through XWayland.

Make the downloaded file executable and run it as your regular user:

```bash
chmod +x DesignRC-1.9.1-x86_64.AppImage
./DesignRC-1.9.1-x86_64.AppImage
```

If FUSE mounting is unavailable, the runtime can extract and run automatically:

```bash
./DesignRC-1.9.1-x86_64.AppImage --appimage-extract-and-run
```

There is no system package installation or uninstaller. Delete the AppImage to
remove the application; saved projects and user settings remain. Help and
license notices are inside the image. They can also be inspected by running
`./DesignRC-1.9.1-x86_64.AppImage --appimage-extract` and opening
`squashfs-root/usr/lib/designrc/help/index.html` or the adjacent `licenses` folder.
An extracted image can be moved and started using its `AppRun` file.

## Building

Use the Ubuntu 24.04 builder described in the repository README. First build and
test the release DEB using `build-ubuntu-24.04.sh`. Then, inside the same builder,
run:

```bash
bash /workspace/packaging/linux/build-appimage.sh \
  /output/designrc_1.9.1-1_amd64.deb /output
```

The packaging script does not rebuild or change the release source. It extracts
the DEB, adds a relative-path launcher, bundles dynamically loaded Qt XCB/GL
plugins, resolves library dependencies using linuxdeploy, includes distribution
copyright notices, and creates the image using appimagetool. Native Linux staging
under `/build` avoids slow Windows bind-mount IO. `/output` must be writable.
Build dependencies are the existing Ubuntu builder plus `curl` and CA certificates.

The official linuxdeploy, appimagetool, and AppImage runtime downloads are cached
in `/build/appimage-tools` (override with `APPIMAGE_TOOLS_DIR`). Their SHA256 hashes
are pinned in `appimage-tools.sha256`; a changed continuous-release download is
rejected until explicitly reviewed. Preserve the cached tools for later builds.

Outputs include the AppImage, its checksum, a file inventory, and tool checksums.
The AppImage is unsigned. Verify it with:

```bash
sha256sum -c DesignRC-1.9.1-AppImage.sha256
```

## Testing

Use fresh Ubuntu 24.04 and Fedora 44 containers with no DesignRC, Qt, or OCCT
installed. Install only the display/graphics test prerequisites:

```bash
# Ubuntu
apt-get update
apt-get install -y --no-install-recommends xvfb xauth x11-utils \
  libgl1 libegl1 libgl1-mesa-dri fonts-dejavu-core ca-certificates

# Fedora
dnf install -y xorg-x11-server-Xvfb xorg-x11-xauth xwininfo \
  mesa-libGL mesa-libEGL mesa-dri-drivers dejavu-sans-fonts
```

Mount the source and artifacts read-only at `/workspace`. Run as root; the script
creates an unprivileged application test user:

```bash
bash /workspace/packaging/linux/test-appimage.sh \
  /workspace/dist/DesignRC-1.9.1-x86_64.AppImage /workspace/tests/fixtures
```

The checks exercise the AppImage runtime's extract-and-run mode, backend panel
chord/joiner/cancellation regressions, curved-wing project generation, every ELF
library's dependencies, bundled help/licenses/OCCT shaders, relocation to a path
containing spaces, and an XCB GUI window under Xvfb. They do not exercise FUSE
mounting or a physical GPU; those require a desktop system outside the containers.
