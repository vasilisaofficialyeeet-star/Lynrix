cmake_minimum_required(VERSION 3.20)

cmake_policy(SET CMP0167 NEW)

# Ensure macOS SDK is found for all targets (including FetchContent)
if(APPLE AND NOT CMAKE_OSX_SYSROOT)
    execute_process(
        COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE CMAKE_OSX_SYSROOT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS deployment target")
endif()

project(lynrix VERSION 2.5.0 LANGUAGES CXX)

# ─── Version as compile definition (single source of truth) ─────────────────
add_compile_definitions(LYNRIX_VERSION="${PROJECT_VERSION}")

# ─── C++23 Standard ─────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ─── Build Type ──────────────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ─── PGO + LTO Options ─────────────────────────────────────────────────────
option(ENABLE_LTO "Enable Link-Time Optimization" ON)
option(ENABLE_PGO_GENERATE "Enable PGO instrumentation (generate profile)" OFF)
option(ENABLE_PGO_USE "Enable PGO optimization (use profile)" OFF)
set(PGO_PROFILE_DIR "${CMAKE_BINARY_DIR}/pgo-profiles" CACHE PATH "PGO profile data directory")

# Select target CPU: apple-m4 for latest, apple-m1 for broad compat
set(APPLE_TARGET_CPU "apple-m4" CACHE STRING "Target Apple Silicon CPU (apple-m1/apple-m2/apple-m3/apple-m4)")

if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -mcpu=${APPLE_TARGET_CPU} -ffast-math")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")

    # LTO: ThinLTO for faster link + cross-module optimization
    if(ENABLE_LTO)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto=thin")
        set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -flto=thin")
        message(STATUS "LTO enabled (ThinLTO)")
    endif()

    # PGO generate: instrument binary to collect profile data
    if(ENABLE_PGO_GENERATE)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fprofile-generate=${PGO_PROFILE_DIR}")
        set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -fprofile-generate=${PGO_PROFILE_DIR}")
        message(STATUS "PGO GENERATE mode — profiles will be saved to: ${PGO_PROFILE_DIR}")
    endif()

    # PGO use: optimize using collected profile data
    if(ENABLE_PGO_USE)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fprofile-use=${PGO_PROFILE_DIR}")
        set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -fprofile-use=${PGO_PROFILE_DIR}")
        message(STATUS "PGO USE mode — reading profiles from: ${PGO_PROFILE_DIR}")
    endif()

    # On macOS with newer SDKs, C++ stdlib headers may only exist inside the SDK
    if(CMAKE_OSX_SYSROOT)
        set(_CXX_STDLIB_INCLUDE "${CMAKE_OSX_SYSROOT}/usr/include/c++/v1")
        if(EXISTS "${_CXX_STDLIB_INCLUDE}/atomic")
            add_compile_options(-I${_CXX_STDLIB_INCLUDE})
            message(STATUS "Adding C++ stdlib include: ${_CXX_STDLIB_INCLUDE}")
        endif()
    endif()
else()
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -flto -DNDEBUG")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fsanitize=address,undefined")
endif()

message(STATUS "Target CPU: ${APPLE_TARGET_CPU}")
message(STATUS "Release flags: ${CMAKE_CXX_FLAGS_RELEASE}")

# ─── Dependencies via FetchContent ──────────────────────────────────────────
include(FetchContent)

# Boost (system-installed, header-only for system/asio/beast)
find_package(Boost 1.80 REQUIRED)

# OpenSSL (system-installed)
find_package(OpenSSL REQUIRED)

# spdlog
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.13.0
    GIT_SHALLOW    TRUE
)

# fmt
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        10.2.1
    GIT_SHALLOW    TRUE
)

# simdjson
FetchContent_Declare(
    simdjson
    GIT_REPOSITORY https://github.com/simdjson/simdjson.git
    GIT_TAG        v3.12.2
    GIT_SHALLOW    TRUE
)

# nlohmann/json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(spdlog fmt simdjson nlohmann_json)

# ─── ONNX Runtime (optional) ──────────────────────────────────────────────
option(ENABLE_ONNX "Enable ONNX Runtime for ML inference" ON)

