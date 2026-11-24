#!/usr/bin/env bash
# Runs in the container of build.sh: builds fpl and packages it. The sources are read-only in
# /src, the packages go to /out. CMake downloads MAVLink, cJSON and notcurses, so the container
# needs the network.
set -euo pipefail

# Docker runs as the real root: the sources then belong to another user
git config --global --add safe.directory '*'

# RelWithDebInfo: -O2 -g, the symbols go to the -dbgsym package. /usr: the paths of the data
# and the translations are compiled in
cmake -S /src -B /tmp/fpl -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr \
	-DDEB_MAINTAINER="$DEB_MAINTAINER"
cmake --build /tmp/fpl -j"$(nproc)"
(cd /tmp/fpl && cpack -G DEB -B /tmp/fpl/deb)
cp /tmp/fpl/deb/*.deb /tmp/fpl/deb/*.ddeb /out/

# Docker: the files would stay root's on the host
if [ -n "${HOST_UID:-}" ]; then
	chown "$HOST_UID:${HOST_GID:-$HOST_UID}" /out/*
fi
