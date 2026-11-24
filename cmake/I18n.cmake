# gettext localisation.
#   po/LINGUAS          languages to build
#   po/<lang>.po        translations
#   po/gndtui.pot       template; regenerate with `cmake --build build --target pot`
#                       (also merges new strings into every <lang>.po)
# Catalogs are compiled into <build>/locale/<lang>/LC_MESSAGES/gndtui.mo so the
# binary finds them when run from the build tree, and installed to
# ${CMAKE_INSTALL_LOCALEDIR}.

include(GNUInstallDirs)

if(NOT GNDTUI_NLS)
  return()
endif()

find_package(Intl REQUIRED)
find_package(Gettext REQUIRED) # msgfmt, msgmerge
find_program(XGETTEXT_EXECUTABLE xgettext)

target_compile_definitions(gndtui PRIVATE GNDTUI_NLS
  GNDTUI_LOCALEDIR="${CMAKE_INSTALL_FULL_LOCALEDIR}")
target_include_directories(gndtui PRIVATE ${Intl_INCLUDE_DIRS})
target_link_libraries(gndtui PRIVATE ${Intl_LIBRARIES})

set(_po_dir ${CMAKE_SOURCE_DIR}/po)
file(STRINGS ${_po_dir}/LINGUAS _langs REGEX "^[^#]")
string(REPLACE " " ";" _langs "${_langs}")

set(_mo_files)
foreach(lang IN LISTS _langs)
  set(_po ${_po_dir}/${lang}.po)
  set(_mo ${CMAKE_BINARY_DIR}/locale/${lang}/LC_MESSAGES/gndtui.mo)
  add_custom_command(OUTPUT ${_mo}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/locale/${lang}/LC_MESSAGES
    COMMAND ${GETTEXT_MSGFMT_EXECUTABLE} --check -o ${_mo} ${_po}
    DEPENDS ${_po}
    COMMENT "Compiling ${lang} translation")
  list(APPEND _mo_files ${_mo})
  install(FILES ${_mo} DESTINATION ${CMAKE_INSTALL_LOCALEDIR}/${lang}/LC_MESSAGES)
endforeach()
add_custom_target(translations ALL DEPENDS ${_mo_files})

if(XGETTEXT_EXECUTABLE)
  file(GLOB _i18n_sources RELATIVE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/*.c)
  list(SORT _i18n_sources)
  set(_merge_cmds)
  foreach(lang IN LISTS _langs)
    list(APPEND _merge_cmds COMMAND ${GETTEXT_MSGMERGE_EXECUTABLE} --quiet --update
      --backup=none ${_po_dir}/${lang}.po ${_po_dir}/gndtui.pot)
  endforeach()
  add_custom_target(pot
    COMMAND ${XGETTEXT_EXECUTABLE} --language=C --from-code=UTF-8
      --keyword=_ --keyword=N_ --keyword=ngettext:1,2
      --add-comments=TRANSLATORS: --sort-by-file
      --package-name=gndtui --package-version=${PROJECT_VERSION}
      --msgid-bugs-address= --copyright-holder=
      -o ${_po_dir}/gndtui.pot ${_i18n_sources}
    ${_merge_cmds}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Extracting translatable strings into po/gndtui.pot")
endif()
