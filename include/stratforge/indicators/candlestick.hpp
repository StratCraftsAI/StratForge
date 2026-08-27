#pragma once

#include <stratforge/indicators/candlestick_common.hpp>
#include <stratforge/indicators/indicator.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace stratforge {
namespace detail {

enum class CandlePattern {
    Doji, Hammer, Engulfing, MorningStar, EveningStar, Harami,
    ShootingStar, HangingMan, Marubozu, SpinningTop, ThreeWhiteSoldiers,
    ThreeBlackCrows, Piercing, DarkCloudCover, AbandonedBaby, ThreeInside,
    ThreeOutside, Kicking, BeltHold, CounterAttack, HomingPigeon, MatchingLow,
    TasukiGap, UpsideGapTwoCrows,
    TwoCrows, ThreeLineStrike, ThreeStarsInSouth, AdvanceBlock, Breakaway,
    ClosingMarubozu, ConcealingBabySwallow, DojiStar, DragonflyDoji,
    EveningDojiStar, GapSideSideWhite, GravestoneDoji, HaramiCross, HighWave,
    Hikkake, ModifiedHikkake, IdenticalThreeCrows, InNeck, InvertedHammer,
    KickingByLength, LadderBottom, LongLeggedDoji, LongLine, MatHold,
    MorningDojiStar, OnNeck, RickshawMan, RiseFallThreeMethods,
    SeparatingLines, ShortLine, StalledPattern, StickSandwich, Takuri,
    Thrusting, Tristar, UniqueThreeRiver, XSideGapThreeMethods,
};

class CandleSeries {
public:
    CandleSeries(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close,
                 candlestick::Settings settings)
        : open_(open), high_(high), low_(low), close_(close),
          settings_(std::move(settings)) {}

    [[nodiscard]] candlestick::Candle at(std::size_t i) const noexcept {
        return {open_.data()[i], high_.data()[i], low_.data()[i], close_.data()[i]};
    }
    [[nodiscard]] double open(std::size_t i) const noexcept { return open_.data()[i]; }
    [[nodiscard]] double high(std::size_t i) const noexcept { return high_.data()[i]; }
    [[nodiscard]] double low(std::size_t i) const noexcept { return low_.data()[i]; }
    [[nodiscard]] double close(std::size_t i) const noexcept { return close_.data()[i]; }
    [[nodiscard]] int color(std::size_t i) const noexcept { return candlestick::color(at(i)); }
    [[nodiscard]] double body(std::size_t i) const noexcept { return candlestick::real_body(at(i)); }
    [[nodiscard]] double upper(std::size_t i) const noexcept { return candlestick::upper_shadow(at(i)); }
    [[nodiscard]] double lower(std::size_t i) const noexcept { return candlestick::lower_shadow(at(i)); }
    [[nodiscard]] bool body_gap_up(std::size_t i, std::size_t previous) const noexcept {
        return candlestick::real_body_gap_up(at(i), at(previous));
    }
    [[nodiscard]] bool body_gap_down(std::size_t i, std::size_t previous) const noexcept {
        return candlestick::real_body_gap_down(at(i), at(previous));
    }
    [[nodiscard]] bool gap_up(std::size_t i, std::size_t previous) const noexcept {
        return candlestick::candle_gap_up(at(i), at(previous));
    }
    [[nodiscard]] bool gap_down(std::size_t i, std::size_t previous) const noexcept {
        return candlestick::candle_gap_down(at(i), at(previous));
    }

    /// TA-Lib candle averages at bar i contain ranges from [i-period, i).
    [[nodiscard]] double average(candlestick::SettingType type,
                                 std::size_t i) const noexcept {
        const auto setting = settings_.get(type);
        if (setting.average_period == 0) {
            return candlestick::average(setting, 0.0, at(i));
        }
        double total = 0.0;
        for (std::size_t j = i - setting.average_period; j < i; ++j) {
            total += candlestick::range(setting.range_type, at(j));
        }
        return candlestick::average(setting, total, at(i));
    }

