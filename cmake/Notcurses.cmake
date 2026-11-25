# Provides the interface target `fpl_notcurses`.
#
# 1. If FPL_SYSTEM_NOTCURSES is ON and pkg-config finds notcurses-core,
#    the system library is used.
# 2. Otherwise notcurses is downloaded and built (core only: no multimedia,
#    no C++) at configure time into the build tree and linked statically.
#    Needs: ncursesw (terminfo), libunistring, zlib. zlib rather than
#    notcurses' default libdeflate: zlib is in the base of every distribution,
#    libdeflate is not (RHEL 10 has it only in EPEL).

set(FPL_NOTCURSES_VERSION 3.0.17)
add_library(fpl_notcurses INTERFACE)

if(FPL_SYSTEM_NOTCURSES)
  pkg_check_modules(NOTCURSES QUIET IMPORTED_TARGET notcurses-core>=3.0)
endif()

if(NOTCURSES_FOUND)
  message(STATUS "notcurses: using system notcurses-core ${NOTCURSES_VERSION}")
  target_link_libraries(fpl_notcurses INTERFACE PkgConfig::NOTCURSES)
  return()
endif()

message(STATUS "notcurses: building bundled notcurses ${FPL_NOTCURSES_VERSION}")

# Download only (SOURCE_SUBDIR points nowhere); built below as a separate project.
FetchContent_Declare(notcurses_src
  URL https://github.com/dankamongmen/notcurses/archive/refs/tags/v${FPL_NOTCURSES_VERSION}.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR _none)
FetchContent_MakeAvailable(notcurses_src)

# Portability patch: notcurses frees the terminal with del_curterm(cur_term). cur_term is a
# variable in the usual ncurses ABI, but in the reentrant one of openSUSE it is a function
# (_nc_cur_term), so a binary built on another distribution stops there with "undefined
# symbol: cur_term". set_curterm(NULL) hands over the same terminal and exists in both.
foreach(_f src/lib/direct.c src/lib/termdesc.c src/lib/notcurses.c)
  file(READ ${notcurses_src_SOURCE_DIR}/${_f} _nc_c)
  string(REPLACE "del_curterm(cur_term)" "del_curterm(set_curterm(NULL))" _nc_p "${_nc_c}")
  if(NOT _nc_p STREQUAL _nc_c)
    file(WRITE ${notcurses_src_SOURCE_DIR}/${_f} "${_nc_p}")
  endif()
endforeach()

# the version, the options and the patch in the directory names: a change builds it anew
set(_nc_tag     ${FPL_NOTCURSES_VERSION}-zlib-curterm)
set(_nc_build   ${CMAKE_BINARY_DIR}/_deps/notcurses-${_nc_tag}-build)
set(_nc_install ${CMAKE_BINARY_DIR}/_deps/notcurses-${_nc_tag}-install)
set(_nc_lib     ${_nc_install}/lib/libnotcurses-core.a)
set(_nc_log     ${CMAKE_BINARY_DIR}/_deps/notcurses-${_nc_tag}-build.log)

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
      -DBUILD_FFI_LIBRARY=OFF -DUSE_DEFLATE=OFF
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
find_package(ZLIB REQUIRED)

target_include_directories(fpl_notcurses SYSTEM INTERFACE ${_nc_install}/include)
target_link_libraries(fpl_notcurses INTERFACE
  ${_nc_lib} PkgConfig::NCURSESW ${UNISTRING_LIB} ZLIB::ZLIB Threads::Threads m)
