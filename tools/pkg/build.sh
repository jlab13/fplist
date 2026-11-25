#!/usr/bin/env bash
# Builds the packages of fpl in a container, into dist/<arch>/ of the repository:
#
#   tools/pkg/build.sh deb [amd64|arm64]    Debian 13: fpl_<version>_<arch>.deb (+ -dbgsym .ddeb),
#                                           for Debian 13 and Ubuntu 24.04 or newer
#   tools/pkg/build.sh rpm [amd64|arm64]    AlmaLinux 10: fpl-<version>-1.<arch>.rpm (+ -debuginfo),
#                                           for Fedora, openSUSE Leap 16 / Tumbleweed, RHEL 10
#
# Without an architecture: the one of this machine. See engine.sh for the container engine.
set -euo pipefail

usage() { echo "usage: tools/pkg/build.sh deb|rpm [amd64|arm64]"; }
case ${1:-} in
-h | --help) usage; exit 0 ;;
deb | rpm) ;;
*) usage >&2; exit 1 ;;
esac
[ $# -le 2 ] || { usage >&2; exit 1; }
FORMAT=$1

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
# shellcheck source=engine.sh
. "$HERE/engine.sh"

ARCH=${2:-$(host_arch)}
check_arch "$ARCH"

# the maintainer of the package: $DEB_MAINTAINER ("Name <mail>", CI sets it), otherwise
# whoever builds it
if [ -n "${DEB_MAINTAINER:-}" ]; then
	MAINTAINER=$DEB_MAINTAINER
else
	name=$(git -C "$ROOT" config user.name || true)
	email=$(git -C "$ROOT" config user.email || true)
	[ -n "$name" ] && [ -n "$email" ] || { echo "build.sh: no git user.name/user.email: set DEB_MAINTAINER=\"Name <mail>\"" >&2; exit 1; }
	MAINTAINER="$name <$email>"
fi

# rootless Podman maps the root of the container to the user: the packages are already
# theirs. Docker and Apple container run as the real root, so the container hands them over at
# the end
OWNER_ARGS=()
case $KIND in
docker | container) OWNER_ARGS=(-e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)") ;;
esac

case $FORMAT in
deb) image="fpl-deb:trixie-$ARCH" ;;
rpm) image="fpl-rpm:el10-$ARCH" ;;
esac
out="$ROOT/dist/$ARCH"

echo "== $FORMAT $ARCH: image $image"
engine_build "$ARCH" "$image" "$HERE/Containerfile.$FORMAT" "$HERE"

# only the packages of this format: the other one may lie there too
mkdir -p "$out"
case $FORMAT in
deb) rm -f "$out"/*.deb "$out"/*.ddeb ;;
rpm) rm -f "$out"/*.rpm ;;
esac
# the sources go in at a long path: the -debuginfo RPM rewrites the source paths in the
# symbols to /usr/src/debug/fpl/src_N, which debugedit can only do to paths at least that long
echo "== $FORMAT $ARCH: packages into ${out#"$ROOT"/}"
engine_run "$ARCH" -v "$ROOT:/usr/src/fpl-package/sources$RO" -v "$out:/out" \
	-e DEB_MAINTAINER="$MAINTAINER" ${OWNER_ARGS[@]+"${OWNER_ARGS[@]}"} \
	"$image" bash /usr/src/fpl-package/sources/tools/pkg/inside.sh "$FORMAT"
ls -1 "$out"
