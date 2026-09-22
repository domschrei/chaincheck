cmake_minimum_required(VERSION 3.16)
project(ProofChecker)

set(CMAKE_CXX_STANDARD 20)

# dsr-trim und lsr-check (https://github.com/ccodel/dsr-trim) werden als
# externes, Makefile-basiertes Projekt mitgebaut (kein CMake-Projekt selbst,
# daher ExternalProject_Add statt FetchContent+add_subdirectory).
include(ExternalProject)

ExternalProject_Add(dsr_trim_ext
    GIT_REPOSITORY    https://github.com/ccodel/dsr-trim.git
    GIT_TAG           b812f5445a8297aed1fc25777de87237faa8750a
    SOURCE_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/extern/dsr_trim
    UPDATE_DISCONNECTED TRUE
    CONFIGURE_COMMAND ""
    BUILD_IN_SOURCE   TRUE
    BUILD_COMMAND     make
    INSTALL_COMMAND   ""
    BUILD_BYPRODUCTS  <SOURCE_DIR>/bin/dsr-trim <SOURCE_DIR>/bin/lsr-check
)

ExternalProject_Get_Property(dsr_trim_ext SOURCE_DIR)
set(DSR_TRIM_BINARY  ${SOURCE_DIR}/bin/dsr-trim)
set(LSR_CHECK_BINARY ${SOURCE_DIR}/bin/lsr-check)
set(DSR_COMPRESS_BINARY ${SOURCE_DIR}/bin/compress)

# PalRUP-Check (https://github.com/rubenGoetz/PalRUP-Check) prüft PalRUP-
# Beweise (parallele/verteilte Beweisfragmente). Ist selbst ein CMake-Projekt,
# aber nicht gefahrlos per add_subdirectory()/FetchContent einbindbar: sein
# CMakeLists.txt verwendet im copy_scripts-Target CMAKE_SOURCE_DIR statt
# CMAKE_CURRENT_SOURCE_DIR -- beim Verschachteln würde das auf unser
# Wurzelverzeichnis statt auf extern/palrup_check zeigen und fehlschlagen.
# Deswegen auch hier ExternalProject_Add (eigener, unabhängiger Build), mit
# explizitem SOURCE_DIR, damit der Code wie gewünscht unter extern/ landet.
ExternalProject_Add(palrup_check_ext
    GIT_REPOSITORY    https://github.com/rubenGoetz/PalRUP-Check.git
    GIT_TAG           d089fd6dd4425f56fb8c62ea96cf2be495b41051
    SOURCE_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/extern/palrup_check
    UPDATE_DISCONNECTED TRUE
    INSTALL_COMMAND   ""
    BUILD_BYPRODUCTS  <BINARY_DIR>/palrup_local_check
                       <BINARY_DIR>/palrup_redistribute
                       <BINARY_DIR>/palrup_confirm
)

ExternalProject_Get_Property(palrup_check_ext BINARY_DIR)
set(PALRUP_LOCAL_CHECK_BINARY  ${BINARY_DIR}/palrup_local_check)
set(PALRUP_REDISTRIBUTE_BINARY ${BINARY_DIR}/palrup_redistribute)
set(PALRUP_CONFIRM_BINARY      ${BINARY_DIR}/palrup_confirm)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/external_tools.hpp.in
    ${CMAKE_BINARY_DIR}/generated/external_tools.hpp
    @ONLY
)

add_executable(proof_checker
    src/main.cpp
    src/checker_factory.cpp
)
target_include_directories(proof_checker PRIVATE
    ${CMAKE_BINARY_DIR}/generated
)
add_dependencies(proof_checker dsr_trim_ext palrup_check_ext)