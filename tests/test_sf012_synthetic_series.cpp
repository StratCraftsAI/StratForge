// SPDX-License-Identifier: MIT
// TICKET_SF012 WP9: test-only variable-cardinality Renko architecture spike.

#include <catch2/catch_test_macros.hpp>

#include <stratforge/bar.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

struct BrickId {
    std::uint64_t source_sequence{};
    std::uint32_t source_ordinal{};
    friend constexpr bool operator==(const BrickId&, const BrickId&) = default;
};

struct RenkoBrick {
    BrickId id{};
    stratforge::DateTime timestamp{};
    double open{};
    double close{};
    int direction{};
    friend constexpr bool operator==(const RenkoBrick&, const RenkoBrick&) = default;
};

struct RenkoCoreState {
    bool anchored{};
    double last_close{};
    int direction{};
    friend constexpr bool operator==(const RenkoCoreState&, const RenkoCoreState&) = default;
};

struct RenkoSnapshot {
    std::uint32_t version{1};
    double brick_size{};
    std::uint64_t next_source_sequence{};
    RenkoCoreState core{};
    std::optional<RenkoCoreState> before_last{};
    std::optional<std::uint64_t> last_source_sequence{};
    std::optional<stratforge::DateTime> last_source_timestamp{};
    std::vector<RenkoBrick> last_emitted;
    friend bool operator==(const RenkoSnapshot&, const RenkoSnapshot&) = default;
};

struct ChangeSet {
    std::vector<BrickId> removed;
    std::vector<RenkoBrick> inserted;
};

class RenkoPrototype {
public:
    explicit RenkoPrototype(double brick_size) : brick_size_(brick_size) {
        if (!std::isfinite(brick_size_) || brick_size_ <= 0.0) {
            throw std::invalid_argument("brick size must be positive and finite");
        }
    }

    [[nodiscard]] ChangeSet append(const stratforge::Bar& source) {
        validate(source);
        const auto sequence = next_source_sequence_;
        const auto before = core_;
        auto inserted = generate(source, sequence, core_);
        ++next_source_sequence_;
        before_last_ = before;
        last_source_sequence_ = sequence;
        last_source_timestamp_ = source.timestamp;
        last_emitted_ = inserted;
        return {{}, std::move(inserted)};
    }

    [[nodiscard]] ChangeSet replace_last(const stratforge::Bar& source) {
        validate(source);
        if (!before_last_ || !last_source_sequence_ || !last_source_timestamp_) {
            throw std::logic_error("there is no source bar to replace");
        }
        if (source.timestamp != *last_source_timestamp_) {
            throw std::invalid_argument("replacement timestamp must match the last source");
        }
        ChangeSet change;
        for (const auto& brick : last_emitted_) {
            change.removed.push_back(brick.id);
        }
        auto replacement_state = *before_last_;
        change.inserted = generate(source, *last_source_sequence_, replacement_state);
        core_ = replacement_state;
        last_emitted_ = change.inserted;
        return change;
    }

    [[nodiscard]] RenkoSnapshot snapshot() const {
        return {1, brick_size_, next_source_sequence_, core_, before_last_,
                last_source_sequence_, last_source_timestamp_, last_emitted_};
    }

    void restore(const RenkoSnapshot& snapshot) {
        if (snapshot.version != 1 || snapshot.brick_size != brick_size_) {
            throw std::invalid_argument("incompatible Renko snapshot");
        }
        next_source_sequence_ = snapshot.next_source_sequence;
        core_ = snapshot.core;
        before_last_ = snapshot.before_last;
        last_source_sequence_ = snapshot.last_source_sequence;
        last_source_timestamp_ = snapshot.last_source_timestamp;
        last_emitted_ = snapshot.last_emitted;
    }

private:
    static void validate(const stratforge::Bar& bar) {
        if (!std::isfinite(bar.open) || !std::isfinite(bar.high) ||
            !std::isfinite(bar.low) || !std::isfinite(bar.close) ||
            bar.high < bar.low || bar.open < bar.low || bar.open > bar.high ||
            bar.close < bar.low || bar.close > bar.high) {
            throw std::invalid_argument("invalid source OHLC");
        }
    }

    [[nodiscard]] std::vector<RenkoBrick> generate(
        const stratforge::Bar& source, std::uint64_t sequence, RenkoCoreState& state) const {
        std::vector<RenkoBrick> result;
        if (!state.anchored) {
            state = {true, source.close, 0};
            return result;
        }

        auto emit = [&](double open, double close, int direction) {
            result.push_back({{sequence, static_cast<std::uint32_t>(result.size())},
                              source.timestamp, open, close, direction});
            state.last_close = close;
            state.direction = direction;
        };

        if (state.direction >= 0 && source.close >= state.last_close + brick_size_) {
            while (source.close >= state.last_close + brick_size_) {
                emit(state.last_close, state.last_close + brick_size_, 1);
            }
            return result;
        }
        if (state.direction <= 0 && source.close <= state.last_close - brick_size_) {
            while (source.close <= state.last_close - brick_size_) {
                emit(state.last_close, state.last_close - brick_size_, -1);
            }
            return result;
        }
        if (state.direction == 1 && source.close <= state.last_close - 2.0 * brick_size_) {
            emit(state.last_close - brick_size_, state.last_close - 2.0 * brick_size_, -1);
            while (source.close <= state.last_close - brick_size_) {
                emit(state.last_close, state.last_close - brick_size_, -1);
            }
        } else if (state.direction == -1 &&
                   source.close >= state.last_close + 2.0 * brick_size_) {
            emit(state.last_close + brick_size_, state.last_close + 2.0 * brick_size_, 1);
            while (source.close >= state.last_close + brick_size_) {
                emit(state.last_close, state.last_close + brick_size_, 1);
            }
        }
        return result;
    }

