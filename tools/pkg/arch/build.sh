#!/usr/bin/env bash
# Builds the Arch Linux package of fpl from the working tree, into dist/arch/ of the repository:
#
#   tools/pkg/arch/build.sh     fpl-<version>-<rel>-<arch>.pkg.tar.zst (+ fpl-debug)
#
# On Arch itself, no container. makepkg installs nothing: base-devel, cmake and the depends of
# the PKGBUILD (notcurses, cjson, curl, expat) have to be there. namcap, when installed, checks
# the PKGBUILD and the package. Then:
#
#   sudo pacman -U dist/arch/fpl-<version>-<rel>-<arch>.pkg.tar.zst
#
# The PKGBUILD downloads the release tarball of GitHub; here the tarball is made from the files
# of the working tree (tracked and untracked, without the ignored ones), under its name.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
WORK=$ROOT/build-arch # build-*/ is ignored; the MAVLink tarball stays there between builds
OUT=$ROOT/dist/arch

version=$(sed -n 's/^project(fpl VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")
pkgver=$(sed -n 's/^pkgver=//p' "$HERE/PKGBUILD")
if [ "$version" != "$pkgver" ]; then
	echo "build.sh: PKGBUILD has pkgver=$pkgver, CMakeLists.txt project(fpl VERSION $version)" >&2
	exit 1
fi

mkdir -p "$WORK" "$OUT"
cp "$HERE/PKGBUILD" "$WORK/"
tarball=$WORK/fpl-$version.tar.gz
# the GitHub tarball has everything under fplist-<version>/; a deleted tracked file is left out
(cd "$ROOT" && git ls-files -z --cached --others --exclude-standard --deduplicate |
	while IFS= read -r -d '' f; do [ -e "$f" ] && printf '%s\0' "$f"; done |
	tar --null -T - --transform "s,^,fplist-$version/," -czf "$tarball")

rm -f "$OUT"/fpl-*.pkg.tar.*
(cd "$WORK" && PKGDEST=$OUT makepkg --force --cleanbuild)

if command -v namcap >/dev/null; then
	echo "== namcap"
	namcap "$WORK/PKGBUILD"
	namcap "$OUT"/fpl-"$version"-*.pkg.tar.*
else
	echo "build.sh: no namcap, the package is not checked" >&2
fi
ls -l "$OUT"
