cmake_minimum_required(VERSION 3.20)
project(fpl VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON) # gnu11: POSIX/termios helpers
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(FPL_SYSTEM_NOTCURSES "Use system notcurses if available" ON)
option(FPL_SYSTEM_CJSON "Use system cJSON if available" ON)
option(FPL_ASAN "Build with AddressSanitizer/UBSan" OFF)
option(FPL_NLS "Native language support (gettext translations)" ON)

include(FetchContent)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

# --- MAVLink (header-only, generated C library) ------------------------------
set(FPL_MAVLINK_REV d2062a25f05fc85213830427058401b0ec345430)
FetchContent_Declare(mavlink
  URL https://github.com/mavlink/c_library_v2/archive/${FPL_MAVLINK_REV}.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(mavlink)
add_library(mavlink INTERFACE)
# Headers are third-party: mark as SYSTEM to keep our warnings clean.
target_include_directories(mavlink SYSTEM INTERFACE ${mavlink_SOURCE_DIR})

# --- cJSON (PX4 parameter metadata) -------------------------------------------
# The system libcjson when FPL_SYSTEM_CJSON is ON and pkg-config finds 1.7.19 or newer (the
# Arch package), otherwise the bundled one: the containers of the deb and rpm have no libcjson.
add_library(fpl_cjson INTERFACE)
if(FPL_SYSTEM_CJSON)
  pkg_check_modules(CJSON QUIET IMPORTED_TARGET libcjson>=1.7.19)
endif()
if(CJSON_FOUND)
  message(STATUS "cJSON: using system libcjson ${CJSON_VERSION}")
  target_link_libraries(fpl_cjson INTERFACE PkgConfig::CJSON)
else()
  message(STATUS "cJSON: building bundled cJSON 1.7.19")
  # SOURCE_SUBDIR points nowhere: download only, we compile cJSON.c ourselves.
  FetchContent_Declare(cjson
    URL https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.19.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR _none)
  FetchContent_MakeAvailable(cjson)
  add_library(cjson STATIC ${cjson_SOURCE_DIR}/cJSON.c)
  target_include_directories(cjson SYSTEM PUBLIC ${cjson_SOURCE_DIR})
  target_link_libraries(fpl_cjson INTERFACE cjson)
endif()

# --- notcurses ----------------------------------------------------------------
include(cmake/Notcurses.cmake)

# --- system libraries ---------------------------------------------------------
pkg_check_modules(EXPAT REQUIRED IMPORTED_TARGET expat)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)

# --- application --------------------------------------------------------------
add_executable(fpl
  src/main.c
  src/app.c
  src/util.c
  src/link.c
  src/detect.c
  src/vehicle.c
  src/params.c
  src/meta.c
  src/metafetch.c
  src/paramfile.c
  src/favs.c
  src/ui.c
  src/ui_table.c
  src/ui_edit.c
  src/ui_groups.c
  src/i18n.c
  src/ftp.c
  src/parampck.c
)
target_compile_definitions(fpl PRIVATE FPL_VERSION="${PROJECT_VERSION}" _GNU_SOURCE)
# snprintf truncation into fixed UI/status buffers is intentional
target_compile_options(fpl PRIVATE -Wall -Wextra -Wshadow -Wno-unused-parameter
  -Wno-address-of-packed-member -Wno-format-truncation)
target_link_libraries(fpl PRIVATE mavlink fpl_cjson fpl_notcurses
  PkgConfig::EXPAT PkgConfig::CURL Threads::Threads m)

