cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version} LANGUAGES C CXX)

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

include(FetchContent)
FetchContent_Declare(
  signalsmith-linear
  SYSTEM
  GIT_REPOSITORY https://github.com/Signalsmith-Audio/linear.git
  GIT_TAG 7bb1c8f36794d6980080ce817c91f8da8fe56a66
  GIT_SUBMODULES ""
)
FetchContent_Declare(
  signalsmith-stretch
  SYSTEM
  GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
  GIT_TAG 88061fd4eda56a2353c5d65c74aa3f4ccf76f9da
  GIT_SUBMODULES ""
)
FetchContent_MakeAvailable(signalsmith-linear signalsmith-stretch)

add_library(${CMAKE_PROJECT_NAME} MODULE)

# C++17 を要求する
target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_17)

if(MSVC)
  target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE /utf-8 /bigobj)
endif()

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs signalsmith-stretch)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

# src/ と data/ を自動で拾う (追加のたびに CMakeLists.txt を編集しないため)
file(
  GLOB PLUGIN_SOURCES
  CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

file(
  GLOB_RECURSE PLUGIN_RESOURCES
  CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/data/*.effect"
  "${CMAKE_CURRENT_SOURCE_DIR}/data/locale/*.ini"
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${PLUGIN_SOURCES} ${PLUGIN_RESOURCES})

# Solution Explorer のツリー表示を実ディレクトリに揃える
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${PLUGIN_SOURCES} ${PLUGIN_RESOURCES})

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# ビルド直後に cmake --install を実行する (Windows 開発用)
option(ENABLE_POST_BUILD_INSTALL "Run cmake --install after build" OFF)

if(WIN32 AND ENABLE_POST_BUILD_INSTALL)
  add_custom_command(
    TARGET ${CMAKE_PROJECT_NAME}
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --config "$<CONFIG>"
    COMMENT "Installing OBS plugin (cmake --install)"
    VERBATIM
  )
endif()
# The isolated libobs probe has no install rule and never opens an audio device.
option(ENABLE_PITCH_OBS_PROBE "Build the isolated libobs audio integration probe" OFF)
if(ENABLE_PITCH_OBS_PROBE)
  add_executable(pitch-obs-probe tests/pitch-obs-probe.cpp)
  target_compile_features(pitch-obs-probe PRIVATE cxx_std_17)
  target_link_libraries(pitch-obs-probe PRIVATE OBS::libobs)
  if(MSVC)
    target_compile_options(pitch-obs-probe PRIVATE /utf-8)
  endif()
endif()

# 配置検証用モジュールを配布物へ含めない。
option(ENABLE_INSTALL_PROBE "Build the normal-install frontend verification module" OFF)
if(WIN32 AND ENABLE_INSTALL_PROBE)
  find_package(obs-frontend-api REQUIRED)
  find_package(Qt6 REQUIRED COMPONENTS Widgets)
  add_library(pitch-install-probe MODULE tests/install-probe.cpp)
  target_compile_features(pitch-install-probe PRIVATE cxx_std_17)
  target_link_libraries(pitch-install-probe PRIVATE OBS::libobs OBS::obs-frontend-api Qt6::Widgets)
  if(MSVC)
    target_compile_options(pitch-install-probe PRIVATE /utf-8)
  endif()
  set_target_properties(
    pitch-install-probe
    PROPERTIES
      PREFIX ""
      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/install-probe"
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/install-probe"
  )
endif()
