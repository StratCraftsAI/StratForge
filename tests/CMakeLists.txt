# Unit tests (Catch2)
add_executable(stratforge_tests
    test_line.cpp
    test_data_feed.cpp
    test_interval.cpp
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
    test_public_live_engine.cpp
    test_candlestick.cpp
    test_mavp.cpp
    test_coverage_indicators.cpp
    test_coverage_broker.cpp
    test_coverage_engine.cpp
    test_data_robustness.cpp
    test_simd_ops.cpp
    test_performance_gate.cpp
    test_property.cpp
    test_regime_detector_strategy.cpp
    test_regime_entry_strategy.cpp
    test_signal_entry_strategy.cpp
    test_ai_signal_entry_strategy.cpp
    test_ai_libero_strategy.cpp
    test_exit_strategy.cpp
    test_observer_strategy.cpp
    test_stats_adf.cpp
    test_stats_hurst_rs.cpp
    test_stats_garch11.cpp
    test_stats_hmm2.cpp
    test_bar.cpp
    test_increment_types.cpp
    test_increment_batcher.cpp
    test_increment_batcher_baseline.cpp
    test_increment_v2_wire_contract.cpp
    test_performance_increment_batcher.cpp
    test_in_memory_feed.cpp
    test_signal_evaluator.cpp
    test_signal_evaluator_golden.cpp
    test_stats_result.cpp
    test_stats_result_json.cpp
    test_rolling_per_bar.cpp
    test_sf009_phase1.cpp
    test_sf009_phase2.cpp
    test_sf009_phase3.cpp
    test_sf009_phase4.cpp
    test_sf009_phase5.cpp
    test_instrument_venue_currency.cpp
    test_resampled_feed.cpp
    test_multi_clock_cerebro.cpp
    test_backtest_runner_feeds.cpp
    test_ticket791_williams_conformance.cpp
    test_indicator_history_contract.cpp
    test_generated_strategy_warmup.cpp
)

target_link_libraries(stratforge_tests PRIVATE
    stratforge
    Catch2::Catch2WithMain
)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test_live_engine.cpp OR
   EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test_strategy_runtime.cpp)
    find_package(SQLite3 REQUIRED)
    target_link_libraries(stratforge_tests PRIVATE SQLite::SQLite3)
endif()

if(NOT WIN32)
    target_link_libraries(stratforge_tests PRIVATE dl)
endif()

target_compile_definitions(stratforge_tests PRIVATE
    SF_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# -Wno-missing-field-initializers: GCC false positive with designated
# initializers on structs containing std::optional fields with defaults.
# Global -Wall -Wextra -Wpedantic -Werror applied via root CMakeLists.txt.
if(NOT MSVC)
    target_compile_options(stratforge_tests PRIVATE -Wno-missing-field-initializers)
endif()

include(CTest)
include(Catch)
catch_discover_tests(stratforge_tests)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(
    NAME indicator_history_contract_drift
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tools/generate_indicator_history_contract.py
            --check
)

set_target_properties(stratforge_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/tests
)

# This private integration test is intentionally absent from the public
# StratForge sync, whose SDK surface excludes qnx_strategy_sdk.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test_ticket791_validation_sdk.py)
    add_test(
        NAME ticket791_validation_sdk
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_ticket791_validation_sdk.py
    )
endif()
