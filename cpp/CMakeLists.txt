cmake_minimum_required(VERSION 3.25)

project(Wslc.Testcontainers.Cpp VERSION 0.3.0 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "Wslc.Testcontainers C++ requires Windows (WSL containers are Windows-only).")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# Emit compile_commands.json for clangd and cpp/scripts/Run-ClangTidy.ps1 (Ninja only).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(WSLC_BUILD_TESTS "Build the C++ test suite" ON)
option(WSLC_BUILD_EXAMPLES "Build the C++ examples" ON)
option(WSLC_ENABLE_CODE_ANALYSIS "Run MSVC static analysis (/analyze) while compiling" OFF)
option(WSLC_ENABLE_CLANG_TIDY "Run clang-tidy while compiling (uses cpp/.clang-tidy)" OFF)
option(WSLC_ENABLE_SANITIZERS "Enable AddressSanitizer (and UBSan on Clang) for tests" OFF)

# ---------------------------------------------------------------------------
# Locate the Microsoft.WSL.Containers SDK (wslcsdk.h/.lib/.dll + CMake config).
# The package ships in the NuGet cache once the .NET side has restored it; the
# .packages directory is populated by scripts/Install-WslcSdk.ps1 for clean machines.
# ---------------------------------------------------------------------------
if(NOT WSLC_SDK_ROOT)
  set(_wslc_search_roots "")
  if(DEFINED ENV{NUGET_PACKAGES})
    list(APPEND _wslc_search_roots "$ENV{NUGET_PACKAGES}/microsoft.wsl.containers")
  else()
    list(APPEND _wslc_search_roots "$ENV{USERPROFILE}/.nuget/packages/microsoft.wsl.containers")
  endif()
  list(APPEND _wslc_search_roots "${CMAKE_CURRENT_SOURCE_DIR}/.packages")

  foreach(_root IN LISTS _wslc_search_roots)
    file(GLOB _wslc_configs "${_root}/*/cmake/Microsoft.WSL.ContainersConfig.cmake")
    if(_wslc_configs)
      list(GET _wslc_configs 0 _wslc_config)
      get_filename_component(_wslc_root "${_wslc_config}" DIRECTORY)
      get_filename_component(WSLC_SDK_ROOT "${_wslc_root}" DIRECTORY)
      break()
    endif()
  endforeach()
endif()

if(NOT WSLC_SDK_ROOT)
  message(FATAL_ERROR
    "Microsoft.WSL.Containers SDK not found. Restore the .NET solution (csharp/) or run "
    "cpp/scripts/Install-WslcSdk.ps1, then configure with -DWSLC_SDK_ROOT=<expanded package>.")
endif()

find_package(Microsoft.WSL.Containers REQUIRED CONFIG PATHS "${WSLC_SDK_ROOT}" NO_DEFAULT_PATH)

# ---------------------------------------------------------------------------
# Strict compiler rules (zero warnings, ADR-0005) shared by every target.
# ---------------------------------------------------------------------------
function(wslc_apply_strict_options target)
  target_compile_features(${target} PUBLIC cxx_std_23)
  if(MSVC)
    target_compile_options(${target} PRIVATE
      /W4 /WX
      /permissive-
      /utf-8
      /EHsc
      /Zc:__cplusplus
      /Zc:preprocessor
      /Zc:inline
      /Zc:externConstexpr
      /diagnostics:caret
      /external:anglebrackets
      /external:W0
      /MP)
    if(WSLC_ENABLE_CODE_ANALYSIS)
      target_compile_options(${target} PRIVATE /analyze /analyze:external- /analyze:quiet)
    endif()
  endif()

  if(WSLC_ENABLE_CLANG_TIDY)
    find_program(WSLC_CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
    # MSVC compile lines omit an explicit exceptions flag that clang understands; tell
    # clang-tidy the code builds with exceptions enabled.
    set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY "${WSLC_CLANG_TIDY_EXE};--extra-arg=/EHsc")
  endif()

  if(WSLC_ENABLE_SANITIZERS)
    if(MSVC)
      # MSVC ships AddressSanitizer only (no UBSan); Clang builds get both below.
      # ASan requires debug information for symbolization.
      target_compile_options(${target} PRIVATE /fsanitize=address /Z7)
      target_link_options(${target} PRIVATE /INCREMENTAL:NO /DEBUG)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
      target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
  endif()
endfunction()

# The SDK runtime DLL is not copied by CMake for imported targets; place it next to
# every executable so wslcsdk.dll resolves at run time.
function(wslc_copy_sdk_runtime target)
  get_target_property(_wslc_sdk_dll Microsoft.WSL.Containers::SDK IMPORTED_LOCATION)
  if(_wslc_sdk_dll)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_wslc_sdk_dll}" "$<TARGET_FILE_DIR:${target}>"
      COMMENT "Copying wslcsdk.dll next to ${target}")
  endif()
endfunction()

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------
add_library(wslc STATIC
  src/environment.cpp
  src/exceptions.cpp
  src/exec.cpp
  src/log_line.cpp
  src/log_stream.cpp
  src/platform.cpp
  src/resource_reaper.cpp
  src/testing/log_dumper.cpp
  src/wait.cpp
  src/wsl_container.cpp
  src/wsl_container_builder.cpp
  src/internal/api.cpp
  src/internal/configuration.cpp
  src/internal/container_host.cpp
  src/internal/container_process.cpp
  src/internal/image_resolver.cpp
  src/internal/instance_store.cpp
  src/internal/json.cpp
  src/internal/log_broadcaster.cpp
  src/internal/pipe_reader.cpp
  src/internal/port_mapping.cpp
  src/internal/process_runner.cpp
  src/internal/tcp_http.cpp
  src/internal/text_truncation.cpp
  src/internal/util.cpp)

