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

build="$src/build-deb"
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT

# export CC=gcc-14 CXX=g++-14
# meson keeps the compiler an existing build dir was configured with
if [[ -d $build ]] && ! grep -qs '"g++-14"' "$build/meson-info/intro-compilers.json"; then
    rm -rf "$build"
fi
meson setup "$build" "$src" --reconfigure \
    --buildtype=release \
    --prefix=/usr \
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

mkdir -p "$stage/usr/lib/systemd/user/"
cp "$src/assets/noctalia.service" "$stage/usr/lib/systemd/user/noctalia.service"

build_deb "$stage" "$out"
