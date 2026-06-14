cmake_minimum_required(VERSION 3.21)
project(studio-bondage-club CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(SBC_EMBED_WEB "Embed web/dist into the binary instead of reading from disk" OFF)
option(SBC_BUILD_TESTS "Build unit tests" ON)

# ---- Dependencies (provided by vcpkg toolchain) ----
find_package(Boost REQUIRED COMPONENTS system url)
find_package(OpenSSL REQUIRED)
find_package(leveldb CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(Threads REQUIRED)

# ---- Core library (everything except main.cpp, so tests can link it) ----
file(GLOB_RECURSE SBC_CORE_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM SBC_CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

# Drop platform-specific path implementations we are not building for.
if(NOT WIN32)
  list(FILTER SBC_CORE_SOURCES EXCLUDE REGEX "platform/paths_win\\.cpp$")
endif()
if(NOT ANDROID)
  list(FILTER SBC_CORE_SOURCES EXCLUDE REGEX "platform/paths_android\\.cpp$")
endif()
if(WIN32 OR ANDROID)
  list(FILTER SBC_CORE_SOURCES EXCLUDE REGEX "platform/paths_posix\\.cpp$")
endif()

add_library(sbc_core STATIC ${SBC_CORE_SOURCES})
target_include_directories(sbc_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(sbc_core PUBLIC cxx_std_20)
target_link_libraries(sbc_core PUBLIC
  Boost::system
  Boost::url
  OpenSSL::SSL
  OpenSSL::Crypto
  leveldb::leveldb
  nlohmann_json::nlohmann_json
  spdlog::spdlog
  Threads::Threads)

target_compile_definitions(sbc_core PUBLIC
  BOOST_ASIO_NO_DEPRECATED
  BOOST_BEAST_USE_STD_STRING_VIEW)

if(WIN32)
  target_compile_definitions(sbc_core PUBLIC _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

# ---- Optional: embed web assets ----
if(SBC_EMBED_WEB)
  include(cmake/EmbedWebAssets.cmake)
  embed_web_assets(sbc_core "${CMAKE_CURRENT_SOURCE_DIR}/web/dist")
  target_compile_definitions(sbc_core PUBLIC SBC_EMBED_WEB=1)
endif()

# ---- Executable ----
add_executable(studio-bondage-club "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
target_link_libraries(studio-bondage-club PRIVATE sbc_core)

# ---- Release size optimisations ----
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  # Hide symbols not explicitly exported — enables finer-grained dead stripping.
  target_compile_options(sbc_core PRIVATE -fvisibility=hidden)

  if(UNIX)
    target_compile_options(sbc_core PRIVATE -ffunction-sections -fdata-sections)
  endif()

  if(APPLE)
    # -dead_strip: remove unreferenced sections after LTO; -S: strip debug info.
    target_link_options(studio-bondage-club PRIVATE -Wl,-dead_strip -Wl,-S)
  elseif(UNIX)
    target_link_options(studio-bondage-club PRIVATE -Wl,--gc-sections -Wl,-s)
  endif()

  # Link-time optimisation (inter-procedural). Enable with:
  #   cmake … -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
  # or set it unconditionally here if build time is not a concern.
  if(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
    set_target_properties(sbc_core studio-bondage-club
      PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()
endif()

# ---- Tests ----
if(SBC_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
  enable_testing()
  file(GLOB SBC_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
  if(SBC_TEST_SOURCES)
    add_executable(sbc_tests ${SBC_TEST_SOURCES})
    target_link_libraries(sbc_tests PRIVATE sbc_core)
    add_test(NAME sbc_tests COMMAND sbc_tests)
  endif()
endif()