add_library(wslc::wslc ALIAS wslc)
target_include_directories(wslc PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(wslc PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(wslc PUBLIC Microsoft.WSL.Containers::SDK PRIVATE bcrypt ws2_32 ole32)
wslc_apply_strict_options(wslc)

# ---------------------------------------------------------------------------
# Modules
# ---------------------------------------------------------------------------
add_library(wslc_postgresql STATIC modules/postgresql/postgresql.cpp)
add_library(wslc::postgresql ALIAS wslc_postgresql)
target_include_directories(wslc_postgresql PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/modules/postgresql/include")
target_link_libraries(wslc_postgresql PUBLIC wslc)
wslc_apply_strict_options(wslc_postgresql)

add_library(wslc_redis STATIC modules/redis/redis.cpp)
add_library(wslc::redis ALIAS wslc_redis)
target_include_directories(wslc_redis PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/modules/redis/include")
target_link_libraries(wslc_redis PUBLIC wslc)
wslc_apply_strict_options(wslc_redis)

add_library(wslc_valkey STATIC modules/valkey/valkey.cpp)
add_library(wslc::valkey ALIAS wslc_valkey)
target_include_directories(wslc_valkey PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/modules/valkey/include")
target_link_libraries(wslc_valkey PUBLIC wslc)
wslc_apply_strict_options(wslc_valkey)

add_library(wslc_mariadb STATIC modules/mariadb/mariadb.cpp)
add_library(wslc::mariadb ALIAS wslc_mariadb)
target_include_directories(wslc_mariadb PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/modules/mariadb/include")
target_link_libraries(wslc_mariadb PUBLIC wslc)
wslc_apply_strict_options(wslc_mariadb)

add_library(wslc_rabbitmq STATIC modules/rabbitmq/rabbitmq.cpp)
add_library(wslc::rabbitmq ALIAS wslc_rabbitmq)
target_include_directories(wslc_rabbitmq PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/modules/rabbitmq/include")
target_link_libraries(wslc_rabbitmq PUBLIC wslc)
wslc_apply_strict_options(wslc_rabbitmq)

add_library(wslc_mongodb STATIC modules/mongodb/mongodb.cpp)
add_library(wslc::mongodb ALIAS wslc_mongodb)
target_include_directories(wslc_mongodb PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/modules/mongodb/include")
target_link_libraries(wslc_mongodb PUBLIC wslc)
wslc_apply_strict_options(wslc_mongodb)

add_library(wslc_nats STATIC modules/nats/nats.cpp)
add_library(wslc::nats ALIAS wslc_nats)
target_include_directories(wslc_nats PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/modules/nats/include")
target_link_libraries(wslc_nats PUBLIC wslc)
wslc_apply_strict_options(wslc_nats)

add_library(wslc_mailpit STATIC modules/mailpit/mailpit.cpp)
add_library(wslc::mailpit ALIAS wslc_mailpit)
target_include_directories(wslc_mailpit PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/modules/mailpit/include")
target_link_libraries(wslc_mailpit PUBLIC wslc)
wslc_apply_strict_options(wslc_mailpit)

add_library(wslc_rustfs STATIC modules/rustfs/rustfs.cpp)
add_library(wslc::rustfs ALIAS wslc_rustfs)
target_include_directories(wslc_rustfs PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/modules/rustfs/include")
target_link_libraries(wslc_rustfs PUBLIC wslc)
wslc_apply_strict_options(wslc_rustfs)

add_library(wslc_wiremock STATIC modules/wiremock/wiremock.cpp)
add_library(wslc::wiremock ALIAS wslc_wiremock)
target_include_directories(wslc_wiremock PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/modules/wiremock/include")
target_link_libraries(wslc_wiremock PUBLIC wslc)
wslc_apply_strict_options(wslc_wiremock)
add_library(wslc_qdrant STATIC modules/qdrant/qdrant.cpp)
add_library(wslc::qdrant ALIAS wslc_qdrant)
target_include_directories(wslc_qdrant PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/modules/qdrant/include")
target_link_libraries(wslc_qdrant PUBLIC wslc)
wslc_apply_strict_options(wslc_qdrant)
add_library(wslc_clickhouse STATIC modules/clickhouse/clickhouse.cpp)
add_library(wslc::clickhouse ALIAS wslc_clickhouse)
target_include_directories(wslc_clickhouse PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/modules/clickhouse/include")
target_link_libraries(wslc_clickhouse PUBLIC wslc)
wslc_apply_strict_options(wslc_clickhouse)
add_library(wslc_vault STATIC modules/vault/vault.cpp)
add_library(wslc::vault ALIAS wslc_vault)
target_include_directories(wslc_vault PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/modules/vault/include")
target_link_libraries(wslc_vault PUBLIC wslc)
wslc_apply_strict_options(wslc_vault)
add_library(wslc_keycloak STATIC modules/keycloak/keycloak.cpp)
add_library(wslc::keycloak ALIAS wslc_keycloak)
target_include_directories(wslc_keycloak PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/modules/keycloak/include")
target_link_libraries(wslc_keycloak PUBLIC wslc)
wslc_apply_strict_options(wslc_keycloak)

# ---------------------------------------------------------------------------
# Tests and examples
# ---------------------------------------------------------------------------
if(WSLC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(WSLC_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