    [[nodiscard]] const candlestick::Settings& settings() const noexcept { return settings_; }
    [[nodiscard]] std::size_t index() const noexcept { return close_.index(); }
    [[nodiscard]] std::size_t size() const noexcept { return close_.size(); }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    candlestick::Settings settings_;
};

[[nodiscard]] inline std::size_t candle_pattern_lookback(
    CandlePattern pattern, const candlestick::Settings& settings) {
    using candlestick::SettingType;
    const auto p = [&settings](SettingType type) {
        return settings.get(type).average_period;
    };
    switch (pattern) {
    case CandlePattern::Doji: return p(SettingType::BodyDoji);
    case CandlePattern::Hammer:
    case CandlePattern::HangingMan:
        return std::max({p(SettingType::BodyShort), p(SettingType::ShadowLong),
                         p(SettingType::ShadowVeryShort), p(SettingType::Near)}) + 1;
    case CandlePattern::Engulfing: return 2;
    case CandlePattern::MorningStar:
    case CandlePattern::EveningStar:
        return std::max(p(SettingType::BodyShort), p(SettingType::BodyLong)) + 2;
    case CandlePattern::Harami:
    case CandlePattern::HomingPigeon:
        return std::max(p(SettingType::BodyShort), p(SettingType::BodyLong)) + 1;
    case CandlePattern::ShootingStar:
        return std::max({p(SettingType::BodyShort), p(SettingType::ShadowLong),
                         p(SettingType::ShadowVeryShort)}) + 1;
    case CandlePattern::Marubozu:
    case CandlePattern::BeltHold:
        return std::max(p(SettingType::BodyLong), p(SettingType::ShadowVeryShort));
    case CandlePattern::SpinningTop: return p(SettingType::BodyShort);
    case CandlePattern::ThreeWhiteSoldiers:
        return std::max({p(SettingType::ShadowVeryShort), p(SettingType::BodyShort),
                         p(SettingType::Far), p(SettingType::Near)}) + 2;
    case CandlePattern::ThreeBlackCrows:
        return p(SettingType::ShadowVeryShort) + 3;
    case CandlePattern::Piercing:
    case CandlePattern::DarkCloudCover:
        return p(SettingType::BodyLong) + 1;
    case CandlePattern::AbandonedBaby:
        return std::max({p(SettingType::BodyDoji), p(SettingType::BodyLong),
                         p(SettingType::BodyShort)}) + 2;
    case CandlePattern::ThreeInside:
        return std::max(p(SettingType::BodyShort), p(SettingType::BodyLong)) + 2;
    case CandlePattern::ThreeOutside: return 3;
    case CandlePattern::Kicking:
        return std::max(p(SettingType::ShadowVeryShort), p(SettingType::BodyLong)) + 1;
    case CandlePattern::CounterAttack:
        return std::max(p(SettingType::Equal), p(SettingType::BodyLong)) + 1;
    case CandlePattern::MatchingLow: return p(SettingType::Equal) + 1;
    case CandlePattern::TasukiGap: return p(SettingType::Near) + 2;
    case CandlePattern::UpsideGapTwoCrows:
        return std::max(p(SettingType::BodyShort), p(SettingType::BodyLong)) + 2;
    case CandlePattern::TwoCrows: return p(SettingType::BodyLong) + 2;
    case CandlePattern::ThreeLineStrike: return p(SettingType::Near) + 3;
    case CandlePattern::ThreeStarsInSouth:
        return std::max({p(SettingType::ShadowVeryShort), p(SettingType::ShadowLong),
                         p(SettingType::BodyLong), p(SettingType::BodyShort)}) + 2;
    case CandlePattern::AdvanceBlock:
        return std::max({p(SettingType::ShadowLong), p(SettingType::ShadowShort),
                         p(SettingType::Far), p(SettingType::Near),
                         p(SettingType::BodyLong)}) + 2;
    case CandlePattern::Breakaway: return p(SettingType::BodyLong) + 4;
    case CandlePattern::ClosingMarubozu:
        return std::max(p(SettingType::BodyLong), p(SettingType::ShadowVeryShort));
    case CandlePattern::ConcealingBabySwallow:
        return p(SettingType::ShadowVeryShort) + 3;
    case CandlePattern::DojiStar:
    case CandlePattern::HaramiCross:
        return std::max(p(SettingType::BodyDoji), p(SettingType::BodyLong)) + 1;
    case CandlePattern::DragonflyDoji:
    case CandlePattern::GravestoneDoji:
        return std::max(p(SettingType::BodyDoji), p(SettingType::ShadowVeryShort));
    case CandlePattern::EveningDojiStar:
    case CandlePattern::MorningDojiStar:
        return std::max({p(SettingType::BodyDoji), p(SettingType::BodyLong),
                         p(SettingType::BodyShort)}) + 2;
    case CandlePattern::GapSideSideWhite:
        return std::max(p(SettingType::Near), p(SettingType::Equal)) + 2;
    case CandlePattern::HighWave:
        return std::max(p(SettingType::BodyShort), p(SettingType::ShadowVeryLong));
    case CandlePattern::Hikkake: return 5;
    case CandlePattern::ModifiedHikkake: return std::max<std::size_t>(1, p(SettingType::Near)) + 5;
    case CandlePattern::IdenticalThreeCrows:
        return std::max(p(SettingType::ShadowVeryShort), p(SettingType::Equal)) + 2;
    case CandlePattern::InNeck:
    case CandlePattern::OnNeck:
    case CandlePattern::Thrusting:
        return std::max(p(SettingType::Equal), p(SettingType::BodyLong)) + 1;
    case CandlePattern::InvertedHammer:
        return std::max({p(SettingType::BodyShort), p(SettingType::ShadowLong),
                         p(SettingType::ShadowVeryShort)}) + 1;
    case CandlePattern::KickingByLength:
        return std::max(p(SettingType::ShadowVeryShort), p(SettingType::BodyLong)) + 1;
    case CandlePattern::LadderBottom: return p(SettingType::ShadowVeryShort) + 4;
    case CandlePattern::LongLeggedDoji:
        return std::max(p(SettingType::BodyDoji), p(SettingType::ShadowLong));
    case CandlePattern::LongLine:
        return std::max(p(SettingType::BodyLong), p(SettingType::ShadowShort));
    case CandlePattern::MatHold:
    case CandlePattern::RiseFallThreeMethods:
        return std::max(p(SettingType::BodyShort), p(SettingType::BodyLong)) + 4;
    case CandlePattern::RickshawMan:
        return std::max({p(SettingType::BodyDoji), p(SettingType::ShadowLong),
                         p(SettingType::Near)});
    case CandlePattern::SeparatingLines:
        return std::max({p(SettingType::ShadowVeryShort), p(SettingType::BodyLong),
                         p(SettingType::Equal)}) + 1;
    case CandlePattern::ShortLine:
        return std::max(p(SettingType::BodyShort), p(SettingType::ShadowShort));
    case CandlePattern::StalledPattern:
        return std::max({p(SettingType::BodyLong), p(SettingType::BodyShort),
                         p(SettingType::ShadowVeryShort), p(SettingType::Near)}) + 2;
    case CandlePattern::StickSandwich: return p(SettingType::Equal) + 2;
    case CandlePattern::Takuri:
        return std::max({p(SettingType::BodyDoji), p(SettingType::ShadowVeryShort),
                         p(SettingType::ShadowVeryLong)});
    case CandlePattern::Tristar: return p(SettingType::BodyDoji) + 2;
    case CandlePattern::UniqueThreeRiver:
        return std::max(p(SettingType::BodyShort), p(SettingType::BodyLong)) + 2;
    case CandlePattern::XSideGapThreeMethods: return 2;
    }
    return 0;
}

[[nodiscard]] inline double evaluate_candle_pattern(
    CandlePattern pattern, const CandleSeries& s, std::size_t i,
    double penetration) noexcept {
    using candlestick::SettingType;
    const auto top = [&s](std::size_t j) { return std::max(s.open(j), s.close(j)); };
    const auto bottom = [&s](std::size_t j) { return std::min(s.open(j), s.close(j)); };

    switch (pattern) {
    case CandlePattern::Doji:
        return s.body(i) <= s.average(SettingType::BodyDoji, i) ? 100.0 : 0.0;
    case CandlePattern::Hammer:
        return s.body(i) < s.average(SettingType::BodyShort, i) &&
               s.lower(i) > s.average(SettingType::ShadowLong, i) &&
               s.upper(i) < s.average(SettingType::ShadowVeryShort, i) &&
               bottom(i) <= s.low(i - 1) + s.average(SettingType::Near, i - 1)
            ? 100.0 : 0.0;
    case CandlePattern::Engulfing: {
        const bool match =
            (s.color(i) == 1 && s.color(i - 1) == -1 &&
             ((s.close(i) >= s.open(i - 1) && s.open(i) < s.close(i - 1)) ||
              (s.close(i) > s.open(i - 1) && s.open(i) <= s.close(i - 1)))) ||
            (s.color(i) == -1 && s.color(i - 1) == 1 &&
             ((s.open(i) >= s.close(i - 1) && s.close(i) < s.open(i - 1)) ||
              (s.open(i) > s.close(i - 1) && s.close(i) <= s.open(i - 1))));
        if (!match) return 0.0;
        return candlestick::signal(s.color(i),
            s.open(i) != s.close(i - 1) && s.close(i) != s.open(i - 1) ? 100 : 80);
    }
    case CandlePattern::MorningStar:
        return s.body(i - 2) > s.average(SettingType::BodyLong, i - 2) &&
               s.color(i - 2) == -1 &&
               s.body(i - 1) <= s.average(SettingType::BodyShort, i - 1) &&
               s.body_gap_down(i - 1, i - 2) &&
               s.body(i) > s.average(SettingType::BodyShort, i) &&
               s.color(i) == 1 &&
               s.close(i) > s.close(i - 2) + s.body(i - 2) * penetration ? 100.0 : 0.0;
    case CandlePattern::EveningStar:
        return s.body(i - 2) > s.average(SettingType::BodyLong, i - 2) &&
               s.color(i - 2) == 1 &&
               s.body(i - 1) <= s.average(SettingType::BodyShort, i - 1) &&
               s.body_gap_up(i - 1, i - 2) &&
               s.body(i) > s.average(SettingType::BodyShort, i) &&
               s.color(i) == -1 &&
               s.close(i) < s.close(i - 2) - s.body(i - 2) * penetration ? -100.0 : 0.0;
    case CandlePattern::Harami:
        if (s.body(i - 1) <= s.average(SettingType::BodyLong, i - 1) ||
            s.body(i) > s.average(SettingType::BodyShort, i)) return 0.0;
        if (top(i) < top(i - 1) && bottom(i) > bottom(i - 1))
            return candlestick::signal(-s.color(i - 1));
        if (top(i) <= top(i - 1) && bottom(i) >= bottom(i - 1))
            return candlestick::signal(-s.color(i - 1), 80);
        return 0.0;
    case CandlePattern::ShootingStar:
        return s.body(i) < s.average(SettingType::BodyShort, i) &&
               s.upper(i) > s.average(SettingType::ShadowLong, i) &&
               s.lower(i) < s.average(SettingType::ShadowVeryShort, i) &&
               s.body_gap_up(i, i - 1) ? -100.0 : 0.0;
    case CandlePattern::HangingMan:
        return s.body(i) < s.average(SettingType::BodyShort, i) &&
               s.lower(i) > s.average(SettingType::ShadowLong, i) &&
               s.upper(i) < s.average(SettingType::ShadowVeryShort, i) &&
               bottom(i) >= s.high(i - 1) - s.average(SettingType::Near, i - 1)
            ? -100.0 : 0.0;
    case CandlePattern::Marubozu:
        return s.body(i) > s.average(SettingType::BodyLong, i) &&
               s.upper(i) < s.average(SettingType::ShadowVeryShort, i) &&
               s.lower(i) < s.average(SettingType::ShadowVeryShort, i)
            ? candlestick::signal(s.color(i)) : 0.0;
    case CandlePattern::SpinningTop:
        return s.body(i) < s.average(SettingType::BodyShort, i) &&
               s.upper(i) > s.body(i) && s.lower(i) > s.body(i)
            ? candlestick::signal(s.color(i)) : 0.0;
    case CandlePattern::ThreeWhiteSoldiers:
        return s.color(i - 2) == 1 &&
               s.upper(i - 2) < s.average(SettingType::ShadowVeryShort, i - 2) &&
               s.color(i - 1) == 1 &&
               s.upper(i - 1) < s.average(SettingType::ShadowVeryShort, i - 1) &&
               s.color(i) == 1 && s.upper(i) < s.average(SettingType::ShadowVeryShort, i) &&
               s.close(i) > s.close(i - 1) && s.close(i - 1) > s.close(i - 2) &&
               s.open(i - 1) > s.open(i - 2) &&
               s.open(i - 1) <= s.close(i - 2) + s.average(SettingType::Near, i - 2) &&
               s.open(i) > s.open(i - 1) &&
               s.open(i) <= s.close(i - 1) + s.average(SettingType::Near, i - 1) &&
               s.body(i - 1) > s.body(i - 2) - s.average(SettingType::Far, i - 2) &&
               s.body(i) > s.body(i - 1) - s.average(SettingType::Far, i - 1) &&
               s.body(i) > s.average(SettingType::BodyShort, i) ? 100.0 : 0.0;
    case CandlePattern::ThreeBlackCrows:
        return s.color(i - 3) == 1 && s.color(i - 2) == -1 &&
               s.lower(i - 2) < s.average(SettingType::ShadowVeryShort, i - 2) &&
               s.color(i - 1) == -1 &&
               s.lower(i - 1) < s.average(SettingType::ShadowVeryShort, i - 1) &&
               s.color(i) == -1 && s.lower(i) < s.average(SettingType::ShadowVeryShort, i) &&
               s.open(i - 1) < s.open(i - 2) && s.open(i - 1) > s.close(i - 2) &&
               s.open(i) < s.open(i - 1) && s.open(i) > s.close(i - 1) &&
               s.high(i - 3) > s.close(i - 2) &&
               s.close(i - 2) > s.close(i - 1) && s.close(i - 1) > s.close(i)
            ? -100.0 : 0.0;
    case CandlePattern::Piercing:
        return s.color(i - 1) == -1 &&
               s.body(i - 1) > s.average(SettingType::BodyLong, i - 1) &&
               s.color(i) == 1 && s.body(i) > s.average(SettingType::BodyLong, i) &&
               s.open(i) < s.low(i - 1) && s.close(i) < s.open(i - 1) &&
               s.close(i) > s.close(i - 1) + s.body(i - 1) * 0.5 ? 100.0 : 0.0;
    case CandlePattern::DarkCloudCover:
        return s.color(i - 1) == 1 &&
               s.body(i - 1) > s.average(SettingType::BodyLong, i - 1) &&
               s.color(i) == -1 && s.open(i) > s.high(i - 1) &&
               s.close(i) > s.open(i - 1) &&
               s.close(i) < s.close(i - 1) - s.body(i - 1) * penetration ? -100.0 : 0.0;
    case CandlePattern::AbandonedBaby:
        if (s.body(i - 2) <= s.average(SettingType::BodyLong, i - 2) ||
            s.body(i - 1) > s.average(SettingType::BodyDoji, i - 1) ||
            s.body(i) <= s.average(SettingType::BodyShort, i)) return 0.0;
        if ((s.color(i - 2) == 1 && s.color(i) == -1 &&
             s.close(i) < s.close(i - 2) - s.body(i - 2) * penetration &&
             s.gap_up(i - 1, i - 2) && s.gap_down(i, i - 1)) ||
            (s.color(i - 2) == -1 && s.color(i) == 1 &&
             s.close(i) > s.close(i - 2) + s.body(i - 2) * penetration &&
             s.gap_down(i - 1, i - 2) && s.gap_up(i, i - 1)))
            return candlestick::signal(s.color(i));
        return 0.0;
    case CandlePattern::ThreeInside:
        return s.body(i - 2) > s.average(SettingType::BodyLong, i - 2) &&
               s.body(i - 1) <= s.average(SettingType::BodyShort, i - 1) &&
               top(i - 1) < top(i - 2) && bottom(i - 1) > bottom(i - 2) &&
               ((s.color(i - 2) == 1 && s.color(i) == -1 && s.close(i) < s.open(i - 2)) ||
                (s.color(i - 2) == -1 && s.color(i) == 1 && s.close(i) > s.open(i - 2)))
            ? candlestick::signal(-s.color(i - 2)) : 0.0;
    case CandlePattern::ThreeOutside:
        if ((s.color(i - 1) == 1 && s.color(i - 2) == -1 &&
             s.close(i - 1) > s.open(i - 2) && s.open(i - 1) < s.close(i - 2) &&
             s.close(i) > s.close(i - 1)) ||
            (s.color(i - 1) == -1 && s.color(i - 2) == 1 &&
             s.open(i - 1) > s.close(i - 2) && s.close(i - 1) < s.open(i - 2) &&
             s.close(i) < s.close(i - 1)))
            return candlestick::signal(s.color(i - 1));
        return 0.0;
    case CandlePattern::Kicking:
        return s.color(i - 1) == -s.color(i) &&
               s.body(i - 1) > s.average(SettingType::BodyLong, i - 1) &&
               s.upper(i - 1) < s.average(SettingType::ShadowVeryShort, i - 1) &&
               s.lower(i - 1) < s.average(SettingType::ShadowVeryShort, i - 1) &&
               s.body(i) > s.average(SettingType::BodyLong, i) &&
               s.upper(i) < s.average(SettingType::ShadowVeryShort, i) &&
               s.lower(i) < s.average(SettingType::ShadowVeryShort, i) &&
               ((s.color(i - 1) == -1 && s.gap_up(i, i - 1)) ||
                (s.color(i - 1) == 1 && s.gap_down(i, i - 1)))
            ? candlestick::signal(s.color(i)) : 0.0;
    case CandlePattern::BeltHold:
        return s.body(i) > s.average(SettingType::BodyLong, i) &&
               ((s.color(i) == 1 && s.lower(i) < s.average(SettingType::ShadowVeryShort, i)) ||
                (s.color(i) == -1 && s.upper(i) < s.average(SettingType::ShadowVeryShort, i)))
            ? candlestick::signal(s.color(i)) : 0.0;
    case CandlePattern::CounterAttack:
        return s.color(i - 1) == -s.color(i) &&
               s.body(i - 1) > s.average(SettingType::BodyLong, i - 1) &&
               s.body(i) > s.average(SettingType::BodyLong, i) &&
               s.close(i) <= s.close(i - 1) + s.average(SettingType::Equal, i - 1) &&
               s.close(i) >= s.close(i - 1) - s.average(SettingType::Equal, i - 1)
            ? candlestick::signal(s.color(i)) : 0.0;
    case CandlePattern::HomingPigeon:
        return s.color(i - 1) == -1 && s.color(i) == -1 &&
               s.body(i - 1) > s.average(SettingType::BodyLong, i - 1) &&
               s.body(i) <= s.average(SettingType::BodyShort, i) &&
               s.open(i) < s.open(i - 1) && s.close(i) > s.close(i - 1) ? 100.0 : 0.0;
    case CandlePattern::MatchingLow:
        return s.color(i - 1) == -1 && s.color(i) == -1 &&
               s.close(i) <= s.close(i - 1) + s.average(SettingType::Equal, i - 1) &&
               s.close(i) >= s.close(i - 1) - s.average(SettingType::Equal, i - 1)
            ? 100.0 : 0.0;
    case CandlePattern::TasukiGap:
        if ((s.body_gap_up(i - 1, i - 2) && s.color(i - 1) == 1 && s.color(i) == -1 &&
             s.open(i) < s.close(i - 1) && s.open(i) > s.open(i - 1) &&
             s.close(i) < s.open(i - 1) && s.close(i) > top(i - 2) &&
             std::fabs(s.body(i - 1) - s.body(i)) < s.average(SettingType::Near, i - 1)) ||
            (s.body_gap_down(i - 1, i - 2) && s.color(i - 1) == -1 && s.color(i) == 1 &&
             s.open(i) < s.open(i - 1) && s.open(i) > s.close(i - 1) &&
             s.close(i) > s.open(i - 1) && s.close(i) < bottom(i - 2) &&
             std::fabs(s.body(i - 1) - s.body(i)) < s.average(SettingType::Near, i - 1)))
            return candlestick::signal(s.color(i - 1));
        return 0.0;
    case CandlePattern::UpsideGapTwoCrows:
        return s.color(i - 2) == 1 &&
               s.body(i - 2) > s.average(SettingType::BodyLong, i - 2) &&
               s.color(i - 1) == -1 &&
               s.body(i - 1) <= s.average(SettingType::BodyShort, i - 1) &&
               s.body_gap_up(i - 1, i - 2) && s.color(i) == -1 &&
               s.open(i) > s.open(i - 1) && s.close(i) < s.close(i - 1) &&
               s.close(i) > s.close(i - 2) ? -100.0 : 0.0;
    case CandlePattern::TwoCrows:
        return s.color(i - 2) == 1 &&
               s.body(i - 2) > s.average(SettingType::BodyLong, i - 2) &&
               s.color(i - 1) == -1 && s.body_gap_up(i - 1, i - 2) &&
               s.color(i) == -1 && s.open(i) < s.open(i - 1) &&
               s.open(i) > s.close(i - 1) && s.close(i) > s.open(i - 2) &&
               s.close(i) < s.close(i - 2) ? -100.0 : 0.0;
    case CandlePattern::ThreeLineStrike:
        if (s.color(i - 3) != s.color(i - 2) ||
            s.color(i - 2) != s.color(i - 1) ||
            s.color(i) != -s.color(i - 1) ||
            s.open(i - 2) < bottom(i - 3) - s.average(SettingType::Near, i - 3) ||
            s.open(i - 2) > top(i - 3) + s.average(SettingType::Near, i - 3) ||
            s.open(i - 1) < bottom(i - 2) - s.average(SettingType::Near, i - 2) ||
            s.open(i - 1) > top(i - 2) + s.average(SettingType::Near, i - 2)) return 0.0;
        if ((s.color(i - 1) == 1 && s.close(i - 1) > s.close(i - 2) &&
             s.close(i - 2) > s.close(i - 3) && s.open(i) > s.close(i - 1) &&
             s.close(i) < s.open(i - 3)) ||
            (s.color(i - 1) == -1 && s.close(i - 1) < s.close(i - 2) &&
             s.close(i - 2) < s.close(i - 3) && s.open(i) < s.close(i - 1) &&
             s.close(i) > s.open(i - 3)))
            return candlestick::signal(s.color(i));
        return 0.0;
    case CandlePattern::ThreeStarsInSouth:
        return s.color(i - 2) == -1 && s.color(i - 1) == -1 && s.color(i) == -1 &&
               s.body(i - 2) > s.average(SettingType::BodyLong, i - 2) &&
               s.lower(i - 2) > s.average(SettingType::ShadowLong, i - 2) &&
               s.body(i - 1) < s.body(i - 2) && s.open(i - 1) > s.close(i - 2) &&
               s.open(i - 1) <= s.high(i - 2) && s.low(i - 1) < s.close(i - 2) &&
               s.low(i - 1) >= s.low(i - 2) &&
               s.lower(i - 1) > s.average(SettingType::ShadowVeryShort, i - 1) &&
               s.body(i) < s.average(SettingType::BodyShort, i) &&
               s.lower(i) < s.average(SettingType::ShadowVeryShort, i) &&
               s.upper(i) < s.average(SettingType::ShadowVeryShort, i) &&
               s.low(i) > s.low(i - 1) && s.high(i) < s.high(i - 1) ? 100.0 : 0.0;
    case CandlePattern::AdvanceBlock:
        if (s.color(i - 2) != 1 || s.color(i - 1) != 1 || s.color(i) != 1 ||
            s.close(i) <= s.close(i - 1) || s.close(i - 1) <= s.close(i - 2) ||
            s.open(i - 1) <= s.open(i - 2) ||
            s.open(i - 1) > s.close(i - 2) + s.average(SettingType::Near, i - 2) ||
            s.open(i) <= s.open(i - 1) ||
            s.open(i) > s.close(i - 1) + s.average(SettingType::Near, i - 1) ||
            s.body(i - 2) <= s.average(SettingType::BodyLong, i - 2) ||
            s.upper(i - 2) >= s.average(SettingType::ShadowShort, i - 2)) return 0.0;
        return ((s.body(i - 1) < s.body(i - 2) - s.average(SettingType::Far, i - 2) &&
                 s.body(i) < s.body(i - 1) + s.average(SettingType::Near, i - 1)) ||
                s.body(i) < s.body(i - 1) - s.average(SettingType::Far, i - 1) ||
                (s.body(i) < s.body(i - 1) && s.body(i - 1) < s.body(i - 2) &&
                 (s.upper(i) > s.average(SettingType::ShadowShort, i) ||
                  s.upper(i - 1) > s.average(SettingType::ShadowShort, i - 1))) ||
                (s.body(i) < s.body(i - 1) &&
                 s.upper(i) > s.average(SettingType::ShadowLong, i))) ? -100.0 : 0.0;
    case CandlePattern::Breakaway:
        if (s.body(i - 4) <= s.average(SettingType::BodyLong, i - 4) ||
            s.color(i - 4) != s.color(i - 3) || s.color(i - 3) != s.color(i - 1) ||
            s.color(i - 1) != -s.color(i)) return 0.0;
        if ((s.color(i - 4) == -1 && s.body_gap_down(i - 3, i - 4) &&
             s.high(i - 2) < s.high(i - 3) && s.low(i - 2) < s.low(i - 3) &&
             s.high(i - 1) < s.high(i - 2) && s.low(i - 1) < s.low(i - 2) &&
             s.close(i) > s.open(i - 3) && s.close(i) < s.close(i - 4)) ||
            (s.color(i - 4) == 1 && s.body_gap_up(i - 3, i - 4) &&
             s.high(i - 2) > s.high(i - 3) && s.low(i - 2) > s.low(i - 3) &&
             s.high(i - 1) > s.high(i - 2) && s.low(i - 1) > s.low(i - 2) &&
             s.close(i) < s.open(i - 3) && s.close(i) > s.close(i - 4)))
            return candlestick::signal(s.color(i));
        return 0.0;
    case CandlePattern::ClosingMarubozu:
        return s.body(i) > s.average(SettingType::BodyLong, i) &&
               ((s.color(i) == 1 && s.upper(i) < s.average(SettingType::ShadowVeryShort, i)) ||
                (s.color(i) == -1 && s.lower(i) < s.average(SettingType::ShadowVeryShort, i)))
            ? candlestick::signal(s.color(i)) : 0.0;
    case CandlePattern::ConcealingBabySwallow:
        return s.color(i - 3) == -1 && s.color(i - 2) == -1 &&
               s.color(i - 1) == -1 && s.color(i) == -1 &&
               s.lower(i - 3) < s.average(SettingType::ShadowVeryShort, i - 3) &&
               s.upper(i - 3) < s.average(SettingType::ShadowVeryShort, i - 3) &&
               s.lower(i - 2) < s.average(SettingType::ShadowVeryShort, i - 2) &&
               s.upper(i - 2) < s.average(SettingType::ShadowVeryShort, i - 2) &&
               s.body_gap_down(i - 1, i - 2) &&
               s.upper(i - 1) > s.average(SettingType::ShadowVeryShort, i - 1) &&
               s.high(i - 1) > s.close(i - 2) && s.high(i) > s.high(i - 1) &&
               s.low(i) < s.low(i - 1) ? 100.0 : 0.0;
    case CandlePattern::DojiStar:
        return s.body(i - 1) > s.average(SettingType::BodyLong, i - 1) &&
               s.body(i) <= s.average(SettingType::BodyDoji, i) &&
               ((s.color(i - 1) == 1 && s.body_gap_up(i, i - 1)) ||
                (s.color(i - 1) == -1 && s.body_gap_down(i, i - 1)))
            ? candlestick::signal(-s.color(i - 1)) : 0.0;
    case CandlePattern::DragonflyDoji:
        return s.body(i) <= s.average(SettingType::BodyDoji, i) &&
               s.upper(i) < s.average(SettingType::ShadowVeryShort, i) &&
               s.lower(i) > s.average(SettingType::ShadowVeryShort, i) ? 100.0 : 0.0;
    case CandlePattern::EveningDojiStar:
        return s.body(i - 2) > s.average(SettingType::BodyLong, i - 2) &&
               s.color(i - 2) == 1 &&
               s.body(i - 1) <= s.average(SettingType::BodyDoji, i - 1) &&
               s.body_gap_up(i - 1, i - 2) &&
               s.body(i) > s.average(SettingType::BodyShort, i) && s.color(i) == -1 &&
               s.close(i) < s.close(i - 2) - s.body(i - 2) * penetration ? -100.0 : 0.0;
    case CandlePattern::GapSideSideWhite:
        return ((s.body_gap_up(i - 1, i - 2) && s.body_gap_up(i, i - 2)) ||
                (s.body_gap_down(i - 1, i - 2) && s.body_gap_down(i, i - 2))) &&
               s.color(i - 1) == 1 && s.color(i) == 1 &&
               s.body(i) >= s.body(i - 1) - s.average(SettingType::Near, i - 1) &&
               s.body(i) <= s.body(i - 1) + s.average(SettingType::Near, i - 1) &&
               s.open(i) >= s.open(i - 1) - s.average(SettingType::Equal, i - 1) &&
               s.open(i) <= s.open(i - 1) + s.average(SettingType::Equal, i - 1)
            ? (s.body_gap_up(i - 1, i - 2) ? 100.0 : -100.0) : 0.0;
    case CandlePattern::GravestoneDoji:
        return s.body(i) <= s.average(SettingType::BodyDoji, i) &&
               s.lower(i) < s.average(SettingType::ShadowVeryShort, i) &&
               s.upper(i) > s.average(SettingType::ShadowVeryShort, i) ? 100.0 : 0.0;
    case CandlePattern::HaramiCross:
        if (s.body(i - 1) <= s.average(SettingType::BodyLong, i - 1) ||
            s.body(i) > s.average(SettingType::BodyDoji, i)) return 0.0;
        if (top(i) < top(i - 1) && bottom(i) > bottom(i - 1))
            return candlestick::signal(-s.color(i - 1));
        if (top(i) <= top(i - 1) && bottom(i) >= bottom(i - 1))
            return candlestick::signal(-s.color(i - 1), 80);
        return 0.0;
    case CandlePattern::HighWave:
        return s.body(i) < s.average(SettingType::BodyShort, i) &&
               s.upper(i) > s.average(SettingType::ShadowVeryLong, i) &&
               s.lower(i) > s.average(SettingType::ShadowVeryLong, i)
            ? candlestick::signal(s.color(i)) : 0.0;
    case CandlePattern::Hikkake: {
        const auto detected = [&s](std::size_t j) {
            if (!(s.high(j - 1) < s.high(j - 2) && s.low(j - 1) > s.low(j - 2)))
                return 0;
            if (s.high(j) < s.high(j - 1) && s.low(j) < s.low(j - 1)) return 100;
            if (s.high(j) > s.high(j - 1) && s.low(j) > s.low(j - 1)) return -100;
            return 0;
        };
        if (const int result = detected(i); result != 0) return result;
        for (std::size_t distance = 1; distance <= 3; ++distance) {
            const auto j = i - distance;
            if (const int result = detected(j); result != 0) {
                return (result > 0 && s.close(i) > s.high(j - 1)) ||
                       (result < 0 && s.close(i) < s.low(j - 1))
                    ? static_cast<double>(result * 2) : 0.0;
            }
        }
        return 0.0;
    }
    case CandlePattern::ModifiedHikkake: {
        const auto detected = [&s](std::size_t j) {
            if (s.high(j - 2) >= s.high(j - 3) || s.low(j - 2) <= s.low(j - 3) ||
                s.high(j - 1) >= s.high(j - 2) || s.low(j - 1) <= s.low(j - 2))
                return 0;
            if (s.high(j) < s.high(j - 1) && s.low(j) < s.low(j - 1) &&
                s.close(j - 2) <= s.low(j - 2) + s.average(SettingType::Near, j - 2))
                return 100;
            if (s.high(j) > s.high(j - 1) && s.low(j) > s.low(j - 1) &&
                s.close(j - 2) >= s.high(j - 2) - s.average(SettingType::Near, j - 2))
                return -100;
            return 0;
        };
        if (const int result = detected(i); result != 0) return result;
        for (std::size_t distance = 1; distance <= 3; ++distance) {
            const auto j = i - distance;
            if (const int result = detected(j); result != 0) {
                return (result > 0 && s.close(i) > s.high(j - 1)) ||
                       (result < 0 && s.close(i) < s.low(j - 1))
                    ? static_cast<double>(result * 2) : 0.0;
            }
        }
        return 0.0;
    }
    case CandlePattern::IdenticalThreeCrows:
        return s.color(i - 2) == -1 &&
               s.lower(i - 2) < s.average(SettingType::ShadowVeryShort, i - 2) &&
               s.color(i - 1) == -1 &&
               s.lower(i - 1) < s.average(SettingType::ShadowVeryShort, i - 1) &&
               s.color(i) == -1 && s.lower(i) < s.average(SettingType::ShadowVeryShort, i) &&
               s.close(i - 2) > s.close(i - 1) && s.close(i - 1) > s.close(i) &&
               std::fabs(s.open(i - 1) - s.close(i - 2)) <= s.average(SettingType::Equal, i - 2) &&
               std::fabs(s.open(i) - s.close(i - 1)) <= s.average(SettingType::Equal, i - 1)
            ? -100.0 : 0.0;
    case CandlePattern::InNeck:
        return s.color(i - 1) == -1 &&
               s.body(i - 1) > s.average(SettingType::BodyLong, i - 1) &&
               s.color(i) == 1 && s.open(i) < s.low(i - 1) &&
               s.close(i) <= s.close(i - 1) + s.average(SettingType::Equal, i - 1) &&
               s.close(i) >= s.close(i - 1) ? -100.0 : 0.0;
    case CandlePattern::InvertedHammer:
        return s.body(i) < s.average(SettingType::BodyShort, i) &&
               s.upper(i) > s.average(SettingType::ShadowLong, i) &&
               s.lower(i) < s.average(SettingType::ShadowVeryShort, i) &&
               s.body_gap_down(i, i - 1) ? 100.0 : 0.0;
    case CandlePattern::KickingByLength:
        if (s.color(i - 1) == -s.color(i) &&
            s.body(i - 1) > s.average(SettingType::BodyLong, i - 1) &&
            s.upper(i - 1) < s.average(SettingType::ShadowVeryShort, i - 1) &&
            s.lower(i - 1) < s.average(SettingType::ShadowVeryShort, i - 1) &&
            s.body(i) > s.average(SettingType::BodyLong, i) &&
            s.upper(i) < s.average(SettingType::ShadowVeryShort, i) &&
            s.lower(i) < s.average(SettingType::ShadowVeryShort, i) &&
            ((s.color(i - 1) == -1 && s.gap_up(i, i - 1)) ||
             (s.color(i - 1) == 1 && s.gap_down(i, i - 1))))
            return candlestick::signal(s.body(i) > s.body(i - 1) ? s.color(i) : s.color(i - 1));
        return 0.0;
    case CandlePattern::LadderBottom:
        return s.color(i - 4) == -1 && s.color(i - 3) == -1 && s.color(i - 2) == -1 &&
               s.open(i - 4) > s.open(i - 3) && s.open(i - 3) > s.open(i - 2) &&
               s.close(i - 4) > s.close(i - 3) && s.close(i - 3) > s.close(i - 2) &&
               s.color(i - 1) == -1 &&
               s.upper(i - 1) > s.average(SettingType::ShadowVeryShort, i - 1) &&
               s.color(i) == 1 && s.open(i) > s.open(i - 1) &&
               s.close(i) > s.high(i - 1) ? 100.0 : 0.0;
    case CandlePattern::LongLeggedDoji:
        return s.body(i) <= s.average(SettingType::BodyDoji, i) &&
               (s.lower(i) > s.average(SettingType::ShadowLong, i) ||
                s.upper(i) > s.average(SettingType::ShadowLong, i)) ? 100.0 : 0.0;
    case CandlePattern::LongLine:
        return s.body(i) > s.average(SettingType::BodyLong, i) &&
               s.upper(i) < s.average(SettingType::ShadowShort, i) &&
               s.lower(i) < s.average(SettingType::ShadowShort, i)
            ? candlestick::signal(s.color(i)) : 0.0;
    case CandlePattern::MatHold:
        return s.body(i - 4) > s.average(SettingType::BodyLong, i - 4) &&
               s.body(i - 3) < s.average(SettingType::BodyShort, i - 3) &&
               s.body(i - 2) < s.average(SettingType::BodyShort, i - 2) &&
               s.body(i - 1) < s.average(SettingType::BodyShort, i - 1) &&
               s.color(i - 4) == 1 && s.color(i - 3) == -1 && s.color(i) == 1 &&
               s.body_gap_up(i - 3, i - 4) && bottom(i - 2) < s.close(i - 4) &&
               bottom(i - 1) < s.close(i - 4) &&
               bottom(i - 2) > s.close(i - 4) - s.body(i - 4) * penetration &&
               bottom(i - 1) > s.close(i - 4) - s.body(i - 4) * penetration &&
               top(i - 2) < s.open(i - 3) && top(i - 1) < top(i - 2) &&
               s.open(i) > s.close(i - 1) &&
               s.close(i) > std::max({s.high(i - 3), s.high(i - 2), s.high(i - 1)})
            ? 100.0 : 0.0;
    case CandlePattern::MorningDojiStar:
        return s.body(i - 2) > s.average(SettingType::BodyLong, i - 2) &&
               s.color(i - 2) == -1 &&
               s.body(i - 1) <= s.average(SettingType::BodyDoji, i - 1) &&
               s.body_gap_down(i - 1, i - 2) &&
               s.body(i) > s.average(SettingType::BodyShort, i) && s.color(i) == 1 &&
               s.close(i) > s.close(i - 2) + s.body(i - 2) * penetration ? 100.0 : 0.0;
    case CandlePattern::OnNeck:
        return s.color(i - 1) == -1 &&
               s.body(i - 1) > s.average(SettingType::BodyLong, i - 1) &&
               s.color(i) == 1 && s.open(i) < s.low(i - 1) &&
               std::fabs(s.close(i) - s.low(i - 1)) <= s.average(SettingType::Equal, i - 1)
            ? -100.0 : 0.0;
    case CandlePattern::RickshawMan: {
        const double midpoint = s.low(i) + (s.high(i) - s.low(i)) * 0.5;
        return s.body(i) <= s.average(SettingType::BodyDoji, i) &&
               s.lower(i) > s.average(SettingType::ShadowLong, i) &&
               s.upper(i) > s.average(SettingType::ShadowLong, i) &&
               bottom(i) <= midpoint + s.average(SettingType::Near, i) &&
               top(i) >= midpoint - s.average(SettingType::Near, i) ? 100.0 : 0.0;
    }
    case CandlePattern::RiseFallThreeMethods: {
        const int direction = s.color(i - 4);
        return s.body(i - 4) > s.average(SettingType::BodyLong, i - 4) &&
               s.body(i - 3) < s.average(SettingType::BodyShort, i - 3) &&
               s.body(i - 2) < s.average(SettingType::BodyShort, i - 2) &&
               s.body(i - 1) < s.average(SettingType::BodyShort, i - 1) &&
               s.body(i) > s.average(SettingType::BodyLong, i) &&
               direction == -s.color(i - 3) && s.color(i - 3) == s.color(i - 2) &&
               s.color(i - 2) == s.color(i - 1) && s.color(i - 1) == -s.color(i) &&
               bottom(i - 3) < s.high(i - 4) && top(i - 3) > s.low(i - 4) &&
               bottom(i - 2) < s.high(i - 4) && top(i - 2) > s.low(i - 4) &&
               bottom(i - 1) < s.high(i - 4) && top(i - 1) > s.low(i - 4) &&
               s.close(i - 2) * direction < s.close(i - 3) * direction &&
               s.close(i - 1) * direction < s.close(i - 2) * direction &&
               s.open(i) * direction > s.close(i - 1) * direction &&
               s.close(i) * direction > s.close(i - 4) * direction
            ? candlestick::signal(direction) : 0.0;
    }
    case CandlePattern::SeparatingLines:
        return s.color(i - 1) == -s.color(i) &&
               std::fabs(s.open(i) - s.open(i - 1)) <= s.average(SettingType::Equal, i - 1) &&
               s.body(i) > s.average(SettingType::BodyLong, i) &&
               ((s.color(i) == 1 && s.lower(i) < s.average(SettingType::ShadowVeryShort, i)) ||
                (s.color(i) == -1 && s.upper(i) < s.average(SettingType::ShadowVeryShort, i)))
            ? candlestick::signal(s.color(i)) : 0.0;
    case CandlePattern::ShortLine:
        return s.body(i) < s.average(SettingType::BodyShort, i) &&
               s.upper(i) < s.average(SettingType::ShadowShort, i) &&
               s.lower(i) < s.average(SettingType::ShadowShort, i)
            ? candlestick::signal(s.color(i)) : 0.0;
    case CandlePattern::StalledPattern:
        return s.color(i - 2) == 1 && s.color(i - 1) == 1 && s.color(i) == 1 &&
               s.close(i) > s.close(i - 1) && s.close(i - 1) > s.close(i - 2) &&
               s.body(i - 2) > s.average(SettingType::BodyLong, i - 2) &&
               s.body(i - 1) > s.average(SettingType::BodyLong, i - 1) &&
               s.upper(i - 1) < s.average(SettingType::ShadowVeryShort, i - 1) &&
               s.open(i - 1) > s.open(i - 2) &&
               s.open(i - 1) <= s.close(i - 2) + s.average(SettingType::Near, i - 2) &&
               s.body(i) < s.average(SettingType::BodyShort, i) &&
               s.open(i) >= s.close(i - 1) - s.body(i) - s.average(SettingType::Near, i - 1)
            ? -100.0 : 0.0;
    case CandlePattern::StickSandwich:
        return s.color(i - 2) == -1 && s.color(i - 1) == 1 && s.color(i) == -1 &&
               s.low(i - 1) > s.close(i - 2) &&
               std::fabs(s.close(i) - s.close(i - 2)) <= s.average(SettingType::Equal, i - 2)
            ? 100.0 : 0.0;
    case CandlePattern::Takuri:
        return s.body(i) <= s.average(SettingType::BodyDoji, i) &&
               s.upper(i) < s.average(SettingType::ShadowVeryShort, i) &&
               s.lower(i) > s.average(SettingType::ShadowVeryLong, i) ? 100.0 : 0.0;
    case CandlePattern::Thrusting:
        return s.color(i - 1) == -1 &&
               s.body(i - 1) > s.average(SettingType::BodyLong, i - 1) &&
               s.color(i) == 1 && s.open(i) < s.low(i - 1) &&
               s.close(i) > s.close(i - 1) + s.average(SettingType::Equal, i - 1) &&
               s.close(i) <= s.close(i - 1) + s.body(i - 1) * 0.5 ? -100.0 : 0.0;
    case CandlePattern::Tristar:
        if (s.body(i - 2) > s.average(SettingType::BodyDoji, i - 2) ||
            s.body(i - 1) > s.average(SettingType::BodyDoji, i - 2) ||
            s.body(i) > s.average(SettingType::BodyDoji, i - 2)) return 0.0;
        if (s.body_gap_up(i - 1, i - 2) && top(i) < top(i - 1)) return -100.0;
        if (s.body_gap_down(i - 1, i - 2) && bottom(i) > bottom(i - 1)) return 100.0;
        return 0.0;
    case CandlePattern::UniqueThreeRiver:
        return s.body(i - 2) > s.average(SettingType::BodyLong, i - 2) &&
               s.color(i - 2) == -1 && s.color(i - 1) == -1 &&
               s.close(i - 1) > s.close(i - 2) && s.open(i - 1) <= s.open(i - 2) &&
               s.low(i - 1) < s.low(i - 2) &&
               s.body(i) < s.average(SettingType::BodyShort, i) && s.color(i) == 1 &&
               s.open(i) > s.low(i - 1) ? 100.0 : 0.0;
    case CandlePattern::XSideGapThreeMethods:
        return s.color(i - 2) == s.color(i - 1) && s.color(i - 1) == -s.color(i) &&
               s.open(i) < top(i - 1) && s.open(i) > bottom(i - 1) &&
               s.close(i) < top(i - 2) && s.close(i) > bottom(i - 2) &&
               ((s.color(i - 2) == 1 && s.body_gap_up(i - 1, i - 2)) ||
                (s.color(i - 2) == -1 && s.body_gap_down(i - 1, i - 2)))
            ? candlestick::signal(s.color(i - 2)) : 0.0;
    }
    return 0.0;
}

template <CandlePattern Pattern>
class CandleIndicator : public Indicator<CandleIndicator<Pattern>> {
public:
    CandleIndicator(const Line<double>& open, const Line<double>& high,
                    const Line<double>& low, const Line<double>& close,
                    double penetration, candlestick::Settings settings)
        : series_(open, high, low, close, std::move(settings)),
          penetration_(candlestick::validate_penetration(penetration)),
          lookback_(candle_pattern_lookback(Pattern, series_.settings())) {}

