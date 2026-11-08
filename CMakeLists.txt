cmake_minimum_required(VERSION 3.28)
project(obs-scene-deck VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(SCENE_DECK_BUILD_PLUGIN "Build the OBS plugin" ON)
option(SCENE_DECK_RUNTIME_TESTS "Build tests with real libobs and a simulated frontend" OFF)
option(SCENE_DECK_SYSTEM_INSTALL "Use system library/share directories on Linux" OFF)
include(CTest)

if(BUILD_TESTING)
  add_executable(scene-deck-core-tests tests/core-tests.cpp)
  target_include_directories(scene-deck-core-tests PRIVATE src)
  add_test(NAME scene-deck-core COMMAND scene-deck-core-tests)
endif()

if(NOT SCENE_DECK_BUILD_PLUGIN)
  return()
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)
set(SCENE_DECK_OBS_SOURCE "" CACHE PATH "Matching OBS source checkout (Windows/macOS runtime SDK mode)")
set(SCENE_DECK_OBS_LIBS "" CACHE PATH "Directory with obs.lib and obs-frontend-api.lib")
set(SCENE_DECK_OBS_APP "" CACHE PATH "Matching OBS.app bundle (macOS runtime SDK mode)")

if((WIN32 AND SCENE_DECK_OBS_SOURCE AND SCENE_DECK_OBS_LIBS) OR
   (APPLE AND SCENE_DECK_OBS_SOURCE AND SCENE_DECK_OBS_APP))
  # Build against public headers and import libraries from the installed OBS.
  # No OBS implementation files or private frontend widgets are linked.
  set(OBS_RELEASE_CANDIDATE 0)
  set(OBS_BETA 0)
  configure_file("${SCENE_DECK_OBS_SOURCE}/libobs/obsconfig.h.in"
                 "${CMAKE_CURRENT_BINARY_DIR}/obs-generated/obsconfig.h")
  add_library(scene-deck-obs INTERFACE)
  target_include_directories(scene-deck-obs SYSTEM INTERFACE
    "${SCENE_DECK_OBS_SOURCE}/libobs" "${SCENE_DECK_OBS_SOURCE}/frontend/api"
    "${CMAKE_CURRENT_BINARY_DIR}/obs-generated")
  if(WIN32)
    target_link_libraries(scene-deck-obs INTERFACE
      "${SCENE_DECK_OBS_LIBS}/obs.lib" "${SCENE_DECK_OBS_LIBS}/obs-frontend-api.lib")
  else()
    set(obs_frameworks "${SCENE_DECK_OBS_APP}/Contents/Frameworks")
    if(NOT EXISTS "${obs_frameworks}/libobs.framework/libobs" OR NOT EXISTS "${obs_frameworks}/obs-frontend-api.dylib")
      message(FATAL_ERROR "OBS.app does not contain the expected libobs/framework and frontend API. Use a matching Development SDK instead.")
    endif()
    target_link_libraries(scene-deck-obs INTERFACE
      "${obs_frameworks}/libobs.framework" "${obs_frameworks}/obs-frontend-api.dylib")
  endif()
else()
  add_library(scene-deck-obs INTERFACE)
  find_package(libobs QUIET CONFIG)
  find_package(obs-frontend-api QUIET CONFIG)
  if(TARGET OBS::libobs)
    target_link_libraries(scene-deck-obs INTERFACE OBS::libobs)
  else()
    find_path(SCENE_DECK_OBS_INCLUDE obs.h PATH_SUFFIXES obs REQUIRED)
    find_library(SCENE_DECK_OBS_LIBRARY NAMES obs libobs REQUIRED)
    target_include_directories(scene-deck-obs SYSTEM INTERFACE "${SCENE_DECK_OBS_INCLUDE}")
    target_link_libraries(scene-deck-obs INTERFACE "${SCENE_DECK_OBS_LIBRARY}")
  endif()
  if(TARGET OBS::frontend-api)
    target_link_libraries(scene-deck-obs INTERFACE OBS::frontend-api)
  elseif(TARGET OBS::obs-frontend-api)
    target_link_libraries(scene-deck-obs INTERFACE OBS::obs-frontend-api)
  else()
    find_path(SCENE_DECK_FRONTEND_INCLUDE obs-frontend-api.h PATH_SUFFIXES obs REQUIRED)
    find_library(SCENE_DECK_FRONTEND_LIBRARY NAMES obs-frontend-api REQUIRED)
    target_include_directories(scene-deck-obs SYSTEM INTERFACE "${SCENE_DECK_FRONTEND_INCLUDE}")
    target_link_libraries(scene-deck-obs INTERFACE "${SCENE_DECK_FRONTEND_LIBRARY}")
  endif()
