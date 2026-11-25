# Sourced by build.sh and install-test.sh: the container engine and how to run an image for
# an architecture.
#
# Podman, Apple container on a Mac, or Docker: the first one found, in this order, unless
# CONTAINER_ENGINE= names one (podman, container, docker). Another architecture than this
# machine's runs under QEMU (README, "Packages").
#
# Kept to bash 3.2, the bash of macOS: an empty array under set -u expands as ${a[@]+"${a[@]}"}.

ENGINE=${CONTAINER_ENGINE:-$(command -v podman || command -v container || command -v docker || true)}
[ -n "$ENGINE" ] || { echo "$0: no container engine: podman, container or docker" >&2; exit 1; }
KIND=$(basename "$ENGINE")
case $KIND in
podman | container | docker) ;;
*) echo "$0: unknown container engine '$ENGINE' (podman, container, docker)" >&2; exit 1 ;;
esac

# Podman, Docker: --platform linux/<arch>; Apple container: --arch, and no :ro on a volume
RO=:ro
[ "$KIND" != container ] || RO=

# the architecture of this machine as dpkg names it
host_arch() {
	case $(uname -m) in
	x86_64) echo amd64 ;;
	aarch64 | arm64) echo arm64 ;;
	*) echo "$0: no package architecture for this machine ($(uname -m)): name one, amd64 or arm64" >&2; exit 1 ;;
	esac
}

check_arch() { # <arch>
	case $1 in
	amd64 | arm64) ;;
	*) echo "$0: unknown architecture '$1' (amd64, arm64)" >&2; exit 1 ;;
	esac
}

engine_build() { # <arch> <image> <containerfile> <context>
	if [ "$KIND" = container ]; then
		"$ENGINE" build --arch "$1" -t "$2" -f "$3" "$4"
	else
		"$ENGINE" build --platform "linux/$1" -t "$2" -f "$3" "$4"
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
