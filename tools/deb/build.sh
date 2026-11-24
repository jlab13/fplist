#!/usr/bin/env bash
# Builds the Debian package of fpl in a Debian 13 container, into dist/<arch>/ of the
# repository:
#
#   tools/deb/build.sh           for the architecture of this machine
#   tools/deb/build.sh <arch>    for amd64 or arm64
#
# Podman, Apple container on a Mac, or Docker: the first one found, in this order, unless
# CONTAINER_ENGINE= names one (podman, container, docker). Another architecture than this
# machine's runs under QEMU (README, "Debian package").
#
# Kept to bash 3.2, the bash of macOS: an empty array under set -u expands as ${a[@]+"${a[@]}"}.
set -euo pipefail

case ${1:-} in
-h | --help) echo "usage: tools/deb/build.sh [amd64|arm64]"; exit 0 ;;
esac
[ $# -le 1 ] || { echo "usage: tools/deb/build.sh [amd64|arm64]" >&2; exit 1; }

# the architecture of this machine as dpkg names it, unless one is named
if [ $# -eq 1 ]; then
	ARCHS=("$1")
else
	case $(uname -m) in
	x86_64) ARCHS=(amd64) ;;
	aarch64 | arm64) ARCHS=(arm64) ;;
	*) echo "build.sh: no package architecture for this machine ($(uname -m)): name one, amd64 or arm64" >&2; exit 1 ;;
	esac
fi

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
HERE="$ROOT/tools/deb"
ENGINE=${CONTAINER_ENGINE:-$(command -v podman || command -v container || command -v docker || true)}
[ -n "$ENGINE" ] || { echo "build.sh: no container engine: podman, container or docker" >&2; exit 1; }
KIND=$(basename "$ENGINE")
case $KIND in
podman | container | docker) ;;
*) echo "build.sh: unknown container engine '$ENGINE' (podman, container, docker)" >&2; exit 1 ;;
esac

# the maintainer of the package: whoever builds it
MAINTAINER="$(git -C "$ROOT" config user.name) <$(git -C "$ROOT" config user.email)>"

# Podman, Docker: --platform linux/<arch>; Apple container: --arch, and no :ro on a volume
RO=:ro
[ "$KIND" != container ] || RO=

# rootless Podman maps the root of the container to the user: the packages are already
# theirs. Docker and Apple container run as the real root, so the container hands them over at
# the end
OWNER_ARGS=()
case $KIND in
docker | container) OWNER_ARGS=(-e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)") ;;
esac

engine_build() { # <arch> <image>
	if [ "$KIND" = container ]; then
		"$ENGINE" build --arch "$1" -t "$2" -f "$HERE/Containerfile" "$HERE"
	else
		"$ENGINE" build --platform "linux/$1" -t "$2" -f "$HERE/Containerfile" "$HERE"
	fi
}

engine_run() { # <arch> <run arguments...>
	local arch=$1
	shift
	if [ "$KIND" = container ]; then
		"$ENGINE" run --rm --arch "$arch" "$@"
	else
		"$ENGINE" run --rm --platform "linux/$arch" "$@"
	fi
}

for arch in "${ARCHS[@]}"; do
	case $arch in
	amd64 | arm64) ;;
	*) echo "build.sh: unknown architecture '$arch' (amd64, arm64)" >&2; exit 1 ;;
	esac
	# one tag per architecture: a tag shared by both would follow the last one built
	image="fpl-deb:trixie-$arch"
	out="$ROOT/dist/$arch"

	echo "== $arch: image $image"
	engine_build "$arch" "$image"

	rm -rf "$out"
	mkdir -p "$out"
	echo "== $arch: package into ${out#"$ROOT"/}"
	engine_run "$arch" -v "$ROOT:/src$RO" -v "$out:/out" \
		-e DEB_MAINTAINER="$MAINTAINER" ${OWNER_ARGS[@]+"${OWNER_ARGS[@]}"} \
		"$image" bash /src/tools/deb/inside.sh
	ls -1 "$out"
done
