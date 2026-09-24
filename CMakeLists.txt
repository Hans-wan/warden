cmake_minimum_required(VERSION 3.20)
project(warden CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# warden must never crash or hang: exceptions off, warnings on.
add_compile_options(-Wall -Wextra -Wpedantic -fno-exceptions)

# --- vendored dependencies ----------------------------------------------
# Dependencies are not committed (third_party/ is git-ignored). FetchContent
# pins tag tarballs by URL so a fresh clone reproduces the build. Tarball URLs
# are preferred over GIT_REPOSITORY: codeload is reliably reachable, while git
# protocol can hang for a long time on some networks. If the directory already
# exists (third_party/ placed manually), add_subdirectory reuses it.
# CMake 4.x dropped compatibility policies below 3.5, but yaml-cpp 0.8.0 still
# declares cmake_minimum_required(VERSION 3.4); raise the floor to 3.5 so it
# can be configured.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

include(FetchContent)
# DOWNLOAD_EXTRACT_TIMESTAMP: pinned tag tarballs need no timestamp replay;
# TRUE silences the CMP0135 warning.
if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/googletest/CMakeLists.txt)
  add_subdirectory(third_party/googletest EXCLUDE_FROM_ALL)
else()
  FetchContent_Declare(googletest URL https://codeload.github.com/google/googletest/tar.gz/refs/tags/v1.16.0 DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(googletest)
endif()
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/yaml-cpp/CMakeLists.txt)
  add_subdirectory(third_party/yaml-cpp EXCLUDE_FROM_ALL)
else()
  FetchContent_Declare(yaml-cpp URL https://codeload.github.com/jbeder/yaml-cpp/tar.gz/refs/tags/0.8.0 DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(yaml-cpp)
endif()

# yaml-cpp 0.8.0 cannot compile under -fno-exceptions. Re-enable exceptions for
# this third-party target only. **Ordering matters**: the directory-level
# add_compile_options(-fno-exceptions) above expands before target properties,
# so -fexceptions lands later on the command line and wins on clang — this
# depends on the position of that add_compile_options; do not move it below.
set_property(TARGET yaml-cpp APPEND PROPERTY COMPILE_OPTIONS "-fexceptions")

# ---- yaml-cpp isolation unit ---------------------------------------------
# The warden core forbids try/catch (-fno-exceptions). The only translation
# unit that touches yaml-cpp, yaml_bridge.cpp, re-enables exceptions locally
# and converts YAML exceptions to nullopt. Every other source file stays
# -fno-exceptions.
set_source_files_properties(src/core/policy/yaml_bridge.cpp
  PROPERTIES COMPILE_OPTIONS "-fexceptions")

# ---- factory default policy: embedded at compile time --------------------
# file(READ) pulls the default.yaml text at configure time; configure_file
# renders default_yaml.h.in into the rawDefaultYaml literal header. The
# generated header goes to bin/generated.
file(READ ${CMAKE_SOURCE_DIR}/src/core/policy/default.yaml WARDEN_DEFAULT_YAML)
configure_file(${CMAKE_SOURCE_DIR}/src/core/policy/default_yaml.h.in
               ${CMAKE_BINARY_DIR}/generated/core/policy/default_yaml.h @ONLY)


# ---- warden main library (core + adapters + doctor, no main) ----
file(GLOB_RECURSE WARDEN_SRC CONFIGURE_DEPENDS src/*.cpp)
list(FILTER WARDEN_SRC EXCLUDE REGEX "src/main\\.cpp$")
add_library(warden_core STATIC ${WARDEN_SRC})
target_include_directories(warden_core PUBLIC src)
target_include_directories(warden_core PUBLIC ${CMAKE_BINARY_DIR}/generated)
# yaml-cpp is needed only by policy and is linked statically
target_link_libraries(warden_core PUBLIC yaml-cpp::yaml-cpp)

# ---- executable ----
add_executable(warden src/main.cpp)
target_link_libraries(warden PRIVATE warden_core)
target_include_directories(warden PRIVATE src)

# ---- tests ----
enable_testing()
# Each test binary is one .cpp under tests/; the loop wires up
# add_executable/add_test uniformly.
foreach(t IN ITEMS toolcall json capability_tags capability_bash capability besteffort state attribution policy journal payload diff hooksgen doctor statusline)
  add_executable(test_${t} tests/test_${t}.cpp)
  target_link_libraries(test_${t} PRIVATE warden_core GTest::gtest_main)
  target_include_directories(test_${t} PRIVATE src)
  add_test(NAME ${t} COMMAND test_${t})
endforeach()
