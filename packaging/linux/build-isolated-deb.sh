#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 5 ]]; then
  echo "usage: $0 <deployed-root> <deb-root> <version> <architecture> <output.deb>" >&2
  exit 2
fi

workspace="$(pwd -P)"
deployed_root="$(realpath "$1")"
deb_root="$(realpath -m "$2")"
release_version="$3"
package_architecture="$4"
output_deb="$(realpath -m "$5")"

case "${package_architecture}" in
  amd64|arm64) ;;
  *)
    echo "Unsupported Debian architecture: ${package_architecture}" >&2
    exit 2
    ;;
esac

case "${deb_root}" in
  "${workspace}"/*) ;;
  *)
    echo "Debian staging root must be inside ${workspace}: ${deb_root}" >&2
    exit 2
    ;;
esac
if [[ "${deb_root}" == "${workspace}" ]]; then
  echo "Refusing to use the workspace itself as the Debian staging root." >&2
  exit 2
fi

for required in \
  bin/RetroVDPStudio \
  bin/retrovdp-cli \
  bin/qt.conf \
  lib/libQt6Core.so.6 \
  plugins/platforms/libqxcb.so \
  share/applications/io.github.ciscogarciafl.RetroVDPStudio.desktop \
  share/icons/hicolor/256x256/apps/io.github.ciscogarciafl.RetroVDPStudio.png \
  LICENSE NOTICE.md THIRD_PARTY_NOTICES.md \
  LICENSES/LGPL-3.0.txt LICENSES/GPL-3.0.txt; do
  if [[ ! -e "${deployed_root}/${required}" ]]; then
    echo "Deployed runtime is missing ${required}" >&2
    exit 1
  fi
done

rm -rf -- "${deb_root}"
install -d \
  "${deb_root}/DEBIAN" \
  "${deb_root}/opt/retrovdp-studio" \
  "${deb_root}/usr/bin" \
  "${deb_root}/usr/share/applications" \
  "${deb_root}/usr/share/icons" \
  "${deb_root}/usr/share/doc/retrovdp-studio"

# linuxdeploy's self-contained tree is safe only when it remains private to
# the application. Its qt.conf, libraries, plugins, and QML modules must never
# be installed into global /usr paths.
cp -a "${deployed_root}/." "${deb_root}/opt/retrovdp-studio/"
rm -rf -- \
  "${deb_root}/opt/retrovdp-studio/share/applications" \
  "${deb_root}/opt/retrovdp-studio/share/doc" \
  "${deb_root}/opt/retrovdp-studio/share/icons"

install -m 0755 packaging/linux/retrovdp-studio-gui \
  "${deb_root}/usr/bin/RetroVDPStudio"
install -m 0755 packaging/linux/retrovdp-cli \
  "${deb_root}/usr/bin/retrovdp-cli"
install -m 0644 \
  "${deployed_root}/share/applications/io.github.ciscogarciafl.RetroVDPStudio.desktop" \
  "${deb_root}/usr/share/applications/io.github.ciscogarciafl.RetroVDPStudio.desktop"
cp -a "${deployed_root}/share/icons/hicolor" \
  "${deb_root}/usr/share/icons/"

install -m 0644 "${deployed_root}/LICENSE" \
  "${deb_root}/usr/share/doc/retrovdp-studio/LICENSE"
install -m 0644 "${deployed_root}/NOTICE.md" \
  "${deb_root}/usr/share/doc/retrovdp-studio/NOTICE.md"
install -m 0644 "${deployed_root}/THIRD_PARTY_NOTICES.md" \
  "${deb_root}/usr/share/doc/retrovdp-studio/THIRD_PARTY_NOTICES.md"
cp -a "${deployed_root}/LICENSES" \
  "${deb_root}/usr/share/doc/retrovdp-studio/"

if [[ "${release_version}" == *-* ]]; then
  version_core="${release_version%%-*}"
  version_suffix="${release_version#*-}"
  deb_version="${version_core}~${version_suffix}"
else
  deb_version="${release_version}"
fi
installed_size="$(du -sk "${deb_root}" | awk '{print $1}')"
if [[ ! "${installed_size}" =~ ^[1-9][0-9]*$ ]]; then
  echo "Unable to determine a positive Installed-Size." >&2
  exit 1
fi

sed \
  -e "s|@VERSION@|${deb_version}|g" \
  -e "s|@ARCHITECTURE@|${package_architecture}|g" \
  -e "s|@INSTALLED_SIZE@|${installed_size}|g" \
  packaging/linux/control.in > "${deb_root}/DEBIAN/control"
chmod 0644 "${deb_root}/DEBIAN/control"

mkdir -p "$(dirname "${output_deb}")"
dpkg-deb --root-owner-group --build "${deb_root}" "${output_deb}"
chmod 0644 "${output_deb}"
