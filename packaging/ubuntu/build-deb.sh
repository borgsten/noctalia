#!/usr/bin/env bash
# Build the checked out commit as a noctalia .deb for Ubuntu 24.04.
#
# Usage: packaging/ubuntu/build-deb.sh [--version] [--out DIR]
#   --version  print the package version and exit
#   --out      where the .debs go (default: dist/)

set -euo pipefail

here="$(dirname "$(realpath "${BASH_SOURCE[0]}")")"
source "$here/lib.sh"
src="$(git -C "$here" rev-parse --show-toplevel)"
upstream=https://github.com/noctalia-dev/noctalia.git
# Header-only; noble's libstb-dev predates stb_image_resize2.h
STB_COMMIT=2c980bb59875b0d32144a71867fbdebb2f77cd20

out=dist
print_version=0
while (($#)); do
    case $1 in
        --version) print_version=1 ;;
        --out) out=$2; shift ;;
        *) sed -n '2,11s/^# \?//p' "$0" >&2; exit 2 ;;
    esac
    shift
done
out="$(realpath -m "$out")"

upstream_version="$(<"$src/VERSION")"
tag="v$upstream_version"
# Forks don't carry upstream's tags; fetch just the one the version counts from
if ! git -C "$src" rev-parse -q --verify "refs/tags/$tag" >/dev/null; then
    git -C "$src" fetch --quiet --no-tags "$upstream" "refs/tags/$tag:refs/tags/$tag"
fi
# v5.2.0-3-gbc1e24855 -> 3, gbc1e24855
IFS=- read -r _ ahead commit <<<"$(git -C "$src" describe --tags --long --abbrev=9 --match "$tag")"
version="$upstream_version-0${DEB_SERIES}${ahead}+${commit}"
if [[ -n "$(git -C "$src" status --porcelain --untracked-files=no)" ]]; then
    version+=".dirty"
fi

if ((print_version)); then
    echo "$version"
    exit 0
fi

# libsdbus-c++-dev 2.x and wayland-protocols new enough come from
# ppa:cppiber/hyprland; noble itself only has sdbus-c++ 1.x
sudo apt-get install -y --no-install-recommends \
    build-essential g++-14 curl dpkg-dev git meson ninja-build pkg-config \
    libwayland-dev libwayland-bin wayland-protocols libegl-dev libgles-dev \
    libfreetype-dev libfontconfig-dev libcairo2-dev libpango1.0-dev \
    libharfbuzz-dev librsvg2-dev libxkbcommon-dev libglib2.0-dev \
    libsecret-1-dev libsodium-dev libpolkit-agent-1-dev libpolkit-gobject-1-dev \
    libpipewire-0.3-dev libwireplumber-0.4-dev libcurl4-openssl-dev \
    libqalculate-dev libxml2-dev libmd4c-dev nlohmann-json3-dev \
    libtomlplusplus-dev libical-dev libjemalloc-dev libwebp-dev libjxl-dev \
    libsndfile1-dev libsystemd-dev libpam0g-dev \
    libsdbus-c++-dev

if ! pkg-config --atleast-version=2 sdbus-c++; then
    echo "sdbus-c++ $(pkg-config --modversion sdbus-c++) is too old, add ppa:cppiber/hyprland" >&2
    exit 1
fi

build="$src/build-deb"
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT

stb="$src/build-deb-stb"
if [[ "$(cat "$stb/.commit" 2>/dev/null)" != "$STB_COMMIT" ]]; then
    rm -rf "$stb"
    mkdir -p "$stb/stb"
    for header in stb_image_resize2.h stb_image_write.h; do
        curl -fsSL -o "$stb/stb/$header" \
            "https://raw.githubusercontent.com/nothings/stb/$STB_COMMIT/$header"
    done
    echo "$STB_COMMIT" >"$stb/.commit"
fi

# noble's default GCC 13 lacks C++23 bits noctalia uses (<print>); its
# libstdc++6 already comes from GCC 14, so the result needs nothing extra
export CC=gcc-14 CXX=g++-14
# meson keeps the compiler an existing build dir was configured with
if [[ -d $build ]] && ! grep -qs '"g++-14"' "$build/meson-info/intro-compilers.json"; then
    rm -rf "$build"
fi
meson setup "$build" "$src" --reconfigure \
    --buildtype=release \
    --prefix=/usr \
    -Dc_args="-isystem$stb" \
    -Dcpp_args="-isystem$stb" \
    -Djemalloc=enabled \
    -Dtests=disabled
meson compile -C "$build"
meson install -C "$build" --no-rebuild --skip-subprojects --destdir "$stage"

mkdir "$stage/DEBIAN"
cat >"$stage/DEBIAN/control" <<EOF
Package: noctalia
Version: $version
Architecture: $(deb_arch)
Maintainer: $(maintainer)
Section: x11
Priority: optional
Depends: $(shlib_depends "$stage/usr/bin/noctalia")
Homepage: https://github.com/noctalia-dev/noctalia
Description: A sleek, customizable desktop shell crafted for Wayland.
 Built from the Ubuntu $DEB_SERIES fork at $commit, on top of $tag.
EOF

build_deb "$stage" "$out"
