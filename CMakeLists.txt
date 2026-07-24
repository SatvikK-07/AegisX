cmake_minimum_required(VERSION 3.20)
project(aegisx VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(AEGISX_ENABLE_SANITIZERS "Enable ASan and UBSan" OFF)
option(AEGISX_ENABLE_CLANG_TIDY "Run clang-tidy when available" OFF)

# Catch2 v3.7.1 is vendored under third_party/Catch2 (Boost Software License
# 1.0). Keeping the test dependency in tree makes a clean configure offline.
set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/Catch2 EXCLUDE_FROM_ALL)

add_library(aegisx
  cpp/src/core.cpp
  cpp/src/nasdaq_itch.cpp
  cpp/src/order_book.cpp
  cpp/src/market_state.cpp
  cpp/src/replay.cpp
  cpp/src/execution.cpp
  cpp/src/risk.cpp
)
target_include_directories(aegisx PUBLIC cpp/include)
set(AEGISX_WARNING_FLAGS -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
target_compile_options(aegisx PRIVATE ${AEGISX_WARNING_FLAGS})
if(AEGISX_ENABLE_SANITIZERS)
  target_compile_options(aegisx PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(aegisx PUBLIC -fsanitize=address,undefined)
endif()

add_executable(aegisx_cli cpp/apps/aegisx_main.cpp)
target_link_libraries(aegisx_cli PRIVATE aegisx)
target_compile_options(aegisx_cli PRIVATE ${AEGISX_WARNING_FLAGS})
set_target_properties(aegisx_cli PROPERTIES OUTPUT_NAME aegisx)
add_executable(aegisx_tests cpp/tests/test_main.cpp)
target_link_libraries(aegisx_tests PRIVATE aegisx Catch2::Catch2WithMain)
target_compile_options(aegisx_tests PRIVATE ${AEGISX_WARNING_FLAGS})
target_compile_definitions(aegisx_tests PRIVATE AEGISX_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

if(AEGISX_ENABLE_CLANG_TIDY)
  find_program(AEGISX_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
  # Analyze production code only. Catch2 is vendored and its assertion macros
  # intentionally perform enum casts that trigger opt-in analyzer diagnostics.
  foreach(target aegisx aegisx_cli)
    set_property(TARGET ${target} PROPERTY CXX_CLANG_TIDY
      "${AEGISX_CLANG_TIDY_EXECUTABLE};--warnings-as-errors=*")
  endforeach()
endif()

enable_testing()
add_test(NAME aegisx_cpp_tests COMMAND aegisx_tests)
add_test(NAME aegisx_fixture_smoke COMMAND aegisx_cli replay --input ${CMAKE_SOURCE_DIR}/data/fixtures/aegisx_itch_sample.itch --output ${CMAKE_BINARY_DIR}/fixture_smoke)
add_test(NAME aegisx_execution_micro_scenarios COMMAND aegisx_tests "[execution]")
add_test(NAME aegisx_risk_reservation_scenarios COMMAND aegisx_tests "[risk]")
