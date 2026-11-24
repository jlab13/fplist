cmake_minimum_required(VERSION 3.20)
project(gndtui VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON) # gnu11: POSIX/termios helpers
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(GNDTUI_SYSTEM_NOTCURSES "Use system notcurses if available" ON)
option(GNDTUI_ASAN "Build with AddressSanitizer/UBSan" OFF)
option(GNDTUI_NLS "Native language support (gettext translations)" ON)

include(FetchContent)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

# --- MAVLink (header-only, generated C library) ------------------------------
set(GNDTUI_MAVLINK_REV d2062a25f05fc85213830427058401b0ec345430)
FetchContent_Declare(mavlink
  URL https://github.com/mavlink/c_library_v2/archive/${GNDTUI_MAVLINK_REV}.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(mavlink)
add_library(mavlink INTERFACE)
# Headers are third-party: mark as SYSTEM to keep our warnings clean.
target_include_directories(mavlink SYSTEM INTERFACE ${mavlink_SOURCE_DIR})

# --- cJSON (PX4 parameter metadata) -------------------------------------------
# SOURCE_SUBDIR points nowhere: download only, we compile cJSON.c ourselves.
FetchContent_Declare(cjson
  URL https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.19.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR _none)
FetchContent_MakeAvailable(cjson)
add_library(cjson STATIC ${cjson_SOURCE_DIR}/cJSON.c)
target_include_directories(cjson SYSTEM PUBLIC ${cjson_SOURCE_DIR})

# --- notcurses ----------------------------------------------------------------
include(cmake/Notcurses.cmake)

# --- system libraries ---------------------------------------------------------
pkg_check_modules(EXPAT REQUIRED IMPORTED_TARGET expat)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)

# --- application --------------------------------------------------------------
add_executable(gndtui
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
target_compile_definitions(gndtui PRIVATE GNDTUI_VERSION="${PROJECT_VERSION}" _GNU_SOURCE)
# snprintf truncation into fixed UI/status buffers is intentional
target_compile_options(gndtui PRIVATE -Wall -Wextra -Wshadow -Wno-unused-parameter
  -Wno-address-of-packed-member -Wno-format-truncation)
target_link_libraries(gndtui PRIVATE mavlink cjson gndtui_notcurses
  PkgConfig::EXPAT PkgConfig::CURL Threads::Threads m)

# --- bundled data (machine translations of PX4 metadata) ---------------------
# Copied to <build>/data so the binary finds it when run from the build tree.
include(GNUInstallDirs)
target_compile_definitions(gndtui PRIVATE GNDTUI_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/gndtui")
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
  install(FILES ${_ts} DESTINATION ${CMAKE_INSTALL_DATADIR}/gndtui/${fw})
endforeach()
add_custom_target(gndtui_data ALL DEPENDS ${_data_out})

# --- translations -------------------------------------------------------------
include(cmake/I18n.cmake)

# --- test tool: a fake vehicle that speaks the parameter protocol -------------
add_executable(fakevehicle tools/fakevehicle.c)
target_compile_definitions(fakevehicle PRIVATE _GNU_SOURCE)
target_include_directories(fakevehicle PRIVATE src)
target_compile_options(fakevehicle PRIVATE -Wall -Wextra -Wno-unused-parameter
  -Wno-address-of-packed-member)
target_link_libraries(fakevehicle PRIVATE mavlink m util)

if(GNDTUI_ASAN)
  foreach(t gndtui fakevehicle)
    target_compile_options(${t} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${t} PRIVATE -fsanitize=address,undefined)
  endforeach()
endif()

install(TARGETS gndtui RUNTIME DESTINATION bin)
