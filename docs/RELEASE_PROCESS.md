# Release and Packaging Process

This document defines how RetroVDP Studio becomes a downloadable,
cross-platform product. It is the authoritative packaging and release contract;
[`../PROJECT_PLAN.md`](../PROJECT_PLAN.md) tracks program milestones without
duplicating this procedure.

## User-facing release promise

A release user must not need to install Qt, CMake, a C++ compiler, MinGW,
Xcode, an image library, or another development tool. Every package carries
the Qt libraries, QML modules, platform and image-format plugins, compiler
runtime, application resources, and notices required by that package.

“Standalone” means a self-contained user installation or portable package. It
does not require one literal application binary. The shared-library Qt model
is the release standard because this application uses Qt Quick, QML modules,
platform plugins, and image-format plugins. Static Qt linking is not a Phase 8
goal.

An operating system still provides its normal kernel, desktop/windowing
system, graphics driver, fonts, and other base facilities. Linux compatibility
is bounded by the oldest distribution and C library against which the release
is intentionally built and tested.

## Distribution channel

GitHub Releases is the canonical public distribution channel. A release is
created from an immutable version tag whose commit is contained in `main` and
contains release notes plus all approved binary assets. GitHub's automatically
generated source archives are not substitutes for the user packages.

Stores and package repositories—including Microsoft Store, Mac App Store,
Flathub, Homebrew, and distribution repositories—are optional later channels.
They must consume the same versioned source and pass the same tests; none is a
prerequisite for the first release.

GitHub Releases is also the source of update information, but it does not
update an installed application by itself. The first preview uses manual
updates. An in-application update check may notify the user and open the
appropriate GitHub Release, while automatic download and installation remains
deferred until package signing and update verification are established.

## Preview and beta release policy

The first packaging target, `v0.1.0-beta.1`, was published as a GitHub
prerelease. Tester feedback is delivered through corrective prereleases such
as `v0.1.0-beta.2`, and later builds use monotonically increasing identifiers
such as `v0.1.0-beta.3` and
`v0.1.0-rc.1`; the first stable build then uses the approved stable version.

The Debian packages in `v0.1.0-beta.1` and `v0.1.0-beta.2` are unsafe to
install. They copied a linuxdeploy/AppImage tree into the system root and could
install `/usr/bin/qt.conf`, `/usr/plugins`, `/usr/qml`, and bundled libraries in
global locations. A Kubuntu VM demonstrated the resulting cross-application
failure: SDDM remained active, but its Qt 5 greeter aborted after being
redirected to the application's Qt 6 plugin tree. `v0.1.0-beta.3` supersedes
those Debian assets with an isolated layout. This incident is a release-gate
regression case, not merely a release-note limitation.

Preview packages may be unsigned. This is acceptable for testing on Windows,
Linux, and macOS provided that the release notes:

- label every package as preview software rather than a normal production
  download;
- state exactly which Windows or macOS trust warning the tester should expect
  and how to authorize that specific download;
- identify Linux execution or installation steps without weakening system-wide
  security settings;
- disclose that unsigned packages cannot provide the same publisher identity
  or tamper assurance as the later signed release; and
- include SHA-256 checksums for every binary asset.

The preview must use the same intended application names, package identifiers,
settings locations, recipe format, and installation locations as the future
stable release. Adding Windows and Apple signatures later changes package
trust, not application identity. Preview tags and published assets remain
immutable; a correction receives a new prerelease tag.

## Update discovery and installation policy

The initial beta requires a manual download and install from GitHub Releases.
This keeps the first packaging exercise independent of an updater and allows
the packages themselves to be tested before more release infrastructure is
added.

KDE Discover is a repository/store client, not a generic replacement for the
local Debian-package installer. Publishing a `.deb` file on GitHub does not
make it discoverable in Discover. The current beta therefore keeps the
AppImage and isolated `.deb` as direct GitHub downloads, and documents
`sudo apt install ./<package>.deb` as the reliable Debian fallback when a
desktop authorization agent is unavailable, including some remote-desktop
sessions.

The Linux software-center roadmap is:

1. Add a validated AppStream MetaInfo file using the frozen
   `io.github.ciscogarciafl.RetroVDPStudio` application ID, and keep its
   desktop file, icon, screenshots, releases, summary, licenses, and URLs in
   sync. This metadata improves package presentation but does not by itself
   create a Discover repository.
