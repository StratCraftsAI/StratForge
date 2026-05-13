# Unit tests (Catch2)
add_executable(stratforge_tests
    test_line.cpp
    test_data_feed.cpp
    test_order.cpp
    test_commission.cpp
    test_broker.cpp
    test_strategy.cpp
    test_sizer.cpp
    test_resample_golden.cpp
    test_cerebro.cpp
    test_analytics_golden.cpp
    test_extended_metrics.cpp
    test_phase7_hardening.cpp
    test_talib_cross_validation.cpp
    test_indicator_basicops.cpp
    test_indicator_more.cpp
    test_indicator_atr.cpp
    test_indicator_phase8.cpp
    test_indicator_phase8_remaining.cpp
    test_indicator_phase8_advanced.cpp
    test_indicator_phase8_tail.cpp
    test_indicator_phase8_final.cpp
    test_golden_loader.cpp
    test_indicator_golden.cpp
    test_advanced_orders.cpp
    test_multidata_golden.cpp
    test_optstrategy.cpp
    test_replay_golden.cpp
    test_e2e_integration.cpp
    test_registry_compile.cpp
    test_indicator_ticket011.cpp
    test_candlestick.cpp
    test_coverage_indicators.cpp
    test_coverage_broker.cpp
    test_coverage_engine.cpp
    test_data_robustness.cpp
    test_simd_ops.cpp
    test_stats_adf.cpp
    test_stats_hurst_rs.cpp
    test_stats_garch11.cpp
    test_stats_hmm2.cpp
)

target_link_libraries(stratforge_tests PRIVATE
    stratforge
    Catch2::Catch2WithMain
)

if(NOT WIN32)
    target_link_libraries(stratforge_tests PRIVATE dl)
endif()

target_compile_definitions(stratforge_tests PRIVATE
    SF_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(stratforge_tests PRIVATE /W4 /WX)
else()
    # -Wno-missing-field-initializers: GCC false positive with designated
    # initializers on structs containing std::optional fields with defaults
    target_compile_options(stratforge_tests PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers)
endif()

include(CTest)
include(Catch)
catch_discover_tests(stratforge_tests)

set_target_properties(stratforge_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/tests
)
