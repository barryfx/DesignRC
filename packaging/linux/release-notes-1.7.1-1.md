## Ubuntu package revision 1.7.1-1 — November 4, 2026

This packaging-only update makes DesignRC 1.7.1 installable on Ubuntu 24.04 as well
as Ubuntu 26.04. DesignRC and OCCT 8.0 were rebuilt on Ubuntu 24.04, using Qt 6.4.2
and ICU 74. The DEB now requires `libc6 >= 2.38`, rather than `>= 2.43`.

The application and About dialog remain version **1.7.1**. The Debian package
version is **1.7.1-1**, which sorts newer than the original 1.7.1 package.
Windows and Fedora RPM installers are unchanged.

Use `designrc_1.7.1-1_amd64.deb` with its matching
`DesignRC-1.7.1-1-source.tar.gz` archive and `DesignRC-1.7.1-1-Linux-x64.sha256`
manifest. These distinct filenames preserve the original release artifacts.

The compatibility build passed all 15 Release regression suites. Installation,
non-root generation, GUI startup, and uninstallation were verified in clean
Ubuntu 24.04 and 26.04 containers. The Qt 6.4 file-picker test driver was made
deterministic and checked on Windows Qt 6.11.1 as well.
