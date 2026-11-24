# Common settings of the Debian packages built by CPack: "cpack -G DEB" in the build
# directory, or tools/deb/build.sh, which builds them in a Debian 13 container for amd64 and
# arm64.
#
# Include it after the install rules; it sets what every package shares and DEB_VERSION.
# The project then names its packages, adds its dependencies and includes CPack itself.
#
# Derived from the author's other CMake projects; unlike them the version is project(VERSION)
# alone, without the time of the last commit.

# the version: project(VERSION) as it is, e.g. 0.1.0, so the package is fpl_0.1.0_amd64.deb.
# A native package (the packaging lives with the sources): no Debian revision, a new build to
# install over an old one takes a new version in project()
set(DEB_VERSION "${PROJECT_VERSION}")
find_package(Git QUIET)

# the maintainer: -DDEB_MAINTAINER="Name <mail>" (the container build passes the identity of
# whoever runs it), otherwise the git identity of the source tree
if(NOT DEB_MAINTAINER AND GIT_EXECUTABLE)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} config user.name
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE _deb_name
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  execute_process(
    COMMAND ${GIT_EXECUTABLE} config user.email
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE _deb_email
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(_deb_name AND _deb_email)
    set(DEB_MAINTAINER "${_deb_name} <${_deb_email}>")
  endif()
endif()
if(NOT DEB_MAINTAINER)
  message(WARNING "No git identity: set -DDEB_MAINTAINER=\"Name <mail>\" for the packages")
  set(DEB_MAINTAINER "unknown <unknown@localhost>")
endif()

# the architecture as dpkg names it; without dpkg (a build outside Debian) it is mapped from
# the processor, as CPack would otherwise fall back to i386
find_program(DPKG_EXECUTABLE dpkg)
if(DPKG_EXECUTABLE)
  execute_process(
    COMMAND ${DPKG_EXECUTABLE} --print-architecture
    OUTPUT_VARIABLE _deb_arch
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
  set(_deb_arch amd64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
  set(_deb_arch arm64)
else()
  string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _deb_arch)
endif()

# the dependencies on shared libraries come from dpkg-shlibdeps; outside Debian there is none,
# and the packages would declare no dependencies at all. Only a status: a build that installs
# and never packages is the usual case there
find_program(DPKG_SHLIBDEPS_EXECUTABLE dpkg-shlibdeps)
if(DPKG_SHLIBDEPS_EXECUTABLE)
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
else()
  message(STATUS "No dpkg-shlibdeps: packages built here miss their library dependencies, use tools/deb/build.sh")
endif()

# a library package declares its SONAME in a shlibs file, and a package linked against it then
# depends on it: without one dpkg-shlibdeps drops the library from the dependencies silently.
# ">=": the build it was linked against, or a newer one
set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS ON)
set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS_POLICY ">=")

set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "${DEB_MAINTAINER}")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${DEB_MAINTAINER}")
set(CPACK_DEBIAN_PACKAGE_VERSION "${DEB_VERSION}")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE ${_deb_arch})
set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
# <name>_<version>_<arch>.deb, the Debian convention
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
# the symbols go to a <name>-dbgsym package (.ddeb): the build has -g (tools/deb/build.sh),
# and CPack moves the debug sections of the packaged copy there, stripping it. No
# CPACK_STRIP_FILES: a copy stripped before that leaves nothing for the dbgsym package
set(CPACK_DEBIAN_DEBUGINFO_PACKAGE ON)