2. Evaluate a separately tested Snap in a beta channel as the first
   Discover-visible preview channel. Discover can use a configured Snap
   backend, but the confined package has its own filesystem, portal, hardware,
   and update behavior and does not replace AppImage or Debian testing.
3. Target Flatpak/Flathub for the eventual stable cross-distribution channel
   after the application satisfies Flathub's source-build, sandbox, metadata,
   icon, desktop-file, and stable-release requirements. New beta-only Flathub
   submissions are not currently the preview path.
4. Consider a signed APT repository or PPA only when native Debian/Ubuntu
   repository updates justify its source-package, signing, AppStream-catalog,
   and maintenance overhead. Discover's native package backend still relies on
   PolicyKit, so this channel does not eliminate authorization failures in a
   session without a working authentication agent.

No software-center channel is advertised until its install, launch, update,
rollback or removal, sandbox/permission, and host-integrity checks pass on
clean supported systems. See [KDE Discover](https://userbase.kde.org/Discover/en),
[Flathub application requirements](https://docs.flathub.org/docs/for-app-authors/requirements),
and [Ubuntu source-package guidance](https://documentation.ubuntu.com/project/contributors/bug-fix/build-packages/).

The preferred next step is a notification-only `Help -> Check for Updates`
feature. It should:

1. compare the running application version with published GitHub Releases;
2. offer `Stable only`, `Preview and stable`, and `Do not check
   automatically` preferences;
3. ignore drafts in every channel and ignore prereleases in the stable channel;
4. show the newer version, release date, signing state, important notes, and
   supported platform asset;
5. require an explicit user action before opening the release page or starting
   any download; and
6. never replace files silently or claim an update succeeded before the newly
   installed application is launched and verified.

GitHub's
["latest release" endpoint](https://docs.github.com/en/rest/releases/releases#get-the-latest-release)
excludes prereleases, so the preview channel must enumerate releases and
select the highest compatible non-draft version. Version comparison must use
semantic-version precedence rather than lexical string order. Network or API
failure must be non-fatal and must not interrupt normal editing or conversion.

Full automatic installation is a later feature with separate platform work:

- a Windows NSIS package can perform an explicit user-approved upgrade; a
  future [MSIX App Installer](https://learn.microsoft.com/en-us/windows/msix/app-installer/auto-update-and-repair--overview)
  channel would have a different identity and update contract;
- a macOS updater requires signed update metadata and signed/notarized
  replacement bundles;
- an AppImage may later embed
  [compatible update information](https://docs.appimage.org/packaging-guide/optional/updates.html),
  while Debian packages should normally update through an authenticated
  package repository; and
- portable ZIP users continue to replace the extracted folder unless a safe,
  separately designed portable updater is introduced.

An unsigned beta must not silently install executable updates. A complete
automatic updater requires authenticated metadata, package signature or digest
verification, interrupted-update recovery, rollback behavior, and a tested
settings/recipe migration policy.

## Stable identity and beta-to-official upgrades

The following identities are frozen beginning with the first RetroVDP Studio
release, `v0.1.0-beta.4`:

| Identity | Frozen value and policy |
| --- | --- |
| Qt organization name | `CiscoGarciaFL`; keep it so existing per-user settings remain discoverable. |
| Qt application name | `RetroVDPStudio`; keep it so the settings key does not split between preview and stable builds. |
| Windows installer identity | `{9D53324F-AE3A-42FC-96C6-0B2256798410}`; never regenerate it for later versions. |
| Windows install path | `%LOCALAPPDATA%\RetroVDPStudio`, with `RetroVDPStudio.exe` and `retrovdp-cli.exe` under `bin`. |
| macOS bundle identifier | `io.github.ciscogarciafl.RetroVDPStudio` for both unsigned previews and signed stable bundles. |
| Linux desktop/application ID | `io.github.ciscogarciafl.RetroVDPStudio`, shared by the desktop file and installed icons. |
| Debian package name | `retrovdp-studio`, matching the product and repository identity. |
| Recipe and settings schema | Version persisted data and preserve backward compatibility or provide an explicit migration. |
| Application version source | `RETROVDP_VERSION` in the root `CMakeLists.txt`; GUI, CLI, packages, tags, asset names, and release titles must match it. |

The earlier beta.1 through beta.3 packages used the retired product identity
and are not in-place installation upgrades. Remove an earlier installed beta
before installing beta.4; portable archives and AppImages can simply be
replaced. On first launch, RetroVDP Studio imports prior per-user settings when
its new settings store is empty, and it accepts version-1 recipes written under
the retired recipe identifier. Every subsequent write uses the new identity.

With the beta.4 identities held stable, the official signed Windows installer
can upgrade that unsigned beta, the official macOS application can replace its
unsigned bundle in Applications, and newer Linux artifacts can replace or
upgrade their matching preview forms. Portable ZIP and AppImage users still
replace the old artifact manually.

The current source uses `CiscoGarciaFL` and `RetroVDPStudio` for Qt settings
identity. The GUI, CLI, packages, and release workflow derive their version
from `RETROVDP_VERSION` in the root `CMakeLists.txt`.

## Required release assets

`<version>` below is the semantic version without the leading tag `v`.

| Platform | Required artifact | Purpose |
| --- | --- | --- |
| Windows x64 | `RetroVDPStudio-<version>-Windows-x64-Setup.exe` | Recommended per-user graphical installer with shortcuts and uninstall support. |
| Windows x64 | `RetroVDPStudio-<version>-Windows-x64-Portable.zip` | Installer-free folder containing GUI, CLI, Qt, plugins, runtime, and notices. |
| macOS Apple Silicon | `RetroVDPStudio-<version>-macOS-arm64.dmg` | Signed and notarized drag-to-Applications GUI bundle. |
| macOS Intel | `RetroVDPStudio-<version>-macOS-x86_64.dmg` | Signed and notarized drag-to-Applications GUI bundle. |
| Linux x64 | `RetroVDPStudio-<version>-Linux-x86_64.AppImage` | Primary portable GUI package. |
| Debian/Ubuntu x64 | `retrovdp-studio_<version>_amd64.deb` | Isolated package with its bundled runtime under `/opt/retrovdp-studio` plus launchers, desktop integration, documentation, and declared base-system dependencies. |
| Linux ARM64 | `RetroVDPStudio-<version>-Linux-aarch64.AppImage` | Native AArch64 portable GUI package. |
| Debian/Ubuntu ARM64 | `retrovdp-studio_<version>_arm64.deb` | Native ARM64 package using the same private `/opt/retrovdp-studio` runtime boundary as the x64 package. |
| All binary releases | `SHA256SUMS.txt` | Digest of every published binary artifact. |

Intel and Apple Silicon DMGs are separate initially because both architectures
already have independent CI coverage. A universal macOS bundle can replace
them only after a universal build and package are tested on both architectures.

The filenames and contents above apply to preview and stable releases. Preview
DMGs and Windows executables may be unsigned when the release is clearly
labeled as described above. Signing and notarization become mandatory gates
for the official stable release.

The Windows installer and portable ZIP contain `RetroVDPStudio.exe` and
`retrovdp-cli.exe`. The installer does not modify `PATH` by default. The
desktop executable uses the Windows GUI subsystem so Explorer and installer
shortcuts do not open a console window; the CLI deliberately uses the Windows
console subsystem. The release workflow inspects both PE subsystem values. The
Debian package installs small launchers in `/usr/bin`, desktop integration
under `/usr/share`, and the actual GUI, CLI, `qt.conf`, Qt libraries, plugins,
and QML modules under `/opt/retrovdp-studio`. The macOS DMG and Linux AppImage
focus on the GUI; matching, versioned CLI archives may be attached when
terminal installation instructions and architecture coverage are finalized.

Linux ARM64/AArch64 is supported through native builds and package tests on
Ubuntu 22.04 and 24.04 ARM64 runners. ARM32/armhf, 32-bit Windows, and other
architectures are not implied by that support. Every additional architecture
requires an explicit native build, package, dependency audit, and clean-system
test matrix before release notes call it supported.

## Package contents

Each package includes only the runtime files it needs:

- the optimized Release GUI and, where specified, CLI executable;
- Qt Core, GUI, Quick, Quick Controls, and other actually used runtime
  libraries;
- required QML modules;
- the platform plugin for the target package;
- required image-format, icon, accessibility, and style plugins;
- the applicable compiler runtime;
- application icons and embedded resources;
- `LICENSE`, `NOTICE.md`, Qt license text, and third-party notices; and
- release/package metadata sufficient to identify the exact application and
  Qt versions.

Tests, object files, static libraries, source-only development files, CMake
metadata, debug runtimes, developer tools, and unused Qt plugins are excluded.
Symbols may be retained as private CI artifacts or published separately for
diagnostics, but are not placed in normal user packages.

### Debian filesystem isolation

A self-contained application runtime must never become a global Qt runtime.
The Debian package therefore follows these ownership boundaries:

- `/opt/retrovdp-studio` owns the private executables, `qt.conf`, libraries,
  plugins, QML modules, resources, and bundled license material;
- `/usr/bin/RetroVDPStudio` and `/usr/bin/retrovdp-cli` are launchers that
  execute the corresponding private binaries;
- `/usr/share/applications`, `/usr/share/icons`, and
  `/usr/share/doc/retrovdp-studio` contain only normal desktop integration and
  documentation; and
- the package must not create `/usr/bin/qt.conf`, `/usr/plugins`, `/usr/qml`,
  `/apprun-hooks`, or unnamespaced bundled Qt, X11, or XCB libraries directly
  under `/usr/lib`.

Keeping `qt.conf` beside the real executable under the private prefix limits
its plugin-path changes to RetroVDP Studio. Removing the package must remove
the complete `/opt/retrovdp-studio` tree and both launchers without touching
host Qt packages or configuration.

## CMake deployment and packaging design

The installed tree is the source of every package. Packaging must not copy an
arbitrary developer build directory, and an AppImage staging root must never
be repurposed as the Debian filesystem root.

1. `install(TARGETS ...)` installs the GUI, CLI, icons, license files, and
   platform metadata into a staging prefix.
2. Qt's `qt_generate_deploy_qml_app_script()` deployment API gathers QML
   modules, Qt libraries, plugins, and supported runtime dependencies for the
   GUI.
3. CLI runtime dependencies not already supplied by the GUI deployment are
   collected explicitly.
4. CPack or a narrowly scoped platform packaging script consumes only the
   staged install tree. Linux creates distinct package roots: linuxdeploy may
   mutate `AppDir` for the AppImage, while the Debian builder relocates that
   deployed runtime under `/opt/retrovdp-studio` and explicitly constructs only
   the approved `/usr` integration files.
5. A package-content audit rejects debug libraries, build-machine paths,
   unintended plugins, missing notices, and unresolved dynamic dependencies.

Windows uses the Qt deployment result to produce an NSIS installer and a ZIP
from the same staged tree. macOS uses a proper `.app` bundle and DMG. Linux uses
an AppImage tool selected and pinned during implementation and a separate,
narrowly scoped Debian builder with a release-blocking filesystem-layout
audit. Packaging tool versions are pinned in release automation.

### Linux ARM64 implementation

Linux ARM64 uses the same private-runtime design as x86-64. The release matrix
builds and tests natively on a pinned Ubuntu 22.04 ARM64 runner with Qt's
`linux_arm64` host and `linux_gcc_arm64` architecture. It packages with the
pinned `linuxdeploy-aarch64.AppImage` and
`linuxdeploy-plugin-qt-aarch64.AppImage` tools, emits an `aarch64` AppImage and
an `Architecture: arm64` Debian archive, and includes both in the final asset
manifest and checksum file.

The ARM64 package job applies the same staged-runtime, dependency,
filesystem-isolation, installed GUI/CLI, uninstall-cleanup, and host-integrity
checks as x86-64. A separate Ubuntu 24.04 ARM64 job downloads those completed
artifacts, extracts and audits every ELF machine type, launches the AppImage,
installs and launches the Debian package, and removes it again. This provides
native testing on both the glibc 2.34 baseline and a current Ubuntu target.

Every new Linux architecture must extend the native runner matrix, Debian
architecture metadata, AppImage tooling, ELF audit, artifact manifest, and
two-environment package tests before it is called supported. See
[GitHub-hosted runner images](https://docs.github.com/en/enterprise-cloud@latest/actions/reference/runners/github-hosted-runners),
[install-qt-action](https://github.com/jurplel/install-qt-action),
[aqtinstall platform support](https://github.com/miurahr/aqtinstall/blob/master/docs/getting_started.rst),
[linuxdeploy releases](https://github.com/linuxdeploy/linuxdeploy/releases/tag/1-alpha-20251107-1),
and [linuxdeploy-plugin-qt releases](https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/tag/1-alpha-20250213-1).

## Version and release policy

- CMake's project version, application version metadata, package version, tag,
  artifact names, and release title must agree.
- Tags use `vMAJOR.MINOR.PATCH`, with prerelease tags such as
  `v0.1.0-alpha.1` or `v1.0.0-rc.1` where appropriate.
- Alpha, beta, and release-candidate builds are marked as GitHub prereleases.
- Stable releases are never built from an unreviewed feature branch.
- A published artifact is never silently replaced. Corrections receive a new
  version and tag.
- Release notes identify supported operating systems and architectures,
  conversion compatibility changes, known issues, and any recipe or settings
  migration concern.

The first packaging exercise was published as `v0.1.0-beta.1`. Corrective
package changes are released under a new tag rather than replacing its assets.
Preview builds may be unsigned under the policy above. `v1.0.0` remains gated
by the stable-release and trust criteria in this document plus the applicable
product quality gates in `PROJECT_PLAN.md`.

## Prerelease readiness checklist

Before tagging each prerelease:

- make CMake the single application-version source and verify that the GUI,
  CLI, package metadata, tag, asset names, and release title agree;
- freeze the Windows installer ID, macOS bundle ID, Linux desktop/application
  ID, executable names, install locations, Debian package name, and per-user
  settings identity;
- generate the Windows setup and portable ZIP, Linux AppImage and Debian
  package, and both macOS DMGs from clean CI checkouts;
- audit each package for required Qt/QML/runtime files, missing dependencies,
  debug files, build-machine paths, licenses, and notices;
- reject Debian archives containing global Qt configuration, plugin, QML, or
  unnamespaced bundled-library paths, and require a positive `Installed-Size`;
- run the package smoke tests on clean systems with no development Qt install;
- install and remove the Debian archive on a clean runner, launch both installed
  entry points, and prove that the private runtime is removed while host Qt
  paths remain unchanged;
- verify beta-to-beta upgrade or replacement without losing settings or saved
  recipes;
- create and verify `SHA256SUMS.txt`;
- prepare release notes with preview status, signing state, platform support,
  expected trust prompts, installation steps, known issues, and manual update
  instructions; include the harmless local-file `_apt` notice, the
  warning-free `/tmp` installation method, and any desktop-session icon-cache
  refresh guidance; and
- publish a complete GitHub prerelease only after every required packaging job
  and final asset-set check succeeds.

The notification-only update checker is recommended before later prereleases
or the first stable release, but it does not block `v0.1.0-beta.1`. A full
automatic installer is not a pre-beta requirement.

## GitHub Actions release workflow

The existing build workflow remains the pull-request and push validation path.
A separate `.github/workflows/release.yml` performs packaging and publishing.
It is triggered by an approved version tag or an explicit manual prerelease
dispatch.

The release flow is:

```text
version/tag validation
    -> native Windows, Linux, Intel Mac, and Apple Silicon Mac builds
    -> complete automated tests
    -> staged Qt/runtime deployment
    -> package creation
    -> package-content and unresolved-dependency audit
    -> signing/notarization where required
    -> clean-runner package smoke tests
    -> SHA-256 generation
    -> exact final asset-set and checksum verification
    -> immutable GitHub prerelease with all assets
```

The workflow uses least-privilege permissions. Signing identities, tokens, and
notarization credentials live in protected GitHub environment secrets, are not
available to pull-request jobs, and are not printed in logs. Packaging jobs
upload intermediate artifacts for diagnosis, but only reviewed final packages
are attached to the release.

The release is created only after all platform jobs succeed. The publish job
verifies names, versions, checksums, notices, release notes, and the exact
asset set before creating the visible prerelease. If the tag already has a
release, the workflow refuses to replace it.

## Signing and platform trust

### Windows

Stable Windows installer and executable files are code-signed before public
release. The signing service or certificate is selected before the first
stable release and held outside the repository. Timestamping is required so a
valid release remains verifiable after certificate expiration.

Unsigned developer and prerelease packages may be produced for controlled
testing, but must be labeled clearly and are not promoted as the normal public
download. Self-signing is not treated as equivalent to a publicly trusted
signature.

### macOS

Public DMGs require an Apple Developer ID signing identity. The application,
embedded frameworks, plugins, and CLI/helper executables are signed with the
hardened runtime, submitted to Apple's notarization service, and stapled. CI
verifies both the code signature and Gatekeeper assessment before publishing.

### Linux

Linux artifacts receive SHA-256 checksums. GPG signing may be added when a
maintainer key and rotation policy are established. The Debian package records
its dependencies and package metadata and is published with mode `0644`; the
AppImage is checked for unresolved libraries and tested outside the build
directory. Desktop package installers depend on a functioning authorization
agent and may fail silently when it is unavailable. Release notes therefore
document `sudo apt install ./<package>.deb` as the reliable fallback without
weakening system-wide security settings.

When that local `.deb` is stored below a directory the restricted `_apt` user
cannot traverse, APT may finish successfully and then report:

```text
N: Download is performed unsandboxed as root ... couldn't be accessed by user '_apt'.
```

This notice concerns APT acquiring the already-local archive; it is not a
package installation or runtime-isolation failure. Making the archive mode
`0644` is insufficient when a parent directory remains private, and the
package cannot change its source path before APT opens it. Release notes must
explain the notice and give this warning-free alternative:

```shell
install -m 0644 ./package.deb /tmp/retrovdp.deb
sudo apt install /tmp/retrovdp.deb
rm /tmp/retrovdp.deb
```

Do not recommend disabling APT's sandbox, making a home directory
world-readable, changing `_apt` ownership, or using mode `0777`. An
authenticated APT repository would avoid this local-file condition because
APT controls its download/cache path.

The Debian archive also records a positive `Installed-Size`, confines bundled
runtime files to `/opt/retrovdp-studio`, and passes both archive-layout and
post-install path audits. A successful application launch alone is not enough:
the test must also prove that unrelated host Qt applications retain their own
configuration and plugin discovery after installation and reboot/logout.

## Supported-system policy

The beta supports Windows 10 version 1809 or newer on x64, macOS 13 or newer
on Intel x86_64 and Apple Silicon arm64, and Linux x86_64 and ARM64 with glibc
2.34 or newer. Ubuntu 22.04 is the pinned Linux build and package-test
baseline, with Ubuntu 24.04 providing a second ARM64 package test. X11 is the
automated Linux display-test target. The application itself launched
successfully during a Kubuntu/Wayland test, but the beta.1 Debian package later
prevented SDDM from starting its greeter after reboot. Kubuntu/Wayland is not a
verified Debian installation environment again until the isolated beta.3
package passes installation, reboot/logout, GUI, CLI, and removal checks on
that system. Broader Wayland support remains provisional until additional
distributions and compositors are covered. No
architecture-specific CPU instructions beyond each platform's normal x86_64
or arm64 baseline are required.

The CI operating system used to produce a Linux release is pinned rather than
`ubuntu-latest`; it must be old enough for the declared compatibility target.
The macOS deployment target is explicit in CMake. ARM64 packages are tested on
both the oldest supported Ubuntu baseline and a current Ubuntu release.

## Clean-system acceptance tests

Every release package is tested from the package itself, not from the build
tree. At minimum, each supported platform verifies:

1. Download or transfer the final artifact and verify its SHA-256 digest.
2. Install, mount, extract, or enable the package exactly as documented.
   For Debian packages, verify the normal desktop installer where available
   and the documented terminal fallback when its authorization agent fails.
   Exercise both a private download location that produces the expected
   `_apt` notice and the documented `/tmp` copy that avoids it.
3. Launch the GUI with no Qt or compiler installed separately. On Windows,
   launching from Explorer or an installer shortcut must not open a console
   window, while the separate CLI must retain console behavior.
4. Confirm icons, fonts, theme, file dialogs, menus, QML controls, and image
   plugins load without missing-module warnings. On KDE Plasma, confirm the
   application-menu icon in both a session that was active during installation
   and a fresh login. If only the active session shows a generic icon, verify
   the desktop entry and hicolor files, then rebuild that user's KService cache
   with the matching `kbuildsycoca` version instead of adding a package script
   that edits per-user caches.
5. Open representative PNG, JPEG, PCX, and supported retro-format inputs.
6. Run representative TMS9918A and F18A conversions.
7. Export at least one raw and one wrapped output and verify the manifest.
8. Save and reload a recipe containing current editor data.
9. Run a representative CLI conversion where the package includes the CLI.
10. Relaunch after reboot or logout where installation state is relevant.
11. Upgrade from the preceding released version without losing user settings.
12. Uninstall or remove the application and verify that only documented user
    settings remain.
13. For Debian packages, verify that no global Qt configuration, plugin, QML,
    or unnamespaced bundled-library path was created and that the desktop login
    manager still starts after reboot/logout.

Automated smoke tests cover everything practical on fresh hosted runners.
Signing, Gatekeeper, SmartScreen, desktop integration, and real installer UX
also receive a recorded manual check on physical or clean virtual systems.

## Licensing and attribution gate

Before publication:

- confirm that the original author's permission covers public binary
  distribution of this derived cross-platform application and its package
  formats;
- retain the complete project `LICENSE` and `NOTICE.md` in every package;
- record the exact Qt edition, version, modules, and applicable LGPL/GPL or
  commercial terms;
- include Qt and bundled third-party notices and available SBOM information;
- verify every added packaging/runtime dependency against the project's custom
  license constraints; and
- exclude GPL-only optional components unless a separately approved licensing
  decision permits them.

This checklist is a release gate, not legal advice. Unresolved redistribution
or notice questions block publication.

## Release notes and support information

Each GitHub Release states:

- whether it is alpha, beta, release candidate, or stable;
- supported platforms and architectures;
- which asset ordinary users should download;
- installation or portable-launch instructions;
- major changes and compatibility differences;
- known limitations and deferred features;
- checksum-verification instructions;
- links to the user documentation, license, attribution, and issue tracker; and
- whether packages are signed/notarized.

Release notes must not call an untested architecture or operating-system
version supported.

## Current implementation status

The repository builds and tests Release code on Windows, Ubuntu x86_64,
Ubuntu ARM64, Intel macOS, and Apple Silicon macOS. CMake owns the version and frozen application
identities, stages the GUI, CLI, notices, exact LGPL/GPL texts, Qt/QML modules,
plugins, and runtime dependencies, and supplies CPack metadata. The tag-driven
release workflow builds Windows setup/portable, native x86_64 and AArch64
Linux AppImage/DEB packages, and native Intel and Apple Silicon DMG assets. It
audits staged contents, dependencies, and Linux ELF architectures, runs GUI
and representative CLI package smoke tests, re-tests ARM64 packages on Ubuntu
24.04, verifies the complete asset set, generates SHA-256 checksums, and
publishes an immutable GitHub prerelease.

The Windows portable tree has also passed the package audit and GUI/CLI smoke
test locally. Feedback from `v0.1.0-beta.1` identified a Windows GUI-subsystem
error and an unreliable graphical Debian installation path; `v0.1.0-beta.2`
corrected the former but retained an unsafe Debian filesystem layout. The
published `v0.1.0-beta.3` implementation isolates the deployed runtime under
`/opt/retrovdp-studio`, adds explicit launchers and desktop integration, records
`Installed-Size`, rejects forbidden global paths, and verifies installation,
launch, removal, and cleanup. Its tagged release workflow passed on 2026-09-27
and published the complete seven-file asset set. The remaining Linux acceptance
item is a recorded clean-Kubuntu install plus reboot/logout, SDDM login, GUI/CLI
launch, and removal test. The notification-only update checker remains optional
for a later prerelease, and automatic installation remains intentionally
deferred. Stable Windows and macOS publication still requires the signing and
notarization gates described above. AppStream/software-center metadata remains
future work.

## Release-process completion definition

The release process is complete enough for stable publication only when:

- every required artifact is reproducibly generated from a clean tagged
  checkout;
- each package runs on clean supported systems without development software;
- the complete GUI and CLI test suites pass before packaging;
- package smoke tests and dependency audits pass;
- Debian filesystem-isolation, installed-launch, host-integrity, and removal
  audits pass;
- required Windows and macOS trust checks pass;
- notices and checksums are present and verified;
- GitHub creates a complete immutable prerelease automatically; and
- a maintainer can publish using this document without relying on undocumented
  workstation state or conversation history.
