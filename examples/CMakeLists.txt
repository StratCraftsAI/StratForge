# Example strategies demonstrating StratForge API usage

# Common compiler settings for all examples
function(add_stratforge_example name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE stratforge)
    target_compile_definitions(${name} PRIVATE SF_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

    if(MSVC)
        target_compile_options(${name} PRIVATE /W4)
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/examples
    )
endfunction()

# Add all example executables
add_stratforge_example(sma_crossover)
add_stratforge_example(rsi_mean_reversion)
add_stratforge_example(macd_trend)
add_stratforge_example(bollinger_bands)
add_stratforge_example(multi_timeframe)
add_stratforge_example(pairs_trading)
add_stratforge_example(optimizer_example)

# Add AI-generated examples subdirectory
add_subdirectory(llm_generated)

# Custom target to build all examples
add_custom_target(examples
    DEPENDS
        sma_crossover
        rsi_mean_reversion
        macd_trend
        bollinger_bands
        multi_timeframe
        pairs_trading
        optimizer_example
        llm_examples
)
