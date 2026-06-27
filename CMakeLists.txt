# Top-level CMakeLists for the udex_to_xhand C++ runtime (M5b).
# Builds a single binary `udex_to_xhand` linking against vendor `xhand_control`
# (aarch64 .so in xhand_control_sdk/), yaml-cpp, nlohmann_json, libcurl.
#
# Optional: -DBUILD_TESTS=ON also builds tests/test_mapper_snapshot.

cmake_minimum_required(VERSION 3.10)
project(udex_to_xhand LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(CURL          REQUIRED)
find_package(nlohmann_json 3 REQUIRED)
find_package(yaml-cpp      REQUIRED)
find_package(xhand_control REQUIRED
             HINTS ${CMAKE_SOURCE_DIR}/xhand_control_sdk/share)

# Vendor .so lives outside the system library path → bake RPATH so the binary
# finds it at runtime. -Wl,--disable-new-dtags forces DT_RPATH (propagates to
# transitive libs) instead of DT_RUNPATH (does not). Vendor sample uses the same
# flag; matching it removes a whole class of "linker resolves but loader can't"
# failure modes. See plan §5 "Build setup" for rationale.
set(CMAKE_INSTALL_RPATH ${CMAKE_SOURCE_DIR}/xhand_control_sdk/lib)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

add_executable(udex_to_xhand
    src/main.cpp
    src/cli.cpp
    src/udcap_receiver.cpp
    src/joint_mapper.cpp
    src/xhand_driver.cpp
    src/safety.cpp)

target_include_directories(udex_to_xhand PRIVATE
    src
    ${CMAKE_SOURCE_DIR}/xhand_control_sdk/include)

target_link_libraries(udex_to_xhand PRIVATE
    xhand_control
    CURL::libcurl
    nlohmann_json::nlohmann_json
    yaml-cpp)

target_link_options(udex_to_xhand PRIVATE -Wl,--disable-new-dtags)
target_compile_options(udex_to_xhand PRIVATE -Wall -Wextra -Wpedantic)

# -----------------------------------------------------------------------------
# Optional: snapshot equivalence test (no XHand SDK link — pure mapper math)
# -----------------------------------------------------------------------------
option(BUILD_TESTS "Build snapshot equivalence test" OFF)
if(BUILD_TESTS)
    find_package(OpenSSL REQUIRED)
    add_executable(test_mapper_snapshot
        tests/test_mapper_snapshot.cpp
        src/joint_mapper.cpp
        src/udcap_receiver.cpp)
    target_include_directories(test_mapper_snapshot PRIVATE src)
    target_link_libraries(test_mapper_snapshot PRIVATE
        nlohmann_json::nlohmann_json
        yaml-cpp
        OpenSSL::Crypto)
    target_compile_options(test_mapper_snapshot PRIVATE -Wall -Wextra -Wpedantic)

    # M6: safety primitives unit test. Pure C++ against safety.{hpp,cpp} —
    # does NOT link xhand_control / yaml-cpp / nlohmann_json / OpenSSL, so
    # this target builds and runs on hosts without the vendor aarch64 .so
    # (e.g., darwin/arm64 dev machines).
    add_executable(test_safety
        tests/test_safety.cpp
        src/safety.cpp)
    target_include_directories(test_safety PRIVATE src)
    target_compile_options(test_safety PRIVATE -Wall -Wextra -Wpedantic)
endif()

# -----------------------------------------------------------------------------
# Additive target: LeRobot binary-hand → XHand bridge (2026-06-28).
# Second, fully independent input path. Does NOT modify the udex_to_xhand target
# above; reuses xhand_driver + safety. Reads a realtime LeRobot action UDP/JSON
# stream and drives XHand grasp/open from action[36]/[37].
# See docs/superpowers/specs/2026-06-28-lerobot-binary-hand-sdk-design.md.
# -----------------------------------------------------------------------------
add_executable(lerobot_to_xhand
    src/lerobot_hand_main.cpp
    src/lerobot_receiver.cpp
    src/lerobot_hand_controller.cpp
    src/xhand_driver.cpp
    src/safety.cpp)

target_include_directories(lerobot_to_xhand PRIVATE
    src
    ${CMAKE_SOURCE_DIR}/xhand_control_sdk/include)

target_link_libraries(lerobot_to_xhand PRIVATE
    xhand_control
    CURL::libcurl
    nlohmann_json::nlohmann_json
    yaml-cpp)

target_link_options(lerobot_to_xhand PRIVATE -Wl,--disable-new-dtags)
target_compile_options(lerobot_to_xhand PRIVATE -Wall -Wextra -Wpedantic)
