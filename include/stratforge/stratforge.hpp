#pragma once

// Core
#include <stratforge/analytics/extended_metrics.hpp>
#include <stratforge/bar.hpp>
#include <stratforge/core/line.hpp>
#include <stratforge/core/line_series.hpp>
#include <stratforge/core/params.hpp>
#include <stratforge/core/period_validate.hpp>
#include <stratforge/core/transparent_hash.hpp>

// Data
#include <stratforge/data/timeframe.hpp>
#include <stratforge/data/data_feed.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/data/resampler.hpp>
#include <stratforge/data/replay.hpp>

// Broker
#include <stratforge/broker/order.hpp>
#include <stratforge/broker/commission.hpp>
#include <stratforge/broker/position.hpp>
#include <stratforge/broker/trade.hpp>
#include <stratforge/broker/broker.hpp>
#include <stratforge/broker/sizer.hpp>

// Strategy
#include <stratforge/strategy/signal.hpp>
#include <stratforge/strategy/strategy.hpp>
#include <stratforge/strategy/entry_signal.hpp>
#include <stratforge/strategy/regime_detector_strategy.hpp>
#include <stratforge/strategy/regime_entry_strategy.hpp>
#include <stratforge/strategy/signal_entry_strategy.hpp>
#include <stratforge/strategy/ai_signal_entry_strategy.hpp>
#include <stratforge/strategy/ai_libero_strategy.hpp>
#include <stratforge/strategy/exit_strategy.hpp>
#include <stratforge/strategy/observer_strategy.hpp>

// Indicators
#include <stratforge/indicators/accum.hpp>
#include <stratforge/indicators/candlestick.hpp>
#include <stratforge/indicators/adaptive.hpp>
#include <stratforge/indicators/average.hpp>
#include <stratforge/indicators/awesomeoscillator.hpp>
#include <stratforge/indicators/bollinger.hpp>
#include <stratforge/indicators/bollingerpct.hpp>
#include <stratforge/indicators/atr.hpp>
#include <stratforge/indicators/aroon.hpp>
#include <stratforge/indicators/indicator.hpp>
#include <stratforge/indicators/cci.hpp>
#include <stratforge/indicators/crossover.hpp>
#include <stratforge/indicators/dema.hpp>
#include <stratforge/indicators/downday.hpp>
#include <stratforge/indicators/downmove.hpp>
#include <stratforge/indicators/directionalmovement.hpp>
#include <stratforge/indicators/dma.hpp>
#include <stratforge/indicators/dpo.hpp>
#include <stratforge/indicators/dv2.hpp>
#include <stratforge/indicators/ema.hpp>
#include <stratforge/indicators/exponentialsmoothing.hpp>
#include <stratforge/indicators/heikinashi.hpp>
#include <stratforge/indicators/hadelta.hpp>
#include <stratforge/indicators/highest.hpp>
#include <stratforge/indicators/hma.hpp>
#include <stratforge/indicators/hurst.hpp>
#include <stratforge/indicators/ichimoku.hpp>
#include <stratforge/indicators/kama.hpp>
#include <stratforge/indicators/kst.hpp>
#include <stratforge/indicators/laguerre.hpp>
#include <stratforge/indicators/lowest.hpp>
#include <stratforge/indicators/macd.hpp>
#include <stratforge/indicators/meandeviation.hpp>
#include <stratforge/indicators/momentum.hpp>
#include <stratforge/indicators/obv.hpp>
#include <stratforge/indicators/ols.hpp>
#include <stratforge/indicators/envelope.hpp>
#include <stratforge/indicators/findindex.hpp>
#include <stratforge/indicators/oscillator.hpp>
#include <stratforge/indicators/oscillator_extra.hpp>
#include <stratforge/indicators/logicaln.hpp>
#include <stratforge/indicators/statistics.hpp>
#include <stratforge/indicators/percentchange.hpp>
#include <stratforge/indicators/pctrank.hpp>
#include <stratforge/indicators/pattern.hpp>
#include <stratforge/indicators/periodn.hpp>
#include <stratforge/indicators/pivotpoint.hpp>
#include <stratforge/indicators/prettygoodoscillator.hpp>
#include <stratforge/indicators/psar.hpp>
#include <stratforge/indicators/rsi.hpp>
#include <stratforge/indicators/sma.hpp>
#include <stratforge/indicators/smma.hpp>
#include <stratforge/indicators/stddev.hpp>
#include <stratforge/indicators/stochastic.hpp>
#include <stratforge/indicators/sumn.hpp>
#include <stratforge/indicators/tema.hpp>
#include <stratforge/indicators/tsi.hpp>
#include <stratforge/indicators/trix.hpp>
#include <stratforge/indicators/trend.hpp>
#include <stratforge/indicators/truehigh.hpp>
#include <stratforge/indicators/truelow.hpp>
#include <stratforge/indicators/truerange.hpp>
#include <stratforge/indicators/ultimateoscillator.hpp>
#include <stratforge/indicators/upday.hpp>
#include <stratforge/indicators/upmove.hpp>
#include <stratforge/indicators/volume.hpp>
#include <stratforge/indicators/volatility.hpp>
#include <stratforge/indicators/vortex.hpp>
#include <stratforge/indicators/williams.hpp>
#include <stratforge/indicators/wma.hpp>
#include <stratforge/indicators/weightedaverage.hpp>
#include <stratforge/indicators/zerolag.hpp>
#include <stratforge/indicators/zlema.hpp>
#include <stratforge/indicators/mavp.hpp>
#include <stratforge/indicators/talib_compat.hpp>

