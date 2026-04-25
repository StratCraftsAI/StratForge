# Synthetic data generator
add_executable(gen_bench_data
    gen_dataset.cpp
)

if(MSVC)
    target_compile_options(gen_bench_data PRIVATE /W4)
else()
    target_compile_options(gen_bench_data PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(gen_bench_data PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/benchmarks
)

# Indicator benchmarks
add_executable(indicator_benchmarks
    indicator_benchmarks.cpp
)

target_link_libraries(indicator_benchmarks PRIVATE
    stratforge
)

target_compile_definitions(indicator_benchmarks PRIVATE
    SF_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(indicator_benchmarks PRIVATE /W4)
else()
    target_compile_options(indicator_benchmarks PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(indicator_benchmarks PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/benchmarks
)

# Strategy benchmarks
add_executable(strategy_benchmarks
    strategy_benchmarks.cpp
)

target_link_libraries(strategy_benchmarks PRIVATE
    stratforge
)

target_compile_definitions(strategy_benchmarks PRIVATE
    SF_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(strategy_benchmarks PRIVATE /W4)
else()
    target_compile_options(strategy_benchmarks PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(strategy_benchmarks PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/benchmarks
)

# Optimization benchmarks
add_executable(optimization_benchmarks
    optimization_benchmarks.cpp
)

target_link_libraries(optimization_benchmarks PRIVATE
    stratforge
)

target_compile_definitions(optimization_benchmarks PRIVATE
    SF_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(optimization_benchmarks PRIVATE /W4)
else()
    target_compile_options(optimization_benchmarks PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(optimization_benchmarks PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/benchmarks
)

# Memory benchmarks (allocation audit)
add_executable(memory_benchmarks
    memory_benchmarks.cpp
)

target_link_libraries(memory_benchmarks PRIVATE
    stratforge
)

target_compile_definitions(memory_benchmarks PRIVATE
    SF_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(memory_benchmarks PRIVATE /W4)
else()
    target_compile_options(memory_benchmarks PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(memory_benchmarks PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/benchmarks
)