if(ENABLE_ONNX)
    # Try to find onnxruntime installed via Homebrew or system path
    find_library(ONNXRUNTIME_LIB onnxruntime
        PATHS /opt/homebrew/lib /usr/local/lib /usr/lib
        NO_DEFAULT_PATH
    )
    find_path(ONNXRUNTIME_INCLUDE onnxruntime_cxx_api.h
        PATHS /opt/homebrew/include/onnxruntime
              /opt/homebrew/include
              /usr/local/include/onnxruntime
              /usr/local/include
              /usr/include/onnxruntime
        PATH_SUFFIXES onnxruntime/core/session
    )

    if(ONNXRUNTIME_LIB AND ONNXRUNTIME_INCLUDE)
        message(STATUS "ONNX Runtime found: ${ONNXRUNTIME_LIB}")
        message(STATUS "ONNX Runtime includes: ${ONNXRUNTIME_INCLUDE}")
        set(HAS_ONNXRUNTIME TRUE)
    else()
        message(STATUS "ONNX Runtime not found — ONNX inference disabled, using native GRU fallback")
        set(HAS_ONNXRUNTIME FALSE)
    endif()
else()
    set(HAS_ONNXRUNTIME FALSE)
endif()

# ─── Static Library (for macOS app integration) ────────────────────────────
add_library(trading_core STATIC
    src/bridge/trading_core_api.cpp
)

