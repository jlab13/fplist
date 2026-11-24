# Provides the interface target `gndtui_notcurses`.
#
# 1. If GNDTUI_SYSTEM_NOTCURSES is ON and pkg-config finds notcurses-core,
#    the system library is used.
# 2. Otherwise notcurses is downloaded and built (core only: no multimedia,
#    no C++) at configure time into the build tree and linked statically.
#    Needs: ncursesw (terminfo), libunistring, libdeflate.

set(GNDTUI_NOTCURSES_VERSION 3.0.17)
add_library(gndtui_notcurses INTERFACE)

if(GNDTUI_SYSTEM_NOTCURSES)
  pkg_check_modules(NOTCURSES QUIET IMPORTED_TARGET notcurses-core>=3.0)
endif()

if(NOTCURSES_FOUND)
  message(STATUS "notcurses: using system notcurses-core ${NOTCURSES_VERSION}")
  target_link_libraries(gndtui_notcurses INTERFACE PkgConfig::NOTCURSES)
  return()
endif()

message(STATUS "notcurses: building bundled notcurses ${GNDTUI_NOTCURSES_VERSION}")

# Download only (SOURCE_SUBDIR points nowhere); built below as a separate project.
FetchContent_Declare(notcurses_src
  URL https://github.com/dankamongmen/notcurses/archive/refs/tags/v${GNDTUI_NOTCURSES_VERSION}.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR _none)
FetchContent_MakeAvailable(notcurses_src)

set(_nc_build   ${CMAKE_BINARY_DIR}/_deps/notcurses-build)
set(_nc_install ${CMAKE_BINARY_DIR}/_deps/notcurses-install)
set(_nc_lib     ${_nc_install}/lib/libnotcurses-core.a)
set(_nc_log     ${CMAKE_BINARY_DIR}/_deps/notcurses-build.log)

if(NOT EXISTS ${_nc_lib})
  execute_process(
    COMMAND ${CMAKE_COMMAND} -S ${notcurses_src_SOURCE_DIR} -B ${_nc_build}
      -DCMAKE_BUILD_TYPE=Release
      -DCMAKE_INSTALL_PREFIX=${_nc_install}
      -DCMAKE_INSTALL_LIBDIR=lib
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON
      -DUSE_CXX=OFF -DUSE_PANDOC=OFF -DUSE_DOCTEST=OFF -DUSE_POC=OFF
      -DUSE_MULTIMEDIA=none -DUSE_GPM=OFF -DUSE_QRCODEGEN=OFF
      -DUSE_STATIC=ON -DBUILD_EXECUTABLES=OFF -DBUILD_TESTING=OFF
      -DBUILD_FFI_LIBRARY=OFF
    RESULT_VARIABLE _rc OUTPUT_FILE ${_nc_log} ERROR_FILE ${_nc_log})
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "notcurses: configure failed (${_rc}), see ${_nc_log}")
  endif()
  include(ProcessorCount)
  ProcessorCount(_ncpu)
  if(_ncpu EQUAL 0)
    set(_ncpu 2)
  endif()
  execute_process(
    COMMAND ${CMAKE_COMMAND} --build ${_nc_build} --parallel ${_ncpu}
    RESULT_VARIABLE _rc OUTPUT_FILE ${_nc_log}.build ERROR_FILE ${_nc_log}.build)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "notcurses: build failed (${_rc}), see ${_nc_log}.build")
  endif()
  execute_process(
    COMMAND ${CMAKE_COMMAND} --install ${_nc_build}
    RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_FILE ${_nc_log}.install)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "notcurses: install failed (${_rc})")
  endif()
endif()

pkg_check_modules(NCURSESW REQUIRED IMPORTED_TARGET ncursesw)
find_library(UNISTRING_LIB unistring REQUIRED)
find_library(DEFLATE_LIB deflate REQUIRED)

target_include_directories(gndtui_notcurses SYSTEM INTERFACE ${_nc_install}/include)
target_link_libraries(gndtui_notcurses INTERFACE
  ${_nc_lib} PkgConfig::NCURSESW ${UNISTRING_LIB} ${DEFLATE_LIB} Threads::Threads m)
