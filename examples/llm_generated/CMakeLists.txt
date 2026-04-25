# LLM-Generated Strategies (TICKET_003_10_10 Phase 10.3)

function(add_llm_example name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE stratforge)
    target_compile_definitions(${name} PRIVATE SF_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

    if(MSVC)
        target_compile_options(${name} PRIVATE /W4)
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/examples/llm_generated
    )
endfunction()

# Add all LLM-generated examples
add_llm_example(sma_crossover_with_stop)
add_llm_example(rsi_with_stop)
add_llm_example(macd_trailing_stop)

# Phase 3: End-to-End LLM Generation Tests (TICKET_004)
add_llm_example(phase3_test_1)
add_llm_example(phase3_test_2)
add_llm_example(phase3_test_3)
add_llm_example(phase3_test_4)
add_llm_example(phase3_test_5)
add_llm_example(phase3_test_6)
add_llm_example(phase3_test_7)
add_llm_example(phase3_test_8)
add_llm_example(phase3_test_9)
add_llm_example(phase3_test_10)

# Custom target to build all LLM examples
add_custom_target(llm_examples
    DEPENDS
        sma_crossover_with_stop
        rsi_with_stop
        macd_trailing_stop
        phase3_test_1
        phase3_test_2
        phase3_test_3
        phase3_test_4
        phase3_test_5
        phase3_test_6
        phase3_test_7
        phase3_test_8
        phase3_test_9
        phase3_test_10
)