// Phase 1: TICKET_SF009 — TA-Lib deferred indicators
#include <stratforge/indicators/aberration.hpp>
#include <stratforge/indicators/accbands.hpp>
#include <stratforge/indicators/adosc.hpp>
#include <stratforge/indicators/alma.hpp>
#include <stratforge/indicators/amat.hpp>
#include <stratforge/indicators/aobv.hpp>
#include <stratforge/indicators/bias.hpp>
#include <stratforge/indicators/cdl_inside.hpp>
#include <stratforge/indicators/cdl_z.hpp>
#include <stratforge/indicators/cfo.hpp>
#include <stratforge/indicators/chop.hpp>
#include <stratforge/indicators/cksp.hpp>
#include <stratforge/indicators/cti.hpp>
#include <stratforge/indicators/decay.hpp>
#include <stratforge/indicators/decreasing.hpp>
#include <stratforge/indicators/dx.hpp>
#include <stratforge/indicators/ebsw.hpp>
#include <stratforge/indicators/efficiency_ratio.hpp>
#include <stratforge/indicators/efi.hpp>
#include <stratforge/indicators/entropy.hpp>
#include <stratforge/indicators/fwma.hpp>
#include <stratforge/indicators/hwc.hpp>
#include <stratforge/indicators/increasing.hpp>
#include <stratforge/indicators/inertia.hpp>
#include <stratforge/indicators/jma.hpp>
#include <stratforge/indicators/kurtosis.hpp>
#include <stratforge/indicators/log_return.hpp>
#include <stratforge/indicators/long_run.hpp>
#include <stratforge/indicators/massi.hpp>
#include <stratforge/indicators/mcgd.hpp>
#include <stratforge/indicators/median.hpp>
#include <stratforge/indicators/nvi.hpp>
#include <stratforge/indicators/pdist.hpp>
#include <stratforge/indicators/psl.hpp>
#include <stratforge/indicators/pvi.hpp>
#include <stratforge/indicators/pvol.hpp>
#include <stratforge/indicators/pvr.hpp>
#include <stratforge/indicators/pwma.hpp>
#include <stratforge/indicators/qqe.hpp>
#include <stratforge/indicators/qstick.hpp>
#include <stratforge/indicators/quantile.hpp>
#include <stratforge/indicators/rvi.hpp>
#include <stratforge/indicators/short_run.hpp>
#include <stratforge/indicators/sinwma.hpp>
#include <stratforge/indicators/skew.hpp>
#include <stratforge/indicators/squeeze.hpp>
#include <stratforge/indicators/ssf.hpp>
#include <stratforge/indicators/stc.hpp>
#include <stratforge/indicators/swma.hpp>
#include <stratforge/indicators/td_seq.hpp>
#include <stratforge/indicators/thermo.hpp>
#include <stratforge/indicators/tos_stdevall.hpp>
#include <stratforge/indicators/tsignals.hpp>
#include <stratforge/indicators/vp.hpp>
#include <stratforge/indicators/vwma.hpp>
#include <stratforge/indicators/xsignals.hpp>
#include <stratforge/indicators/zscore.hpp>