    void next_impl() {
        if (this->line_.empty()) [[unlikely]] { this->reserve_output(series_.size()); }
        const auto i = series_.index();
        this->line_.forward(i < lookback_ ? 0.0
            : evaluate_candle_pattern(Pattern, series_, i, penetration_));
    }
    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return lookback_ + 1; }

private:
    CandleSeries series_;
    double penetration_;
    std::size_t lookback_;
};

} // namespace detail

#define STRATFORGE_CANDLE_CLASS(Name, PatternName)                               \
class Name : public detail::CandleIndicator<detail::CandlePattern::PatternName> { \
    using Base = detail::CandleIndicator<detail::CandlePattern::PatternName>;    \
public:                                                                           \
    Name(const Line<double>& open, const Line<double>& high,                      \
         const Line<double>& low, const Line<double>& close,                      \
         candlestick::Settings settings = {})                                     \
        : Base(open, high, low, close, 0.0, std::move(settings)) {}               \
}

#define STRATFORGE_CANDLE_PENETRATION_CLASS(Name, PatternName, DefaultValue)      \
class Name : public detail::CandleIndicator<detail::CandlePattern::PatternName> { \
    using Base = detail::CandleIndicator<detail::CandlePattern::PatternName>;    \
public:                                                                           \
    Name(const Line<double>& open, const Line<double>& high,                      \
         const Line<double>& low, const Line<double>& close,                      \
         double penetration = DefaultValue, candlestick::Settings settings = {})  \
        : Base(open, high, low, close, penetration, std::move(settings)) {}       \
}

