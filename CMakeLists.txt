cmake_minimum_required(VERSION 3.21)

project(PulseLayer VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build type: single-config generators (Ninja, Makefiles) default to Release.
# Multi-config ones (Visual Studio, Xcode) choose it at build time with --config,
# so a -DCMAKE_BUILD_TYPE given to them has no effect - say so instead of a vague warning.
get_property(_pl_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(_pl_multi_config)
  if(CMAKE_BUILD_TYPE)
    message(STATUS "PulseLayer: ${CMAKE_GENERATOR} picks the configuration at build time - "
                   "use: cmake --build <dir> --config ${CMAKE_BUILD_TYPE}")
  endif()
elseif(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ------------------------------------------------------------------
# Dependencies: Pulse + PulseUI
#
# * Sibling checkouts (../Pulse, ../PulseUI) are used when they exist - handy
#   while hacking on all three projects at once.
# * Otherwise (or with -DPULSELAYER_FETCH_DEPS=ON) the pinned versions below are
#   downloaded into ./external/ and built from source. No git required.
# ------------------------------------------------------------------
set(PULSELAYER_PULSE_REF   "0.2.1" CACHE STRING
    "Pulse version to download: tag, branch or commit")
set(PULSELAYER_PULSEUI_REF "0.4.0" CACHE STRING
    "PulseUI version to download: tag, branch or commit")
set(PULSELAYER_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external" CACHE PATH
    "Where downloaded dependencies are unpacked (one subfolder per version pair)")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../PulseUI/CMakeLists.txt" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../Pulse/CMakeLists.txt")
  set(_pl_have_siblings ON)
else()
  set(_pl_have_siblings OFF)
endif()
if(_pl_have_siblings)
  option(PULSELAYER_FETCH_DEPS "Download Pulse and PulseUI instead of using ../Pulse and ../PulseUI" OFF)
else()
  option(PULSELAYER_FETCH_DEPS "Download Pulse and PulseUI instead of using ../Pulse and ../PulseUI" ON)
endif()

set(PULSE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PULSE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(PULSEUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(PULSEUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)

if(PULSELAYER_FETCH_DEPS)
  include(FetchContent)
  # Both libraries go into one folder per version pair: external/<id>/{Pulse,PulseUI}.
  # Keeping them side by side lets the downloaded PulseUI find ../Pulse on its own,
  # which matters for the programs Pulse Layer builds from this PulseUI copy.
  string(SHA1 _pair "${PULSELAYER_PULSE_REF};${PULSELAYER_PULSEUI_REF}")
  string(SUBSTRING "${_pair}" 0 10 _pair)
  set(_root "${PULSELAYER_DEPS_DIR}/${_pair}")
  foreach(_dep IN ITEMS Pulse PulseUI)
    string(TOUPPER "${_dep}" _up)
    string(TOLOWER "${_dep}" _lo)
    set(_ref "${PULSELAYER_${_up}_REF}")
    set(_src "${_root}/${_dep}")
    if(EXISTS "${_src}/CMakeLists.txt")
      message(STATUS "PulseLayer: ${_dep} @ ${_ref} (${_src})")
    else()
      message(STATUS "PulseLayer: downloading ${_dep} @ ${_ref} -> ${_src}")
      FetchContent_Populate(pl_${_lo}
        QUIET
        URL "https://github.com/danrom11/${_dep}/archive/${_ref}.tar.gz"
        SOURCE_DIR "${_src}"
        SUBBUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/_deps/${_lo}-subbuild"
        BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/_deps/${_lo}-unused")
    endif()
    set(_pl_${_lo}_dir "${_src}")
  endforeach()
  file(WRITE "${_root}/VERSIONS.txt" "Pulse   ${PULSELAYER_PULSE_REF}
PulseUI ${PULSELAYER_PULSEUI_REF}
")
  add_subdirectory("${_pl_pulse_dir}" "${CMAKE_CURRENT_BINARY_DIR}/_pulse")
  set(PULSELAYER_PULSEUI_DIR "${_pl_pulseui_dir}")
else()
  if(NOT _pl_have_siblings)
    message(FATAL_ERROR "PulseLayer: ../Pulse and ../PulseUI not found - configure with -DPULSELAYER_FETCH_DEPS=ON")
  endif()
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../Pulse" "${CMAKE_CURRENT_BINARY_DIR}/_pulse")
  set(PULSELAYER_PULSEUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../PulseUI")
endif()
add_subdirectory("${PULSELAYER_PULSEUI_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_pulseui")

pulseui_add_app(PulseLayer
  src/main.cpp
  src/document.cpp
  src/canvas.cpp
  src/panels.cpp
  src/selftest.cpp
  src/settings.cpp
  src/i18n.cpp
  src/lang/en.cpp
  src/lang/ru.cpp
  src/lang/de.cpp
  src/lang/fr.cpp
  src/process.cpp
  src/toolchain.cpp
  src/builder.cpp
  src/cpp_highlight.cpp
  src/code_view.cpp
  src/project.cpp
  src/project_view.cpp
  src/cpp_complete.cpp
  src/code_tools.cpp
  src/preview_board.cpp
  src/debugger.cpp
  src/debug_view.cpp
  src/release.cpp
)
target_include_directories(PulseLayer PRIVATE src)
get_filename_component(_pulseui_abs "${PULSELAYER_PULSEUI_DIR}" ABSOLUTE)
target_compile_definitions(PulseLayer PRIVATE
  PULSELAYER_VERSION="${PROJECT_VERSION}"
  PULSELAYER_PULSEUI_DIR="${_pulseui_abs}")  # default PulseUI sources for user builds
set_target_properties(PulseLayer PROPERTIES
  MACOSX_BUNDLE_BUNDLE_NAME "Pulse Layer"
  MACOSX_BUNDLE_GUI_IDENTIFIER "dev.pulse.layer"
)

if(MSVC)
  target_compile_options(PulseLayer PRIVATE /utf-8)
endif()

# Sample forms and extra (JSON) languages next to the executable
add_custom_command(TARGET PulseLayer POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/samples" "$<TARGET_FILE_DIR:PulseLayer>/samples")
add_custom_command(TARGET PulseLayer POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/lang" "$<TARGET_FILE_DIR:PulseLayer>/lang")