// Phase 2: TICKET_SF009 — Alpha158 deferred indicators
#include <stratforge/indicators/idxmax.hpp>
#include <stratforge/indicators/idxmin.hpp>
#include <stratforge/indicators/imxd.hpp>
#include <stratforge/indicators/rolling_residual.hpp>
#include <stratforge/indicators/rolling_rsquared.hpp>
#include <stratforge/indicators/cntp.hpp>
#include <stratforge/indicators/cntn.hpp>
#include <stratforge/indicators/cntd.hpp>
#include <stratforge/indicators/sump.hpp>
#include <stratforge/indicators/sum_neg_return.hpp>
#include <stratforge/indicators/sumd.hpp>
#include <stratforge/indicators/vsump.hpp>
#include <stratforge/indicators/vsumn.hpp>
#include <stratforge/indicators/vsumd.hpp>
#include <stratforge/indicators/wvma.hpp>
#include <stratforge/indicators/alpha158_vwap.hpp>

// Phase 3: TICKET_SF009 — Alpha101 (WorldQuant 101 factors)
#include <stratforge/indicators/alpha101_ops.hpp>
#include <stratforge/indicators/alpha101.hpp>

// Phase 4: TICKET_SF009 — Alpha191 (Guotai Junan 191 factors)
#include <stratforge/indicators/alpha191_ops.hpp>
#include <stratforge/indicators/alpha191.hpp>

// Phase 5: TICKET_SF009 — JKP (Jensen, Kelly & Pedersen 25 academic factors)
#include <stratforge/indicators/jkp_ops.hpp>
#include <stratforge/indicators/jkp.hpp>

// Analyzers
#include <stratforge/analyzers/analyzer.hpp>
#include <stratforge/analyzers/sharpe_ratio.hpp>
#include <stratforge/analyzers/drawdown.hpp>
#include <stratforge/analyzers/trade_analyzer.hpp>
#include <stratforge/analyzers/returns.hpp>

// Observers
#include <stratforge/observers/observer.hpp>
#include <stratforge/observers/buy_sell.hpp>
#include <stratforge/observers/cash_value.hpp>
#include <stratforge/observers/value.hpp>
// -A: POD wire contract for progressive backtest streaming.
// The IncrementBatcher observer (789-B) and the future AsyncIncrementSink
// both project into IncrementSnapshot.
#include <stratforge/observers/increment_types.hpp>
// -B: Built-in batching Observer that streams IncrementSnapshots
// to a user-supplied flush callback during Cerebro::run().
#include <stratforge/observers/increment_batcher.hpp>

// Engine
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/engine/optimizer.hpp>

// Statistical operators — batch APIs for Signal Discovery
// hypothesis tests. Distinct from streaming indicator APIs above.
#include <stratforge/stats/adf.hpp>
#include <stratforge/stats/hurst_rs.hpp>
#include <stratforge/stats/garch11.hpp>
#include <stratforge/stats/hmm2.hpp>
// : HypothesisResult POD + alias headers for LLM-friendly spellings.
// : result_json.hpp — canonical wire format owned by StratForge.
#include <stratforge/stats/result.hpp>
#include <stratforge/stats/result_json.hpp>
#include <stratforge/stats/garch.hpp>
#include <stratforge/stats/hmm.hpp>
#include <stratforge/stats/hurst.hpp>