# --- bundled data (machine translations of PX4 metadata) ---------------------
# Copied to <build>/data so the binary finds it when run from the build tree.
include(GNUInstallDirs)
target_compile_definitions(fpl PRIVATE FPL_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/fpl")
set(_data_out)
foreach(fw px4 ardupilot)
  file(GLOB _ts CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/translations/${fw}/*.ts)
  foreach(f IN LISTS _ts)
    get_filename_component(_n ${f} NAME)
    add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/data/${fw}/${_n}
      COMMAND ${CMAKE_COMMAND} -E copy ${f} ${CMAKE_BINARY_DIR}/data/${fw}/${_n}
      DEPENDS ${f})
    list(APPEND _data_out ${CMAKE_BINARY_DIR}/data/${fw}/${_n})
  endforeach()
  install(FILES ${_ts} DESTINATION ${CMAKE_INSTALL_DATADIR}/fpl/${fw})
endforeach()
add_custom_target(fpl_data ALL DEPENDS ${_data_out})

# --- translations -------------------------------------------------------------
include(cmake/I18n.cmake)

# --- test tool: a fake vehicle that speaks the parameter protocol -------------
add_executable(fakevehicle tools/fakevehicle.c)
target_compile_definitions(fakevehicle PRIVATE _GNU_SOURCE)
target_include_directories(fakevehicle PRIVATE src)
target_compile_options(fakevehicle PRIVATE -Wall -Wextra -Wno-unused-parameter
  -Wno-address-of-packed-member)
target_link_libraries(fakevehicle PRIVATE mavlink m util)

if(FPL_ASAN)
  foreach(t fpl fakevehicle)
    target_compile_options(${t} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${t} PRIVATE -fsanitize=address,undefined)
  endforeach()
endif()

install(TARGETS fpl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES LICENSE README.md README_UA.md DESTINATION ${CMAKE_INSTALL_DOCDIR})

# --- packages: deb and rpm, see cmake/DebPackage.cmake and cmake/RpmPackage.cmake
# Built by tools/pkg/build.sh in a container: Debian 13 for the deb, AlmaLinux 10 for the rpm.
# notcurses is linked statically (Debian 13 has none); the shared libraries become dependencies
# through dpkg-shlibdeps and the soname dependencies of rpmbuild.
# The paths of the data and the translations are compiled in: the packages go to the prefix
# the build was configured for (tools/pkg/inside.sh configures /usr)
set(FPL_DESCRIPTION "Terminal (TUI) editor of autopilot parameters over MAVLink, with the columns of
Mission Planner's Full Parameter List. Finds the vehicle on USB, UDP or TCP like
QGroundControl, downloads the parameters (MAVFTP for ArduPilot), shows their metadata
and translations and writes the changes back.")
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/DebPackage.cmake)
set(CPACK_PACKAGING_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Full Parameter List: ArduPilot and PX4 parameter editor")
# the metadata is downloaded over HTTPS
set(_fpl_recommends ca-certificates)

# deb: fpl (+ fpl-dbgsym)
set(CPACK_DEBIAN_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_DEBIAN_PACKAGE_SECTION electronics)
# the extended description only: CPack puts the summary above it and indents every line
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "${FPL_DESCRIPTION}")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/jlab13/fplist")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS ${_fpl_recommends})

# rpm: fpl (+ fpl-debuginfo)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/RpmPackage.cmake)
set(CPACK_RPM_PACKAGE_LICENSE MIT)
set(CPACK_RPM_PACKAGE_URL "https://github.com/jlab13/fplist")
set(CPACK_RPM_PACKAGE_DESCRIPTION "${FPL_DESCRIPTION}")
# no Recommends: CPack 3.31 leaves it out with rpm 4.19, and ca-certificates comes anyway with
# the TLS library of libcurl on every RPM distribution (tools/pkg/install-test.sh checks it)
# the locale directories belong to the system (filesystem package); the package owns only
# its fpl.mo files in them
file(STRINGS ${CMAKE_SOURCE_DIR}/po/LINGUAS _fpl_langs REGEX "^[^#]")
string(REPLACE " " ";" _fpl_langs "${_fpl_langs}")
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION ${CMAKE_INSTALL_FULL_LOCALEDIR})
foreach(_l IN LISTS _fpl_langs)
  list(APPEND CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    ${CMAKE_INSTALL_FULL_LOCALEDIR}/${_l} ${CMAKE_INSTALL_FULL_LOCALEDIR}/${_l}/LC_MESSAGES)
endforeach()
include(CPack)
