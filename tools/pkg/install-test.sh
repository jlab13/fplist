#!/usr/bin/env bash
# Installs the packages of dist/<arch>/ in clean containers of the systems they are for and
# starts fpl there:
#
#   tools/pkg/install-test.sh deb|rpm [amd64|arm64]
#
#   deb: Debian 13, Ubuntu 24.04, Ubuntu 26.04
#   rpm: Fedora (latest), AlmaLinux 10 (RHEL 10), openSUSE Leap 16.0, openSUSE Tumbleweed
#
# Also checks that ca-certificates came along (the metadata is downloaded over HTTPS). Needs
# the network for the repositories of each system. See engine.sh for the container engine.
set -euo pipefail

usage() { echo "usage: tools/pkg/install-test.sh deb|rpm [amd64|arm64]"; }
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
DIST="$ROOT/dist/$ARCH"

# image|installer: the shell snippet run in the container, with the package in /d
DEB_INSTALL='apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq /d/fpl_*.deb >/dev/null'
DNF_INSTALL='dnf install -y -q --nogpgcheck /d/fpl-[0-9]*.rpm >/dev/null'
ZYPPER_INSTALL='zypper -q --non-interactive install --allow-unsigned-rpm /d/fpl-[0-9]*.rpm >/dev/null'
case $FORMAT in
deb)
	ls "$DIST"/fpl_*.deb >/dev/null 2>&1 || { echo "install-test: no fpl_*.deb in ${DIST#"$ROOT"/}: tools/pkg/build.sh deb $ARCH" >&2; exit 1; }
	SYSTEMS=(
		"docker.io/library/debian:trixie|$DEB_INSTALL|dpkg -s ca-certificates >/dev/null"
		"docker.io/library/ubuntu:24.04|$DEB_INSTALL|dpkg -s ca-certificates >/dev/null"
		"docker.io/library/ubuntu:26.04|$DEB_INSTALL|dpkg -s ca-certificates >/dev/null"
	)
	;;
rpm)
	ls "$DIST"/fpl-[0-9]*.rpm >/dev/null 2>&1 || { echo "install-test: no fpl-*.rpm in ${DIST#"$ROOT"/}: tools/pkg/build.sh rpm $ARCH" >&2; exit 1; }
	SYSTEMS=(
		"docker.io/library/fedora:latest|$DNF_INSTALL|rpm -q ca-certificates >/dev/null"
		"docker.io/library/almalinux:10|$DNF_INSTALL|rpm -q ca-certificates >/dev/null"
		"registry.opensuse.org/opensuse/leap:16.0|$ZYPPER_INSTALL|rpm -q ca-certificates >/dev/null"
		"registry.opensuse.org/opensuse/tumbleweed|$ZYPPER_INSTALL|rpm -q ca-certificates >/dev/null"
	)
	;;
esac

FAILED=0
for sys in "${SYSTEMS[@]}"; do
	IFS='|' read -r image install cacert <<<"$sys"
	if engine_run "$ARCH" -v "$DIST:/d$RO" "$image" sh -c "
		$install || exit 1
		v=\$(fpl -V) || exit 1
		$cacert || { echo 'no ca-certificates'; exit 1; }
		. /etc/os-release
		echo \"ok   \$PRETTY_NAME ($ARCH): \$v\""; then
		:
	else
		echo "FAIL $image ($ARCH)" >&2
		FAILED=1
	fi
done
[ $FAILED -eq 0 ] || exit 1
