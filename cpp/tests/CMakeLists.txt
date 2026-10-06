include(FetchContent)

# Prefer an installed GoogleTest; fall back to fetching the release tarball.
find_package(GTest QUIET)
if(NOT GTest_FOUND)
  # CI restores the fetched source from actions/cache and points here via
  # WSLC_GTEST_SOURCE_DIR; a cache miss leaves it unset and FetchContent downloads.
  if(EXISTS "$ENV{WSLC_GTEST_SOURCE_DIR}/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_GOOGLETEST "$ENV{WSLC_GTEST_SOURCE_DIR}")
    message(STATUS "GoogleTest not found; using cached source at $ENV{WSLC_GTEST_SOURCE_DIR}")
  else()
    message(STATUS "GoogleTest not found; fetching googletest v1.18.0")
  endif()
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.18.0.tar.gz
    URL_HASH SHA512=BA0F5769CCF34ACF1BC72D1F7E9FFB8202176D02B64F6F3D9047ACCFC0CF9026FF5A653D24935E2705FFF8709566676452616C93CA0CA6277F1E21D79B58A10A
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(googletest)

  # GoogleTest must be compiled with the same sanitizer settings as the tests; otherwise the
  # MSVC annotate_* linker checks fail on the mismatch.
  if(WSLC_ENABLE_SANITIZERS AND MSVC)
    foreach(_gtest_target gtest gtest_main)
      if(TARGET ${_gtest_target})
        target_compile_options(${_gtest_target} PRIVATE /fsanitize=address /Z7 /wd5072)
      endif()
    endforeach()
  endif()
endif()

add_executable(wslc_tests
  atomic_file_tests.cpp
  capture_buffer_tests.cpp
  config_hash_fixture_tests.cpp
  config_hasher_tests.cpp
  container_builder_tests.cpp
  container_lifecycle_tests.cpp
  copy_cancel_tests.cpp
  environment_tests.cpp
  exceptions_tests.cpp
  image_resolver_tests.cpp
  instance_store_tests.cpp
  line_assembler_tests.cpp
  log_broadcaster_tests.cpp
  log_line_tests.cpp
  naming_tests.cpp
  platform_tests.cpp
  port_mapping_tests.cpp
  process_registry_tests.cpp
  resource_reaper_tests.cpp
  stdin_limit_tests.cpp
  wait_strategy_tests.cpp
  integration_tests.cpp
  support/integration.cpp
  support/tiny_http_server.cpp)

target_include_directories(wslc_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/src")
# The shared golden-vector fixture lives at the repository root, outside the CMake tree.
get_filename_component(_wslc_repo_root "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)
target_compile_definitions(wslc_tests PRIVATE WSLC_REPO_ROOT="${_wslc_repo_root}")
target_link_libraries(wslc_tests PRIVATE wslc wslc_postgresql wslc_redis wslc_valkey wslc_mariadb wslc_rabbitmq wslc_mongodb wslc_nats wslc_mailpit wslc_rustfs wslc_wiremock GTest::gtest_main)
wslc_apply_strict_options(wslc_tests)
wslc_copy_sdk_runtime(wslc_tests)

include(GoogleTest)
gtest_discover_tests(wslc_tests DISCOVERY_TIMEOUT 60)
