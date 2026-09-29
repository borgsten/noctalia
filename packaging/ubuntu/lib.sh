# Helpers for building binary .debs from a staged install tree.
# Sourced by the scripts in this directory.

# The series the packages are built for, part of every version string
DEB_SERIES=noble

deb_arch() {
    dpkg --print-architecture
}

# shlibs:Depends for the given ELF files, resolved against installed packages
shlib_depends() {
    local work
    work="$(mktemp -d)"
    mkdir "$work/debian"
    # dpkg-shlibdeps insists on a control file even though it only needs the arch
    printf 'Source: x\n\nPackage: x\nArchitecture: any\n' >"$work/debian/control"
    (cd "$work" && dpkg-shlibdeps -O "$@") | sed -n 's/^shlibs:Depends=//p'
    rm -rf "$work"
}

# build_deb <staged root> <out dir>; reads the control file from <root>/DEBIAN
build_deb() {
    local root=$1 out=$2 name version
    name="$(sed -n 's/^Package: //p' "$root/DEBIAN/control")"
    version="$(sed -n 's/^Version: //p' "$root/DEBIAN/control")"
    echo "Installed-Size: $(du -sk --exclude=DEBIAN "$root" | cut -f1)" >>"$root/DEBIAN/control"
    mkdir -p "$out"
    dpkg-deb --root-owner-group --build "$root" "$out/${name}_${version}_$(deb_arch).deb"
}

maintainer() {
    echo "$(git config user.name || echo "$USER") <$(git config user.email || echo "$USER@localhost")>"
}