    double brick_size_;
    std::uint64_t next_source_sequence_{};
    RenkoCoreState core_{};
    std::optional<RenkoCoreState> before_last_;
    std::optional<std::uint64_t> last_source_sequence_;
    std::optional<stratforge::DateTime> last_source_timestamp_;
    std::vector<RenkoBrick> last_emitted_;
};

[[nodiscard]] stratforge::Bar bar(double close, std::int64_t seconds = 0) {
    return {stratforge::DateTime{std::chrono::seconds{seconds}}, close, close, close,
            close, 0.0};
}

}  // namespace

TEST_CASE("WP9 Renko emits zero or many ordered bricks with source timestamps",
          "[sf012][synthetic_series]") {
    RenkoPrototype renko{1.0};
    REQUIRE(renko.append(bar(10.0)).inserted.empty());
    REQUIRE(renko.append(bar(10.5, 1)).inserted.empty());

    const auto jump = renko.append(bar(13.0, 2));
    REQUIRE(jump.inserted.size() == 3);
    REQUIRE(jump.inserted[0].open == 10.0);
    REQUIRE(jump.inserted[2].close == 13.0);
    REQUIRE(jump.inserted[0].id == BrickId{2, 0});
    REQUIRE(jump.inserted[2].id == BrickId{2, 2});
    for (const auto& brick : jump.inserted) {
        REQUIRE(brick.timestamp == stratforge::DateTime{std::chrono::seconds{2}});
        REQUIRE(brick.direction == 1);
    }
}

TEST_CASE("WP9 Renko applies inclusive two-brick reversal and supports negative prices",
          "[sf012][synthetic_series]") {
    RenkoPrototype renko{1.0};
    static_cast<void>(renko.append(bar(-2.0)));
    static_cast<void>(renko.append(bar(0.0, 1)));

    const auto reversal = renko.append(bar(-3.0, 2));
    REQUIRE(reversal.inserted.size() == 2);
    REQUIRE(reversal.inserted[0].open == -1.0);
    REQUIRE(reversal.inserted[0].close == -2.0);
    REQUIRE(reversal.inserted[1].open == -2.0);
    REQUIRE(reversal.inserted[1].close == -3.0);
}

TEST_CASE("WP9 Renko replacement retracts and deterministically reuses source IDs",
          "[sf012][synthetic_series]") {
    RenkoPrototype renko{1.0};
    static_cast<void>(renko.append(bar(10.0)));
    const auto original = renko.append(bar(13.0, 1));
    REQUIRE(original.inserted.size() == 3);

    const auto replacement = renko.replace_last(bar(12.0, 1));
    REQUIRE(replacement.removed ==
            std::vector<BrickId>{{1, 0}, {1, 1}, {1, 2}});
    REQUIRE(replacement.inserted.size() == 2);
    REQUIRE(replacement.inserted[0].id == BrickId{1, 0});
    REQUIRE(replacement.inserted[1].id == BrickId{1, 1});

    const auto next = renko.append(bar(13.0, 2));
    REQUIRE(next.inserted.size() == 1);
    REQUIRE(next.inserted[0].id == BrickId{2, 0});
}

TEST_CASE("WP9 Renko snapshot restore matches replay and preserves replacement",
          "[sf012][synthetic_series]") {
    RenkoPrototype original{0.5};
    static_cast<void>(original.append(bar(10.0)));
    static_cast<void>(original.append(bar(11.0, 1)));
    const auto saved = original.snapshot();

    RenkoPrototype restored{0.5};
    restored.restore(saved);
    REQUIRE(restored.snapshot() == saved);
    REQUIRE(restored.replace_last(bar(10.5, 1)).inserted.size() == 1);

    RenkoPrototype replayed{0.5};
    static_cast<void>(replayed.append(bar(10.0)));
    static_cast<void>(replayed.append(bar(10.5, 1)));
    REQUIRE(restored.snapshot() == replayed.snapshot());
}

TEST_CASE("WP9 Renko rejects invalid input atomically", "[sf012][synthetic_series]") {
    RenkoPrototype renko{1.0};
    static_cast<void>(renko.append(bar(10.0)));
    const auto before = renko.snapshot();
    auto invalid = bar(10.0, 1);
    invalid.high = 9.0;

    REQUIRE_THROWS_AS(renko.append(invalid), std::invalid_argument);
    REQUIRE(renko.snapshot() == before);
    const auto next = renko.append(bar(11.0, 1));
    REQUIRE(next.inserted[0].id == BrickId{1, 0});
}

TEST_CASE("WP9 Renko rejects a replacement for a different source timestamp",
          "[sf012][synthetic_series]") {
    RenkoPrototype renko{1.0};
    static_cast<void>(renko.append(bar(10.0)));
    static_cast<void>(renko.append(bar(11.0, 1)));
    const auto before = renko.snapshot();

    REQUIRE_THROWS_AS(renko.replace_last(bar(12.0, 2)), std::invalid_argument);
    REQUIRE(renko.snapshot() == before);
}
