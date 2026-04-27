#pragma once

/// @file talib_compat.hpp
/// @brief TA-Lib compatibility namespace aliases.
/// Maps TA-Lib function names to native indicator classes for familiar usage.
/// Usage: talib::SMA, talib::CDLDOJI, etc.

#include <stratforge/indicators/sma.hpp>
#include <stratforge/indicators/ema.hpp>
#include <stratforge/indicators/wma.hpp>
#include <stratforge/indicators/dema.hpp>
#include <stratforge/indicators/tema.hpp>
#include <stratforge/indicators/kama.hpp>
#include <stratforge/indicators/mavp.hpp>
#include <stratforge/indicators/rsi.hpp>
#include <stratforge/indicators/macd.hpp>
#include <stratforge/indicators/momentum.hpp>
#include <stratforge/indicators/stochastic.hpp>
#include <stratforge/indicators/cci.hpp>
#include <stratforge/indicators/ultimateoscillator.hpp>
#include <stratforge/indicators/awesomeoscillator.hpp>
#include <stratforge/indicators/tsi.hpp>
#include <stratforge/indicators/bollinger.hpp>
#include <stratforge/indicators/atr.hpp>
#include <stratforge/indicators/stddev.hpp>
#include <stratforge/indicators/directionalmovement.hpp>
#include <stratforge/indicators/aroon.hpp>
#include <stratforge/indicators/obv.hpp>
#include <stratforge/indicators/williams.hpp>
#include <stratforge/indicators/psar.hpp>
#include <stratforge/indicators/truerange.hpp>
#include <stratforge/indicators/highest.hpp>
#include <stratforge/indicators/lowest.hpp>
#include <stratforge/indicators/hma.hpp>
#include <stratforge/indicators/smma.hpp>
#include <stratforge/indicators/trix.hpp>
#include <stratforge/indicators/dpo.hpp>
#include <stratforge/indicators/candlestick.hpp>

namespace stratforge::talib {

// --- Moving Averages ---
using SMA   = stratforge::SMA;
using EMA   = stratforge::EMA;
using WMA   = stratforge::WMA;
using DEMA  = stratforge::DEMA;
using TEMA  = stratforge::TEMA;
using KAMA  = stratforge::KAMA;
using HMA   = stratforge::HMA;
using MAVP  = stratforge::MAVP;

// --- Momentum ---
using RSI       = stratforge::RSI;
using MACD      = stratforge::MACD;
using MOM       = stratforge::Momentum;
using ROC       = stratforge::ROC;
using ROC100    = stratforge::ROC100;

// --- Oscillators ---
using STOCH     = stratforge::Stochastic;
using CCI       = stratforge::CCI;
using ULTOSC    = stratforge::UltimateOscillator;
using TSI       = stratforge::TrueStrengthIndicator;
using TRIX      = stratforge::Trix;

// --- Volatility ---
using BBANDS    = stratforge::BollingerBands;
using ATR       = stratforge::ATR;
using STDDEV    = stratforge::StdDev;
using TRANGE    = stratforge::TrueRange;

// --- Trend ---
using ADX       = stratforge::ADX;
using ADXR      = stratforge::ADXR;
using PLUS_DI   = stratforge::PlusDirectionalIndicator;
using MINUS_DI  = stratforge::MinusDirectionalIndicator;
using AROON     = stratforge::Aroon;
using SAR       = stratforge::ParabolicSAR;

// --- Volume ---
using OBV       = stratforge::OBV;

// --- Price ---
using MAX       = stratforge::Highest;
using MIN       = stratforge::Lowest;
using WILLR     = stratforge::WilliamsR;

// --- Candlestick Patterns ---
using CDLDOJI               = stratforge::CDLDoji;
using CDLHAMMER             = stratforge::CDLHammer;
using CDLENGULFING          = stratforge::CDLEngulfing;
using CDLMORNINGSTAR        = stratforge::CDLMorningStar;
using CDLEVENINGSTAR        = stratforge::CDLEveningStar;
using CDLHARAMI             = stratforge::CDLHarami;
using CDLSHOOTINGSTAR       = stratforge::CDLShootingStar;
using CDLHANGINGMAN         = stratforge::CDLHangingMan;
using CDLMARUBOZU           = stratforge::CDLMarubozu;
using CDLSPINNINGTOP        = stratforge::CDLSpinningTop;
using CDL3WHITESOLDIERS     = stratforge::CDLThreeWhiteSoldiers;
using CDL3BLACKCROWS        = stratforge::CDLThreeBlackCrows;
using CDLPIERCING           = stratforge::CDLPiercingLine;
using CDLDARKCLOUDCOVER     = stratforge::CDLDarkCloudCover;
using CDLABANDONEDBABY      = stratforge::CDLAbandonedBaby;
using CDL3INSIDE            = stratforge::CDLThreeInside;
using CDL3OUTSIDE           = stratforge::CDLThreeOutside;
using CDLKICKING            = stratforge::CDLKicking;
using CDLBELTHOLD           = stratforge::CDLBeltHold;
using CDLCOUNTERATTACK      = stratforge::CDLCounterAttack;
using CDLHOMINGPIGEON       = stratforge::CDLHomingPigeon;
using CDLMATCHINGLOW        = stratforge::CDLMatchingLow;
using CDLTASUKIGAP          = stratforge::CDLTasukiGap;
using CDLUPSIDEGAP2CROWS    = stratforge::CDLUpsideGap;

} // namespace stratforge::talib
