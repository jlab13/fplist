#!/usr/bin/env bash
# Runs in the container of build.sh: builds fpl and packages it, deb or rpm. The sources are
# read-only in /usr/src/fpl-package/sources, the packages go to /out. CMake downloads MAVLink,
# cJSON and notcurses, so the container needs the network.
set -euo pipefail
FORMAT=$1
SRC=/usr/src/fpl-package/sources
BIN=/usr/src/fpl-package/build

# Docker runs as the real root: the sources then belong to another user
git config --global --add safe.directory '*'

# RelWithDebInfo: -O2 -g, the symbols go to the -dbgsym / -debuginfo package. /usr: the paths
# of the data and the translations are compiled in
cmake -S "$SRC" -B "$BIN" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr \
	-DDEB_MAINTAINER="$DEB_MAINTAINER"
cmake --build "$BIN" -j"$(nproc)"
case $FORMAT in
deb)
	(cd "$BIN" && cpack -G DEB -B "$BIN/pkg")
	cp "$BIN"/pkg/*.deb "$BIN"/pkg/*.ddeb /out/
	;;
rpm)
	(cd "$BIN" && cpack -G RPM -B "$BIN/pkg")
	cp "$BIN"/pkg/*.rpm /out/
	;;
esac

# Docker: the files would stay root's on the host
if [ -n "${HOST_UID:-}" ]; then
	chown "$HOST_UID:${HOST_GID:-$HOST_UID}" /out/*
fi