class CDLDoji : public detail::CandleIndicator<detail::CandlePattern::Doji> {
    using Base = detail::CandleIndicator<detail::CandlePattern::Doji>;

    static candlestick::Settings settings_with_factor(double factor) {
        candlestick::Settings settings;
        settings.set(candlestick::SettingType::BodyDoji,
                     {candlestick::RangeType::HighLow, 10, factor});
        return settings;
    }

public:
    CDLDoji(const Line<double>& open, const Line<double>& high,
            const Line<double>& low, const Line<double>& close,
            candlestick::Settings settings = {})
        : Base(open, high, low, close, 0.0, std::move(settings)) {}

    CDLDoji(const Line<double>& open, const Line<double>& high,
            const Line<double>& low, const Line<double>& close,
            double body_threshold)
        : Base(open, high, low, close, 0.0,
               settings_with_factor(body_threshold)) {}
};
STRATFORGE_CANDLE_CLASS(CDLHammer, Hammer);
STRATFORGE_CANDLE_CLASS(CDLEngulfing, Engulfing);
STRATFORGE_CANDLE_PENETRATION_CLASS(CDLMorningStar, MorningStar, 0.3);
STRATFORGE_CANDLE_PENETRATION_CLASS(CDLEveningStar, EveningStar, 0.3);
STRATFORGE_CANDLE_CLASS(CDLHarami, Harami);
STRATFORGE_CANDLE_CLASS(CDLShootingStar, ShootingStar);
STRATFORGE_CANDLE_CLASS(CDLHangingMan, HangingMan);
STRATFORGE_CANDLE_CLASS(CDLMarubozu, Marubozu);
STRATFORGE_CANDLE_CLASS(CDLSpinningTop, SpinningTop);
STRATFORGE_CANDLE_CLASS(CDLThreeWhiteSoldiers, ThreeWhiteSoldiers);
STRATFORGE_CANDLE_CLASS(CDLThreeBlackCrows, ThreeBlackCrows);
STRATFORGE_CANDLE_CLASS(CDLPiercingLine, Piercing);
STRATFORGE_CANDLE_PENETRATION_CLASS(CDLDarkCloudCover, DarkCloudCover, 0.5);
STRATFORGE_CANDLE_PENETRATION_CLASS(CDLAbandonedBaby, AbandonedBaby, 0.3);
STRATFORGE_CANDLE_CLASS(CDLThreeInside, ThreeInside);
STRATFORGE_CANDLE_CLASS(CDLThreeOutside, ThreeOutside);
STRATFORGE_CANDLE_CLASS(CDLKicking, Kicking);
STRATFORGE_CANDLE_CLASS(CDLBeltHold, BeltHold);
STRATFORGE_CANDLE_CLASS(CDLCounterAttack, CounterAttack);
STRATFORGE_CANDLE_CLASS(CDLHomingPigeon, HomingPigeon);
STRATFORGE_CANDLE_CLASS(CDLMatchingLow, MatchingLow);
STRATFORGE_CANDLE_CLASS(CDLTasukiGap, TasukiGap);
STRATFORGE_CANDLE_CLASS(CDLUpsideGap, UpsideGapTwoCrows);
STRATFORGE_CANDLE_CLASS(CDLTwoCrows, TwoCrows);
STRATFORGE_CANDLE_CLASS(CDLThreeLineStrike, ThreeLineStrike);
STRATFORGE_CANDLE_CLASS(CDLThreeStarsInSouth, ThreeStarsInSouth);
STRATFORGE_CANDLE_CLASS(CDLAdvanceBlock, AdvanceBlock);
STRATFORGE_CANDLE_CLASS(CDLBreakaway, Breakaway);
STRATFORGE_CANDLE_CLASS(CDLClosingMarubozu, ClosingMarubozu);
STRATFORGE_CANDLE_CLASS(CDLConcealingBabySwallow, ConcealingBabySwallow);
STRATFORGE_CANDLE_CLASS(CDLDojiStar, DojiStar);
STRATFORGE_CANDLE_CLASS(CDLDragonflyDoji, DragonflyDoji);
STRATFORGE_CANDLE_PENETRATION_CLASS(CDLEveningDojiStar, EveningDojiStar, 0.3);
STRATFORGE_CANDLE_CLASS(CDLGapSideSideWhite, GapSideSideWhite);
STRATFORGE_CANDLE_CLASS(CDLGravestoneDoji, GravestoneDoji);
STRATFORGE_CANDLE_CLASS(CDLHaramiCross, HaramiCross);
STRATFORGE_CANDLE_CLASS(CDLHighWave, HighWave);
STRATFORGE_CANDLE_CLASS(CDLHikkake, Hikkake);
STRATFORGE_CANDLE_CLASS(CDLModifiedHikkake, ModifiedHikkake);
STRATFORGE_CANDLE_CLASS(CDLIdenticalThreeCrows, IdenticalThreeCrows);
STRATFORGE_CANDLE_CLASS(CDLInNeck, InNeck);
STRATFORGE_CANDLE_CLASS(CDLInvertedHammer, InvertedHammer);
STRATFORGE_CANDLE_CLASS(CDLKickingByLength, KickingByLength);
STRATFORGE_CANDLE_CLASS(CDLLadderBottom, LadderBottom);
STRATFORGE_CANDLE_CLASS(CDLLongLeggedDoji, LongLeggedDoji);
STRATFORGE_CANDLE_CLASS(CDLLongLine, LongLine);
STRATFORGE_CANDLE_PENETRATION_CLASS(CDLMatHold, MatHold, 0.5);
STRATFORGE_CANDLE_PENETRATION_CLASS(CDLMorningDojiStar, MorningDojiStar, 0.3);
STRATFORGE_CANDLE_CLASS(CDLOnNeck, OnNeck);
STRATFORGE_CANDLE_CLASS(CDLRickshawMan, RickshawMan);
STRATFORGE_CANDLE_CLASS(CDLRiseFallThreeMethods, RiseFallThreeMethods);
STRATFORGE_CANDLE_CLASS(CDLSeparatingLines, SeparatingLines);
STRATFORGE_CANDLE_CLASS(CDLShortLine, ShortLine);
STRATFORGE_CANDLE_CLASS(CDLStalledPattern, StalledPattern);
STRATFORGE_CANDLE_CLASS(CDLStickSandwich, StickSandwich);
STRATFORGE_CANDLE_CLASS(CDLTakuri, Takuri);
STRATFORGE_CANDLE_CLASS(CDLThrusting, Thrusting);
STRATFORGE_CANDLE_CLASS(CDLTristar, Tristar);
STRATFORGE_CANDLE_CLASS(CDLUniqueThreeRiver, UniqueThreeRiver);
STRATFORGE_CANDLE_CLASS(CDLXSideGapThreeMethods, XSideGapThreeMethods);

#undef STRATFORGE_CANDLE_CLASS
#undef STRATFORGE_CANDLE_PENETRATION_CLASS

} // namespace stratforge