target_include_directories(trading_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(trading_core PUBLIC
    Boost::headers
    OpenSSL::SSL
    OpenSSL::Crypto
    spdlog::spdlog
    fmt::fmt
    simdjson
    nlohmann_json::nlohmann_json
    pthread
)

# ─── Apple Accelerate Framework (NEON/vDSP/BNNS/BLAS) ────────────────────
if(APPLE)
    find_library(ACCELERATE_FRAMEWORK Accelerate)
    if(ACCELERATE_FRAMEWORK)
        target_link_libraries(trading_core PUBLIC ${ACCELERATE_FRAMEWORK})
        message(STATUS "Accelerate framework: ${ACCELERATE_FRAMEWORK}")
    endif()
    find_library(METAL_FRAMEWORK Metal)
    find_library(METALPS_FRAMEWORK MetalPerformanceShaders)
    if(METAL_FRAMEWORK AND METALPS_FRAMEWORK)
        target_link_libraries(trading_core PUBLIC ${METAL_FRAMEWORK} ${METALPS_FRAMEWORK})
        target_compile_definitions(trading_core PUBLIC HAS_METAL=1)
        message(STATUS "Metal + MPS frameworks linked")
    endif()
endif()

if(HAS_ONNXRUNTIME)
    target_include_directories(trading_core PUBLIC ${ONNXRUNTIME_INCLUDE})
    target_link_libraries(trading_core PUBLIC ${ONNXRUNTIME_LIB})
    target_compile_definitions(trading_core PUBLIC HAS_ONNXRUNTIME=1)
endif()

target_compile_options(trading_core PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)

set_target_properties(trading_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME trading_core
)

# ─── Main Executable ────────────────────────────────────────────────────────
add_executable(bybit_hft main.cpp)

target_include_directories(bybit_hft PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(bybit_hft PRIVATE
    Boost::headers
    OpenSSL::SSL
    OpenSSL::Crypto
    spdlog::spdlog
    fmt::fmt
    simdjson
    nlohmann_json::nlohmann_json
    pthread
)

if(HAS_ONNXRUNTIME)
    target_include_directories(bybit_hft PRIVATE ${ONNXRUNTIME_INCLUDE})
    target_link_libraries(bybit_hft PRIVATE ${ONNXRUNTIME_LIB})
    target_compile_definitions(bybit_hft PRIVATE HAS_ONNXRUNTIME=1)
endif()

target_compile_options(bybit_hft PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)

# ─── Unit Tests ──────────────────────────────────────────────────────────────
option(BUILD_TESTS "Build unit tests" ON)

if(BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    # Orderbook tests
    add_executable(test_orderbook tests/test_orderbook.cpp)
    target_include_directories(test_orderbook PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_orderbook PRIVATE GTest::gtest_main)
    target_compile_options(test_orderbook PRIVATE -O2)
    add_test(NAME OrderBookTests COMMAND test_orderbook)

    # All-modules tests
    add_executable(test_all_modules tests/test_all_modules.cpp)
    target_include_directories(test_all_modules PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_all_modules PRIVATE GTest::gtest_main)
    target_compile_options(test_all_modules PRIVATE -O2)
    add_test(NAME AllModuleTests COMMAND test_all_modules)

    # Config & persistence tests
    add_executable(test_config_persistence tests/test_config_persistence.cpp)
    target_include_directories(test_config_persistence PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_config_persistence PRIVATE
        GTest::gtest_main nlohmann_json::nlohmann_json spdlog::spdlog fmt::fmt)
    target_compile_options(test_config_persistence PRIVATE -O2)
    add_test(NAME ConfigPersistenceTests COMMAND test_config_persistence)

    # AI modules tests
    add_executable(test_ai_modules tests/test_ai_modules.cpp)
    target_include_directories(test_ai_modules PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_ai_modules PRIVATE GTest::gtest_main)
    target_compile_options(test_ai_modules PRIVATE -O2)
    add_test(NAME AIModuleTests COMMAND test_ai_modules)

    # Comprehensive tests (backtester, circuit breaker, enhanced risk, E2E pipeline)
    add_executable(test_comprehensive tests/test_comprehensive.cpp)
    target_include_directories(test_comprehensive PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_comprehensive PRIVATE GTest::gtest_main spdlog::spdlog fmt::fmt)
    target_compile_options(test_comprehensive PRIVATE -O2)
    add_test(NAME ComprehensiveTests COMMAND test_comprehensive)

    # Feature engine benchmark
    add_executable(bench_features tests/bench_features.cpp)
    target_include_directories(bench_features PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(bench_features PRIVATE fmt::fmt)
    target_compile_options(bench_features PRIVATE -O3)

    # OrderBook v3 tests
    add_executable(test_orderbook_v3 tests/test_orderbook_v3.cpp)
    target_include_directories(test_orderbook_v3 PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_orderbook_v3 PRIVATE GTest::gtest_main)
    target_compile_options(test_orderbook_v3 PRIVATE -O2)
    add_test(NAME OrderBookV3Tests COMMAND test_orderbook_v3)

    # OrderBook v3 benchmark
    add_executable(bench_orderbook_v3 tests/bench_orderbook_v3.cpp)
    target_include_directories(bench_orderbook_v3 PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(bench_orderbook_v3 PRIVATE fmt::fmt)
    target_compile_options(bench_orderbook_v3 PRIVATE -O3)

    # TSC Clock benchmark
    add_executable(bench_tsc tests/bench_tsc.cpp)
    target_include_directories(bench_tsc PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(bench_tsc PRIVATE fmt::fmt)
    target_compile_options(bench_tsc PRIVATE -O3)

    # Order State Machine + VaR tests
    add_executable(test_order_state_machine tests/test_order_state_machine.cpp)
    target_include_directories(test_order_state_machine PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_order_state_machine PRIVATE GTest::gtest_main)
    target_compile_options(test_order_state_machine PRIVATE -O2)
    if(APPLE AND ACCELERATE_FRAMEWORK)
        target_link_libraries(test_order_state_machine PRIVATE ${ACCELERATE_FRAMEWORK})
    endif()
    add_test(NAME OrderStateMachineTests COMMAND test_order_state_machine)

    # Execution engine benchmark
    add_executable(bench_execution tests/bench_execution.cpp)
    target_include_directories(bench_execution PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(bench_execution PRIVATE fmt::fmt)
    target_compile_options(bench_execution PRIVATE -O3)
    if(APPLE AND ACCELERATE_FRAMEWORK)
        target_link_libraries(bench_execution PRIVATE ${ACCELERATE_FRAMEWORK})
    endif()

    # Hybrid PPO+SAC RL tests
    add_executable(test_ppo_sac_hybrid tests/test_ppo_sac_hybrid.cpp)
    target_include_directories(test_ppo_sac_hybrid PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_ppo_sac_hybrid PRIVATE GTest::gtest_main spdlog::spdlog)
    target_compile_options(test_ppo_sac_hybrid PRIVATE -O2)
    if(APPLE AND ACCELERATE_FRAMEWORK)
        target_link_libraries(test_ppo_sac_hybrid PRIVATE ${ACCELERATE_FRAMEWORK})
    endif()
    add_test(NAME PPOSACHybridTests COMMAND test_ppo_sac_hybrid)

    # RL benchmark
    add_executable(bench_rl tests/bench_rl.cpp)
    target_include_directories(bench_rl PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(bench_rl PRIVATE fmt::fmt spdlog::spdlog)
    target_compile_options(bench_rl PRIVATE -O3)
    if(APPLE AND ACCELERATE_FRAMEWORK)
        target_link_libraries(bench_rl PRIVATE ${ACCELERATE_FRAMEWORK})
    endif()

    # Chaos Engine + PerfSignpost tests
    add_executable(test_chaos_engine tests/test_chaos_engine.cpp)
    target_include_directories(test_chaos_engine PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_chaos_engine PRIVATE GTest::gtest_main spdlog::spdlog)
    target_compile_options(test_chaos_engine PRIVATE -O2)
    if(APPLE AND ACCELERATE_FRAMEWORK)
        target_link_libraries(test_chaos_engine PRIVATE ${ACCELERATE_FRAMEWORK})
    endif()
    add_test(NAME ChaosEngineTests COMMAND test_chaos_engine)

    # Deterministic Replay tests
    add_executable(test_deterministic_replay tests/test_deterministic_replay.cpp)
    target_include_directories(test_deterministic_replay PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_deterministic_replay PRIVATE GTest::gtest_main spdlog::spdlog)
    target_compile_options(test_deterministic_replay PRIVATE -O2)
    if(APPLE AND ACCELERATE_FRAMEWORK)
        target_link_libraries(test_deterministic_replay PRIVATE ${ACCELERATE_FRAMEWORK})
    endif()
    add_test(NAME DeterministicReplayTests COMMAND test_deterministic_replay)

    # Strong Types + Hot Path + Clock Interface tests (standalone, no GTest dep)
    add_executable(test_strong_types tests/test_strong_types.cpp)
    target_include_directories(test_strong_types PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_options(test_strong_types PRIVATE -O2)
    add_test(NAME StrongTypesTests COMMAND test_strong_types)

    # Stage 2 Integration tests (clock injection, templates, typed API)
    add_executable(test_stage2_integration tests/test_stage2_integration.cpp)
    target_include_directories(test_stage2_integration PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_stage2_integration PRIVATE GTest::gtest_main spdlog::spdlog)
    target_compile_options(test_stage2_integration PRIVATE -O2)
    if(APPLE AND ACCELERATE_FRAMEWORK)
        target_link_libraries(test_stage2_integration PRIVATE ${ACCELERATE_FRAMEWORK})
    endif()
    add_test(NAME Stage2IntegrationTests COMMAND test_stage2_integration)

    # Stage 3 Market-Data Correctness tests (gap detection, invalidation, resync)
    add_executable(test_stage3_md_correctness tests/test_stage3_md_correctness.cpp)
    target_include_directories(test_stage3_md_correctness PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_stage3_md_correctness PRIVATE GTest::gtest_main)
    target_compile_options(test_stage3_md_correctness PRIVATE -O2)
    add_test(NAME Stage3MDCorrectnessTests COMMAND test_stage3_md_correctness)

    # Stage 4 Hot-Path Cleanup tests (deferred queue, counters, budget, layout)
    add_executable(test_stage4_hot_path tests/test_stage4_hot_path.cpp)
    target_include_directories(test_stage4_hot_path PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_stage4_hot_path PRIVATE GTest::gtest_main)
    target_compile_options(test_stage4_hot_path PRIVATE -O2)
    add_test(NAME Stage4HotPathTests COMMAND test_stage4_hot_path)

    # Stage 5 Semantic Type Safety tests
    add_executable(test_stage5_semantic_types tests/test_stage5_semantic_types.cpp)
    target_include_directories(test_stage5_semantic_types PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_stage5_semantic_types PRIVATE GTest::gtest_main spdlog::spdlog fmt::fmt)
    target_compile_options(test_stage5_semantic_types PRIVATE -O2)
    add_test(NAME Stage5SemanticTypeTests COMMAND test_stage5_semantic_types)

    # Stage 6 System Control FSM tests
    add_executable(test_stage6_system_control tests/test_stage6_system_control.cpp)
    target_include_directories(test_stage6_system_control PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_stage6_system_control PRIVATE GTest::gtest_main spdlog::spdlog fmt::fmt)
    target_compile_options(test_stage6_system_control PRIVATE -O2)
    add_test(NAME Stage6SystemControlTests COMMAND test_stage6_system_control)

    # Stage 6 Control Plane benchmark
    add_executable(bench_stage6_control tests/bench_stage6_control.cpp)
    target_include_directories(bench_stage6_control PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(bench_stage6_control PRIVATE spdlog::spdlog fmt::fmt)
    target_compile_options(bench_stage6_control PRIVATE -O3)
endif()

# ─── Fuzzing Target (requires -DENABLE_FUZZING=ON) ──────────────────────────
option(ENABLE_FUZZING "Build fuzzing targets" OFF)
if(ENABLE_FUZZING)
    add_executable(fuzz_orderbook_parser tests/fuzz_orderbook_parser.cpp)
    target_include_directories(fuzz_orderbook_parser PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_options(fuzz_orderbook_parser PRIVATE -g -O1 -fsanitize=fuzzer,address,undefined)
    target_link_options(fuzz_orderbook_parser PRIVATE -fsanitize=fuzzer,address,undefined)
    if(APPLE AND ACCELERATE_FRAMEWORK)
        target_link_libraries(fuzz_orderbook_parser PRIVATE ${ACCELERATE_FRAMEWORK})
    endif()
endif()