endif()

add_library(obs-scene-deck MODULE
  src/plugin-main.cpp src/scene-deck.cpp src/scene-deck.hpp
  src/scene-preview.cpp src/scene-preview.hpp
  src/scene-audio-meter.cpp src/scene-audio-meter.hpp
  src/native-display.cpp src/native-display.hpp src/obs-compat.hpp
  src/hotkey-edit.cpp src/hotkey-edit.hpp src/core/layout.hpp src/text.hpp)
target_link_libraries(obs-scene-deck PRIVATE scene-deck-obs Qt6::Widgets)
target_compile_definitions(obs-scene-deck PRIVATE
  SCENE_DECK_VERSION="${PROJECT_VERSION}" QT_NO_KEYWORDS)
set_target_properties(obs-scene-deck PROPERTIES PREFIX "" AUTOMOC ON)
if(MSVC)
  target_compile_options(obs-scene-deck PRIVATE /W4 /utf-8 /permissive-)
endif()

if(BUILD_TESTING AND SCENE_DECK_RUNTIME_TESTS AND WIN32 AND SCENE_DECK_OBS_LIBS)
  add_executable(scene-deck-runtime-tests
    tests/runtime-tests.cpp src/scene-deck.cpp src/scene-preview.cpp src/hotkey-edit.cpp
    src/scene-audio-meter.cpp src/scene-audio-meter.hpp src/native-display.cpp
    src/scene-deck.hpp src/scene-preview.hpp)
  set_target_properties(scene-deck-runtime-tests PROPERTIES AUTOMOC ON)
  target_include_directories(scene-deck-runtime-tests PRIVATE src)
  target_link_libraries(scene-deck-runtime-tests PRIVATE scene-deck-obs Qt6::Widgets)
  target_compile_definitions(scene-deck-runtime-tests PRIVATE QT_NO_KEYWORDS
    SCENE_DECK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  if(MSVC)
    target_compile_options(scene-deck-runtime-tests PRIVATE /W4 /utf-8 /permissive-)
  endif()
endif()

if(WIN32)
  install(TARGETS obs-scene-deck RUNTIME DESTINATION obs-scene-deck/bin/64bit
          LIBRARY DESTINATION obs-scene-deck/bin/64bit)
  install(DIRECTORY data/ DESTINATION obs-scene-deck/data)
  install(FILES LICENSE README.md CROSS_PLATFORM.md DESTINATION obs-scene-deck)
elseif(APPLE)
  set_target_properties(obs-scene-deck PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION plugin
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos/Info.plist.in"
    INSTALL_RPATH "@loader_path/../../../Frameworks;@executable_path/../Frameworks")
  file(GLOB_RECURSE locale_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/data/*")
  foreach(locale_file IN LISTS locale_files)
    file(RELATIVE_PATH relative_path "${CMAKE_CURRENT_SOURCE_DIR}/data" "${locale_file}")
    get_filename_component(resource_dir "${relative_path}" DIRECTORY)
    target_sources(obs-scene-deck PRIVATE "${locale_file}")
    set_source_files_properties("${locale_file}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/${resource_dir}")
  endforeach()
  install(TARGETS obs-scene-deck BUNDLE DESTINATION . LIBRARY DESTINATION .)
  install(FILES LICENSE README.md CROSS_PLATFORM.md DESTINATION .)
elseif(SCENE_DECK_SYSTEM_INSTALL)
  include(GNUInstallDirs)
  install(TARGETS obs-scene-deck LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/obs-plugins)
  install(DIRECTORY data/ DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/obs/obs-plugins/obs-scene-deck)
else()
  install(TARGETS obs-scene-deck LIBRARY DESTINATION obs-scene-deck/bin/64bit)
  install(DIRECTORY data/ DESTINATION obs-scene-deck/data)
  install(FILES LICENSE README.md CROSS_PLATFORM.md DESTINATION obs-scene-deck)
endif()

set(CPACK_PACKAGE_NAME "obs-scene-deck")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "obs-scene-deck-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
if(WIN32 OR APPLE)
  set(CPACK_GENERATOR ZIP)
else()
  set(CPACK_GENERATOR TGZ)
endif()
include(CPack)
