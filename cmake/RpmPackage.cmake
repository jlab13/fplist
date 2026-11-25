# Settings of the RPM package built by CPack: "cpack -G RPM" in the build directory, or
# tools/pkg/build.sh rpm, which builds it in an AlmaLinux 10 container for amd64 and arm64.
#
# Include it after cmake/DebPackage.cmake: the version (DEB_VERSION is the project version) and
# the maintainer are the same for both formats.

# <name>-<version>-<release>.<arch>.rpm, e.g. fpl-0.1.0-1.x86_64.rpm. The release counts the
# packagings of one version. No dist tag (.fc45, .el10): one package for every distribution,
# its dependencies are sonames (libcurl.so.4()(64bit), ...), found by rpmbuild itself
set(CPACK_RPM_PACKAGE_RELEASE 1)
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)

# RPM has no maintainer field: the vendor names who built it
set(CPACK_RPM_PACKAGE_VENDOR "${DEB_MAINTAINER}")

# the symbols go to <name>-debuginfo (the build has -g). Its sources go to
# /usr/src/debug/<name>/src_N, and debugedit only writes that over a path at least as long:
# build.sh mounts the sources at a long one
set(CPACK_RPM_DEBUGINFO_PACKAGE ON)
set(CPACK_RPM_BUILD_SOURCE_DIRS_PREFIX "/usr/src/debug/${PROJECT_NAME}")
