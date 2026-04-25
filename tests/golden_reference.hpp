#pragma once

#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace stratforge::test {

struct GoldenBar {
    std::string date;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    double volume = 0.0;
    double openinterest = 0.0;
};

struct CsvGoldenReference {
    std::string data_file;
    std::size_t expected_rows = 0;
    GoldenBar first_bar;
    GoldenBar last_bar;
};

struct IndicatorGoldenReference {
    std::string indicator;
    std::string data_file;
    std::size_t bars = 0;
    std::size_t warmup_bars = 0;
    std::size_t period = 0;
    std::size_t maperiod = 0;
    std::size_t lookback = 0;
    std::size_t period1 = 0;
    std::size_t period2 = 0;
    std::size_t pchange = 0;
    std::vector<std::string> values;
};

struct BollingerGoldenReference {
    std::string data_file;
    std::size_t bars = 0;
    std::size_t warmup_bars = 0;
    std::size_t period = 0;
    std::vector<std::string> mid;
    std::vector<std::string> top;
    std::vector<std::string> bottom;
};

struct MacdGoldenReference {
    std::string data_file;
    std::size_t bars = 0;
    std::size_t warmup_bars = 0;
    std::size_t fast_period = 0;
    std::size_t slow_period = 0;
    std::size_t signal_period = 0;
    std::vector<std::string> macd;
    std::vector<std::string> signal;
    std::vector<std::string> histogram;
};

struct AroonGoldenReference {
    std::string data_file;
    std::size_t bars = 0;
    std::size_t warmup_bars = 0;
    std::size_t period = 0;
    std::vector<std::string> up;
    std::vector<std::string> down;
    std::vector<std::string> oscillator;
};

struct StochasticGoldenReference {
    std::string data_file;
    std::size_t bars = 0;
    std::size_t warmup_bars = 0;
    std::size_t period = 0;
    std::size_t period_dfast = 0;
    std::size_t period_dslow = 0;
    std::vector<std::string> percK;
    std::vector<std::string> percD;
    std::vector<std::string> fullK;
    std::vector<std::string> fullD;
    std::vector<std::string> fullDSlow;
};

struct DirectionalMovementGoldenReference {
    std::string data_file;
    std::size_t bars = 0;
    std::size_t warmup_bars = 0;
    std::size_t period = 0;
    std::vector<std::string> adx;
    std::vector<std::string> adxr;
    std::vector<std::string> plus_di;
    std::vector<std::string> minus_di;
};

struct CrossOverGoldenReference {
    std::string data_file;
    std::size_t bars = 0;
    std::size_t warmup_bars = 0;
    std::size_t fast_period = 0;
    std::size_t slow_period = 0;
    std::vector<std::string> values;
};

struct AdxrGoldenReference {
    std::string data_file;
    std::size_t bars = 0;
    std::size_t warmup_bars = 0;
    std::size_t period = 0;
    std::vector<std::string> adxr;
    std::vector<std::string> plus_di;
    std::vector<std::string> minus_di;
};

struct HeikinAshiGoldenReference {
    std::string data_file;
    std::size_t bars = 0;
    std::vector<std::string> open;
    std::vector<std::string> high;
    std::vector<std::string> low;
    std::vector<std::string> close;
};

struct VortexGoldenReference {
    std::string data_file;
    std::size_t bars = 0;
    std::size_t warmup_bars = 0;
    std::size_t period = 0;
    std::vector<std::string> vi_plus;
    std::vector<std::string> vi_minus;
};

struct AnalyticsGoldenReference {
    struct TradeAnalyzerReference {
        struct Total {
            std::size_t total = 0;
            std::size_t open = 0;
            std::size_t closed = 0;
        } total;
        struct Streak {
            std::size_t current = 0;
            std::size_t longest = 0;
        };
        struct PnlSummary {
            double total = 0.0;
            double average = 0.0;
            double max = 0.0;
        };
        struct Pnl {
            PnlSummary gross;
            PnlSummary net;
        } pnl;
        struct {
            Streak won;
            Streak lost;
        } streak;
        struct {
            std::size_t total = 0;
            PnlSummary pnl;
        } won, lost;
        struct {
            std::size_t total = 0;
            double average = 0.0;
            std::size_t max = 0;
            std::size_t min = 0;
        } len;
    } trade_analyzer;

    struct DrawdownReference {
        std::size_t len = 0;
        double drawdown = 0.0;
        double moneydown = 0.0;
        struct {
            std::size_t len = 0;
            double drawdown = 0.0;
            double moneydown = 0.0;
        } max;
    } drawdown;

    struct ReturnsReference {
        double rtot = 0.0;
        double ravg = 0.0;
        double rnorm = 0.0;
        double rnorm100 = 0.0;
    } returns;

    std::string scenario;
    double initial_cash = 0.0;
    double sharpe_ratio = 0.0;
    std::vector<std::string> buy;
    std::vector<std::string> sell;
    std::vector<std::string> value;
};

class JsonCursor {
public:
    explicit JsonCursor(std::string_view input) : input_(input) {}

    void skip_ws() {
        while (pos_ < input_.size() &&
               std::isspace(static_cast<unsigned char>(input_[pos_])) != 0) {
            ++pos_;
        }
    }

    void expect(char ch) {
        skip_ws();
        if (pos_ >= input_.size() || input_[pos_] != ch) {
            throw std::runtime_error("Malformed golden reference JSON");
        }
        ++pos_;
    }

    [[nodiscard]] bool consume(char ch) {
        skip_ws();
        if (pos_ < input_.size() && input_[pos_] == ch) {
            ++pos_;
            return true;
        }
        return false;
    }

    [[nodiscard]] char peek() {
        skip_ws();
        if (pos_ >= input_.size()) {
            throw std::runtime_error("Unexpected end of JSON input");
        }
        return input_[pos_];
    }

    [[nodiscard]] std::string parse_string() {
        skip_ws();
        if (pos_ >= input_.size() || input_[pos_] != '"') {
            throw std::runtime_error("Expected JSON string");
        }

        ++pos_;
        std::string result;
        while (pos_ < input_.size()) {
            const char ch = input_[pos_++];
            if (ch == '"') {
                return result;
            }

            if (ch == '\\') {
                if (pos_ >= input_.size()) {
                    throw std::runtime_error("Invalid escape sequence");
                }

                const char escaped = input_[pos_++];
                switch (escaped) {
                case '"':
                case '\\':
                case '/':
                    result.push_back(escaped);
                    break;
                case 'b':
                    result.push_back('\b');
                    break;
                case 'f':
                    result.push_back('\f');
                    break;
                case 'n':
                    result.push_back('\n');
                    break;
                case 'r':
                    result.push_back('\r');
                    break;
                case 't':
                    result.push_back('\t');
                    break;
                default:
                    throw std::runtime_error("Unsupported JSON escape");
                }
                continue;
            }

            result.push_back(ch);
        }

        throw std::runtime_error("Unterminated JSON string");
    }

    [[nodiscard]] double parse_number() {
        skip_ws();
        const auto start = pos_;
        if (pos_ < input_.size() && (input_[pos_] == '-' || input_[pos_] == '+')) {
            ++pos_;
        }

        while (pos_ < input_.size()) {
            const char ch = input_[pos_];
            if ((ch >= '0' && ch <= '9') || ch == '.' || ch == 'e' || ch == 'E' ||
                ch == '-' || ch == '+') {
                ++pos_;
                continue;
            }
            break;
        }

        const std::string token(input_.substr(start, pos_ - start));
        if (token.empty()) {
            throw std::runtime_error("Expected JSON number");
        }

        char* end = nullptr;
        const double value = std::strtod(token.c_str(), &end);
        if (end == nullptr || *end != '\0') {
            throw std::runtime_error("Invalid JSON number");
        }
        return value;
    }

private:
    std::string_view input_;
    std::size_t pos_ = 0;
};

inline GoldenBar parse_bar(JsonCursor& cursor) {
    GoldenBar bar;
    cursor.expect('{');

    while (!cursor.consume('}')) {
        const auto key = cursor.parse_string();
        cursor.expect(':');

        if (key == "date") {
            bar.date = cursor.parse_string();
        } else if (key == "open") {
            bar.open = cursor.parse_number();
        } else if (key == "high") {
            bar.high = cursor.parse_number();
        } else if (key == "low") {
            bar.low = cursor.parse_number();
        } else if (key == "close") {
            bar.close = cursor.parse_number();
        } else if (key == "volume") {
            bar.volume = cursor.parse_number();
        } else if (key == "openinterest") {
            bar.openinterest = cursor.parse_number();
        } else {
            throw std::runtime_error("Unexpected golden bar key");
        }

        static_cast<void>(cursor.consume(','));
    }

    return bar;
}

inline CsvGoldenReference load_csv_golden_reference(const std::string& fixture_path) {
    std::ifstream input(fixture_path);
    if (!input.is_open()) {
        throw std::runtime_error("Unable to open golden fixture: " + fixture_path);
    }

    const std::string json((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    JsonCursor cursor(json);
    CsvGoldenReference fixture;

    cursor.expect('{');
    while (!cursor.consume('}')) {
        const auto key = cursor.parse_string();
        cursor.expect(':');

        if (key == "data_file") {
            fixture.data_file = cursor.parse_string();
        } else if (key == "expected_rows") {
            fixture.expected_rows = static_cast<std::size_t>(cursor.parse_number());
        } else if (key == "first_bar") {
            fixture.first_bar = parse_bar(cursor);
        } else if (key == "last_bar") {
            fixture.last_bar = parse_bar(cursor);
        } else {
            throw std::runtime_error("Unexpected golden fixture key");
        }

        static_cast<void>(cursor.consume(','));
    }

    return fixture;
}

inline std::vector<std::string> parse_string_array(JsonCursor& cursor) {
    std::vector<std::string> values;
    cursor.expect('[');

    while (!cursor.consume(']')) {
        values.push_back(cursor.parse_string());
        static_cast<void>(cursor.consume(','));
    }

    return values;
}

inline void skip_json_value(JsonCursor& cursor) {
    cursor.skip_ws();
    if (cursor.consume('{')) {
        while (!cursor.consume('}')) {
            static_cast<void>(cursor.parse_string());
            cursor.expect(':');
            skip_json_value(cursor);
            static_cast<void>(cursor.consume(','));
        }
        return;
    }

    if (cursor.consume('[')) {
        while (!cursor.consume(']')) {
            skip_json_value(cursor);
            static_cast<void>(cursor.consume(','));
        }
        return;
    }

    const char next = cursor.peek();
    if (next == '"') {
        static_cast<void>(cursor.parse_string());
        return;
    }

    if ((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z')) {
        while (true) {
            const char ch = cursor.peek();
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) {
                static_cast<void>(cursor.consume(ch));
                continue;
            }
            break;
        }
        return;
    }

    static_cast<void>(cursor.parse_number());
}

inline IndicatorGoldenReference load_indicator_golden_reference(const std::string& fixture_path) {
    std::ifstream input(fixture_path);
    if (!input.is_open()) {
        throw std::runtime_error("Unable to open golden fixture: " + fixture_path);
    }

    const std::string json((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    JsonCursor cursor(json);
    IndicatorGoldenReference fixture;

    cursor.expect('{');
    while (!cursor.consume('}')) {
        const auto key = cursor.parse_string();
        cursor.expect(':');

        if (key == "indicator") {
            fixture.indicator = cursor.parse_string();
        } else if (key == "data_file") {
            fixture.data_file = cursor.parse_string();
        } else if (key == "bars") {
            fixture.bars = static_cast<std::size_t>(cursor.parse_number());
        } else if (key == "warmup_bars") {
            fixture.warmup_bars = static_cast<std::size_t>(cursor.parse_number());
        } else if (key == "values") {
            fixture.values = parse_string_array(cursor);
        } else if (key == "params") {
            cursor.expect('{');
            while (!cursor.consume('}')) {
                const auto param_key = cursor.parse_string();
                cursor.expect(':');
                if (param_key == "period") {
                    fixture.period = static_cast<std::size_t>(cursor.parse_number());
                } else if (param_key == "maperiod") {
                    fixture.maperiod = static_cast<std::size_t>(cursor.parse_number());
                } else if (param_key == "lookback") {
                    fixture.lookback = static_cast<std::size_t>(cursor.parse_number());
                } else if (param_key == "period1") {
                    fixture.period1 = static_cast<std::size_t>(cursor.parse_number());
                } else if (param_key == "period2") {
                    fixture.period2 = static_cast<std::size_t>(cursor.parse_number());
                } else if (param_key == "pchange") {
                    fixture.pchange = static_cast<std::size_t>(cursor.parse_number());
                } else {
                    skip_json_value(cursor);
                }
                static_cast<void>(cursor.consume(','));
            }
        } else {
            skip_json_value(cursor);
        }

        static_cast<void>(cursor.consume(','));
    }

    return fixture;
}

inline BollingerGoldenReference load_bollinger_golden_reference(const std::string& fixture_path) {
    std::ifstream input(fixture_path);
    if (!input.is_open()) {
        throw std::runtime_error("Unable to open golden fixture: " + fixture_path);
    }

    const std::string json((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    JsonCursor cursor(json);
    BollingerGoldenReference fixture;

    cursor.expect('{');
    while (!cursor.consume('}')) {
        const auto key = cursor.parse_string();
        cursor.expect(':');

        if (key == "data_file") {
            fixture.data_file = cursor.parse_string();
        } else if (key == "bars") {
            fixture.bars = static_cast<std::size_t>(cursor.parse_number());
        } else if (key == "warmup_bars") {
            fixture.warmup_bars = static_cast<std::size_t>(cursor.parse_number());
        } else if (key == "mid") {
            fixture.mid = parse_string_array(cursor);
        } else if (key == "top") {
            fixture.top = parse_string_array(cursor);
        } else if (key == "bottom") {
            fixture.bottom = parse_string_array(cursor);
        } else if (key == "params") {
            cursor.expect('{');
            while (!cursor.consume('}')) {
                const auto param_key = cursor.parse_string();
                cursor.expect(':');
                if (param_key == "period") {
                    fixture.period = static_cast<std::size_t>(cursor.parse_number());
                } else {
                    skip_json_value(cursor);
                }
                static_cast<void>(cursor.consume(','));
            }
        } else {
            skip_json_value(cursor);
        }

        static_cast<void>(cursor.consume(','));
    }

    return fixture;
}

inline MacdGoldenReference load_macd_golden_reference(const std::string& fixture_path) {
    std::ifstream input(fixture_path);
    if (!input.is_open()) {
        throw std::runtime_error("Unable to open golden fixture: " + fixture_path);
    }

    const std::string json((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    JsonCursor cursor(json);
    MacdGoldenReference fixture;

    cursor.expect('{');
    while (!cursor.consume('}')) {
        const auto key = cursor.parse_string();
        cursor.expect(':');

        if (key == "data_file") {
            fixture.data_file = cursor.parse_string();
        } else if (key == "bars") {
            fixture.bars = static_cast<std::size_t>(cursor.parse_number());
        } else if (key == "warmup_bars") {
            fixture.warmup_bars = static_cast<std::size_t>(cursor.parse_number());
        } else if (key == "macd") {
            fixture.macd = parse_string_array(cursor);
        } else if (key == "signal") {
            fixture.signal = parse_string_array(cursor);
        } else if (key == "histogram") {
            fixture.histogram = parse_string_array(cursor);
        } else if (key == "params") {
            cursor.expect('{');
            while (!cursor.consume('}')) {
                const auto param_key = cursor.parse_string();
                cursor.expect(':');
                if (param_key == "fast_period") {
                    fixture.fast_period = static_cast<std::size_t>(cursor.parse_number());
                } else if (param_key == "slow_period") {
                    fixture.slow_period = static_cast<std::size_t>(cursor.parse_number());
                } else if (param_key == "signal_period") {
                    fixture.signal_period = static_cast<std::size_t>(cursor.parse_number());
                } else {
                    skip_json_value(cursor);
                }
                static_cast<void>(cursor.consume(','));
            }
        } else {
            skip_json_value(cursor);
        }

        static_cast<void>(cursor.consume(','));
    }

    return fixture;
}

inline AroonGoldenReference load_aroon_golden_reference(const std::string& fixture_path) {
    std::ifstream input(fixture_path);
    if (!input.is_open()) {
        throw std::runtime_error("Unable to open golden fixture: " + fixture_path);
    }

    const std::string json((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    JsonCursor cursor(json);
    AroonGoldenReference fixture;

    cursor.expect('{');
    while (!cursor.consume('}')) {
        const auto key = cursor.parse_string();
        cursor.expect(':');

        if (key == "data_file") {
            fixture.data_file = cursor.parse_string();
        } else if (key == "bars") {
            fixture.bars = static_cast<std::size_t>(cursor.parse_number());
        } else if (key == "warmup_bars") {
            fixture.warmup_bars = static_cast<std::size_t>(cursor.parse_number());
        } else if (key == "up") {
            fixture.up = parse_string_array(cursor);
        } else if (key == "down") {
            fixture.down = parse_string_array(cursor);
        } else if (key == "oscillator") {
            fixture.oscillator = parse_string_array(cursor);
        } else if (key == "params") {
            cursor.expect('{');
            while (!cursor.consume('}')) {
                const auto param_key = cursor.parse_string();
                cursor.expect(':');
                if (param_key == "period") {
                    fixture.period = static_cast<std::size_t>(cursor.parse_number());
                } else {
                    skip_json_value(cursor);
                }
                static_cast<void>(cursor.consume(','));
            }
        } else {
            skip_json_value(cursor);
        }

        static_cast<void>(cursor.consume(','));
    }

    return fixture;
}

inline StochasticGoldenReference load_stochastic_golden_reference(const std::string& fixture_path) {
    std::ifstream input(fixture_path);
    if (!input.is_open()) {
        throw std::runtime_error("Unable to open golden fixture: " + fixture_path);
    }

    const std::string json((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    JsonCursor cursor(json);
    StochasticGoldenReference fixture;

    cursor.expect('{');
    while (!cursor.consume('}')) {
        const auto key = cursor.parse_string();
        cursor.expect(':');

        if (key == "data_file") {
            fixture.data_file = cursor.parse_string();
        } else if (key == "bars") {
            fixture.bars = static_cast<std::size_t>(cursor.parse_number());
        } else if (key == "warmup_bars") {
            fixture.warmup_bars = static_cast<std::size_t>(cursor.parse_number());
        } else if (key == "percK") {
            fixture.percK = parse_string_array(cursor);
        } else if (key == "percD") {
            fixture.percD = parse_string_array(cursor);
        } else if (key == "fullK") {
            fixture.fullK = parse_string_array(cursor);
        } else if (key == "fullD") {
            fixture.fullD = parse_string_array(cursor);
        } else if (key == "fullDSlow") {
            fixture.fullDSlow = parse_string_array(cursor);
        } else if (key == "params") {
            cursor.expect('{');
            while (!cursor.consume('}')) {
                const auto param_key = cursor.parse_string();
                cursor.expect(':');
                if (param_key == "period") {
                    fixture.period = static_cast<std::size_t>(cursor.parse_number());
                } else if (param_key == "period_dfast") {
                    fixture.period_dfast = static_cast<std::size_t>(cursor.parse_number());
                } else if (param_key == "period_dslow") {
                    fixture.period_dslow = static_cast<std::size_t>(cursor.parse_number());
                } else {
                    skip_json_value(cursor);
                }
                static_cast<void>(cursor.consume(','));
            }
        } else {
            skip_json_value(cursor);
        }

        static_cast<void>(cursor.consume(','));
    }

    return fixture;
}

inline DirectionalMovementGoldenReference load_directionalmovement_golden_reference(const std::string& fixture_path) {
    std::ifstream input(fixture_path);
    if (!input.is_open()) {
        throw std::runtime_error("Unable to open golden fixture: " + fixture_path);
    }

    const std::string json((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    JsonCursor cursor(json);
    DirectionalMovementGoldenReference fixture;

    cursor.expect('{');
    while (!cursor.consume('}')) {
        const auto key = cursor.parse_string();
        cursor.expect(':');

        if (key == "data_file") {
            fixture.data_file = cursor.parse_string();
        } else if (key == "bars") {
            fixture.bars = static_cast<std::size_t>(cursor.parse_number());
        } else if (key == "warmup_bars") {
            fixture.warmup_bars = static_cast<std::size_t>(cursor.parse_number());
        } else if (key == "adx") {
            fixture.adx = parse_string_array(cursor);
        } else if (key == "adxr") {
            fixture.adxr = parse_string_array(cursor);
        } else if (key == "plus_di") {
            fixture.plus_di = parse_string_array(cursor);
        } else if (key == "minus_di") {
            fixture.minus_di = parse_string_array(cursor);
        } else if (key == "params") {
            cursor.expect('{');
            while (!cursor.consume('}')) {
                const auto param_key = cursor.parse_string();
                cursor.expect(':');
                if (param_key == "period") {
                    fixture.period = static_cast<std::size_t>(cursor.parse_number());
                } else {
                    skip_json_value(cursor);
                }
                static_cast<void>(cursor.consume(','));
            }
        } else {
            skip_json_value(cursor);
        }

        static_cast<void>(cursor.consume(','));
    }

    return fixture;
}

inline CrossOverGoldenReference load_crossover_golden_reference(const std::string& fixture_path) {
    std::ifstream input(fixture_path);
    if (!input.is_open()) {
        throw std::runtime_error("Unable to open golden fixture: " + fixture_path);
    }

    const std::string json((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    JsonCursor cursor(json);
    CrossOverGoldenReference fixture;

    cursor.expect('{');
    while (!cursor.consume('}')) {
        const auto key = cursor.parse_string();
        cursor.expect(':');

        if (key == "data_file") {
            fixture.data_file = cursor.parse_string();
        } else if (key == "bars") {
            fixture.bars = static_cast<std::size_t>(cursor.parse_number());
        } else if (key == "warmup_bars") {
            fixture.warmup_bars = static_cast<std::size_t>(cursor.parse_number());
        } else if (key == "values") {
            fixture.values = parse_string_array(cursor);
        } else if (key == "params") {
            cursor.expect('{');
            while (!cursor.consume('}')) {
                const auto param_key = cursor.parse_string();
                cursor.expect(':');
                if (param_key == "fast_period") {
                    fixture.fast_period = static_cast<std::size_t>(cursor.parse_number());
                } else if (param_key == "slow_period") {
                    fixture.slow_period = static_cast<std::size_t>(cursor.parse_number());
                } else {
                    skip_json_value(cursor);
                }
                static_cast<void>(cursor.consume(','));
            }
        } else {
            skip_json_value(cursor);
        }

        static_cast<void>(cursor.consume(','));
    }

    return fixture;
}

inline AdxrGoldenReference load_adxr_golden_reference(const std::string& fixture_path) {
    std::ifstream input(fixture_path);
    if (!input.is_open()) {
        throw std::runtime_error("Unable to open golden fixture: " + fixture_path);
    }

    const std::string json((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    JsonCursor cursor(json);
    AdxrGoldenReference fixture;

    cursor.expect('{');
    while (!cursor.consume('}')) {
        const auto key = cursor.parse_string();
        cursor.expect(':');

        if (key == "data_file") {
            fixture.data_file = cursor.parse_string();
        } else if (key == "bars") {
            fixture.bars = static_cast<std::size_t>(cursor.parse_number());
        } else if (key == "warmup_bars") {
            fixture.warmup_bars = static_cast<std::size_t>(cursor.parse_number());
        } else if (key == "adxr") {
            fixture.adxr = parse_string_array(cursor);
        } else if (key == "plus_di") {
            fixture.plus_di = parse_string_array(cursor);
        } else if (key == "minus_di") {
            fixture.minus_di = parse_string_array(cursor);
        } else if (key == "params") {
            cursor.expect('{');
            while (!cursor.consume('}')) {
                const auto param_key = cursor.parse_string();
                cursor.expect(':');
                if (param_key == "period") {
                    fixture.period = static_cast<std::size_t>(cursor.parse_number());
                } else {
                    skip_json_value(cursor);
                }
                static_cast<void>(cursor.consume(','));
            }
        } else {
            skip_json_value(cursor);
        }

        static_cast<void>(cursor.consume(','));
    }

    return fixture;
}

inline HeikinAshiGoldenReference load_heikinashi_golden_reference(const std::string& fixture_path) {
    std::ifstream input(fixture_path);
    if (!input.is_open()) {
        throw std::runtime_error("Unable to open golden fixture: " + fixture_path);
    }

    const std::string json((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    JsonCursor cursor(json);
    HeikinAshiGoldenReference fixture;

    cursor.expect('{');
    while (!cursor.consume('}')) {
        const auto key = cursor.parse_string();
        cursor.expect(':');

        if (key == "data_file") {
            fixture.data_file = cursor.parse_string();
        } else if (key == "bars") {
            fixture.bars = static_cast<std::size_t>(cursor.parse_number());
        } else if (key == "open") {
            fixture.open = parse_string_array(cursor);
        } else if (key == "high") {
            fixture.high = parse_string_array(cursor);
        } else if (key == "low") {
            fixture.low = parse_string_array(cursor);
        } else if (key == "close") {
            fixture.close = parse_string_array(cursor);
        } else {
            skip_json_value(cursor);
        }

        static_cast<void>(cursor.consume(','));
    }

    return fixture;
}

inline double parse_golden_double(const std::string& value) {
    if (value == "nan") {
        return std::numeric_limits<double>::quiet_NaN();
    }

    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    if (end == nullptr || *end != '\0') {
        throw std::runtime_error("Invalid floating point fixture value");
    }
    return parsed;
}

inline VortexGoldenReference load_vortex_golden_reference(const std::string& fixture_path) {
    std::ifstream input(fixture_path);
    if (!input.is_open()) {
        throw std::runtime_error("Unable to open golden fixture: " + fixture_path);
    }

    const std::string json((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    JsonCursor cursor(json);
    VortexGoldenReference fixture;

    cursor.expect('{');
    while (!cursor.consume('}')) {
        const auto key = cursor.parse_string();
        cursor.expect(':');

        if (key == "data_file") {
            fixture.data_file = cursor.parse_string();
        } else if (key == "bars") {
            fixture.bars = static_cast<std::size_t>(cursor.parse_number());
        } else if (key == "warmup_bars") {
            fixture.warmup_bars = static_cast<std::size_t>(cursor.parse_number());
        } else if (key == "vi_plus") {
            fixture.vi_plus = parse_string_array(cursor);
        } else if (key == "vi_minus") {
            fixture.vi_minus = parse_string_array(cursor);
        } else if (key == "params") {
            cursor.expect('{');
            while (!cursor.consume('}')) {
                const auto param_key = cursor.parse_string();
                cursor.expect(':');
                if (param_key == "period") {
                    fixture.period = static_cast<std::size_t>(cursor.parse_number());
                } else {
                    skip_json_value(cursor);
                }
                static_cast<void>(cursor.consume(','));
            }
        } else {
            skip_json_value(cursor);
        }

        static_cast<void>(cursor.consume(','));
    }

    return fixture;
}

inline AnalyticsGoldenReference load_analytics_golden_reference(const std::string& fixture_path) {
    std::ifstream input(fixture_path);
    if (!input.is_open()) {
        throw std::runtime_error("Unable to open golden fixture: " + fixture_path);
    }

    const std::string json((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    JsonCursor cursor(json);
    AnalyticsGoldenReference fixture;

    cursor.expect('{');
    while (!cursor.consume('}')) {
        const auto key = cursor.parse_string();
        cursor.expect(':');

        if (key == "scenario") {
            fixture.scenario = cursor.parse_string();
        } else if (key == "initial_cash") {
            fixture.initial_cash = cursor.parse_number();
        } else if (key == "analyzers") {
            cursor.expect('{');
            while (!cursor.consume('}')) {
                const auto analyzer_key = cursor.parse_string();
                cursor.expect(':');

                if (analyzer_key == "trade_analyzer") {
                    cursor.expect('{');
                    while (!cursor.consume('}')) {
                        const auto trade_key = cursor.parse_string();
                        cursor.expect(':');

                        if (trade_key == "total") {
                            cursor.expect('{');
                            while (!cursor.consume('}')) {
                                const auto subkey = cursor.parse_string();
                                cursor.expect(':');
                                if (subkey == "total") {
                                    fixture.trade_analyzer.total.total =
                                        static_cast<std::size_t>(cursor.parse_number());
                                } else if (subkey == "open") {
                                    fixture.trade_analyzer.total.open =
                                        static_cast<std::size_t>(cursor.parse_number());
                                } else if (subkey == "closed") {
                                    fixture.trade_analyzer.total.closed =
                                        static_cast<std::size_t>(cursor.parse_number());
                                } else {
                                    skip_json_value(cursor);
                                }
                                static_cast<void>(cursor.consume(','));
                            }
                        } else if (trade_key == "streak") {
                            cursor.expect('{');
                            while (!cursor.consume('}')) {
                                const auto streak_key = cursor.parse_string();
                                cursor.expect(':');
                                auto* target = streak_key == "won"
                                    ? &fixture.trade_analyzer.streak.won
                                    : &fixture.trade_analyzer.streak.lost;
                                cursor.expect('{');
                                while (!cursor.consume('}')) {
                                    const auto subkey = cursor.parse_string();
                                    cursor.expect(':');
                                    if (subkey == "current") {
                                        target->current =
                                            static_cast<std::size_t>(cursor.parse_number());
                                    } else if (subkey == "longest") {
                                        target->longest =
                                            static_cast<std::size_t>(cursor.parse_number());
                                    } else {
                                        skip_json_value(cursor);
                                    }
                                    static_cast<void>(cursor.consume(','));
                                }
                                static_cast<void>(cursor.consume(','));
                            }
                        } else if (trade_key == "pnl") {
                            cursor.expect('{');
                            while (!cursor.consume('}')) {
                                const auto pnl_key = cursor.parse_string();
                                cursor.expect(':');
                                auto* target = pnl_key == "gross"
                                    ? &fixture.trade_analyzer.pnl.gross
                                    : &fixture.trade_analyzer.pnl.net;
                                cursor.expect('{');
                                while (!cursor.consume('}')) {
                                    const auto subkey = cursor.parse_string();
                                    cursor.expect(':');
                                    if (subkey == "total") {
                                        target->total = cursor.parse_number();
                                    } else if (subkey == "average") {
                                        target->average = cursor.parse_number();
                                    } else if (subkey == "max") {
                                        target->max = cursor.parse_number();
                                    } else {
                                        skip_json_value(cursor);
                                    }
                                    static_cast<void>(cursor.consume(','));
                                }
                                static_cast<void>(cursor.consume(','));
                            }
                        } else if (trade_key == "won" || trade_key == "lost") {
                            auto* target = trade_key == "won"
                                ? &fixture.trade_analyzer.won
                                : &fixture.trade_analyzer.lost;
                            cursor.expect('{');
                            while (!cursor.consume('}')) {
                                const auto subkey = cursor.parse_string();
                                cursor.expect(':');
                                if (subkey == "total") {
                                    target->total = static_cast<std::size_t>(cursor.parse_number());
                                } else if (subkey == "pnl") {
                                    cursor.expect('{');
                                    while (!cursor.consume('}')) {
                                        const auto pnl_subkey = cursor.parse_string();
                                        cursor.expect(':');
                                        if (pnl_subkey == "total") {
                                            target->pnl.total = cursor.parse_number();
                                        } else if (pnl_subkey == "average") {
                                            target->pnl.average = cursor.parse_number();
                                        } else if (pnl_subkey == "max") {
                                            target->pnl.max = cursor.parse_number();
                                        } else {
                                            skip_json_value(cursor);
                                        }
                                        static_cast<void>(cursor.consume(','));
                                    }
                                } else {
                                    skip_json_value(cursor);
                                }
                                static_cast<void>(cursor.consume(','));
                            }
                        } else if (trade_key == "len") {
                            cursor.expect('{');
                            while (!cursor.consume('}')) {
                                const auto subkey = cursor.parse_string();
                                cursor.expect(':');
                                if (subkey == "total") {
                                    fixture.trade_analyzer.len.total =
                                        static_cast<std::size_t>(cursor.parse_number());
                                } else if (subkey == "average") {
                                    fixture.trade_analyzer.len.average = cursor.parse_number();
                                } else if (subkey == "max") {
                                    fixture.trade_analyzer.len.max =
                                        static_cast<std::size_t>(cursor.parse_number());
                                } else if (subkey == "min") {
                                    fixture.trade_analyzer.len.min =
                                        static_cast<std::size_t>(cursor.parse_number());
                                } else {
                                    skip_json_value(cursor);
                                }
                                static_cast<void>(cursor.consume(','));
                            }
                        } else {
                            skip_json_value(cursor);
                        }

                        static_cast<void>(cursor.consume(','));
                    }
                } else if (analyzer_key == "sharpe_ratio") {
                    cursor.expect('{');
                    while (!cursor.consume('}')) {
                        const auto subkey = cursor.parse_string();
                        cursor.expect(':');
                        if (subkey == "sharperatio") {
                            fixture.sharpe_ratio = cursor.parse_number();
                        } else {
                            skip_json_value(cursor);
                        }
                        static_cast<void>(cursor.consume(','));
                    }
                } else if (analyzer_key == "drawdown") {
                    cursor.expect('{');
                    while (!cursor.consume('}')) {
                        const auto subkey = cursor.parse_string();
                        cursor.expect(':');
                        if (subkey == "len") {
                            fixture.drawdown.len = static_cast<std::size_t>(cursor.parse_number());
                        } else if (subkey == "drawdown") {
                            fixture.drawdown.drawdown = cursor.parse_number();
                        } else if (subkey == "moneydown") {
                            fixture.drawdown.moneydown = cursor.parse_number();
                        } else if (subkey == "max") {
                            cursor.expect('{');
                            while (!cursor.consume('}')) {
                                const auto max_key = cursor.parse_string();
                                cursor.expect(':');
                                if (max_key == "len") {
                                    fixture.drawdown.max.len =
                                        static_cast<std::size_t>(cursor.parse_number());
                                } else if (max_key == "drawdown") {
                                    fixture.drawdown.max.drawdown = cursor.parse_number();
                                } else if (max_key == "moneydown") {
                                    fixture.drawdown.max.moneydown = cursor.parse_number();
                                } else {
                                    skip_json_value(cursor);
                                }
                                static_cast<void>(cursor.consume(','));
                            }
                        } else {
                            skip_json_value(cursor);
                        }
                        static_cast<void>(cursor.consume(','));
                    }
                } else if (analyzer_key == "returns") {
                    cursor.expect('{');
                    while (!cursor.consume('}')) {
                        const auto subkey = cursor.parse_string();
                        cursor.expect(':');
                        if (subkey == "rtot") {
                            fixture.returns.rtot = cursor.parse_number();
                        } else if (subkey == "ravg") {
                            fixture.returns.ravg = cursor.parse_number();
                        } else if (subkey == "rnorm") {
                            fixture.returns.rnorm = cursor.parse_number();
                        } else if (subkey == "rnorm100") {
                            fixture.returns.rnorm100 = cursor.parse_number();
                        } else {
                            skip_json_value(cursor);
                        }
                        static_cast<void>(cursor.consume(','));
                    }
                } else {
                    skip_json_value(cursor);
                }

                static_cast<void>(cursor.consume(','));
            }
        } else if (key == "observers") {
            cursor.expect('{');
            while (!cursor.consume('}')) {
                const auto observer_key = cursor.parse_string();
                cursor.expect(':');
                if (observer_key == "buy") {
                    fixture.buy = parse_string_array(cursor);
                } else if (observer_key == "sell") {
                    fixture.sell = parse_string_array(cursor);
                } else if (observer_key == "value") {
                    fixture.value = parse_string_array(cursor);
                } else {
                    skip_json_value(cursor);
                }
                static_cast<void>(cursor.consume(','));
            }
        } else {
            skip_json_value(cursor);
        }

        static_cast<void>(cursor.consume(','));
    }

    return fixture;
}

struct MamaGoldenReference {
    std::string data_file;
    std::size_t bars = 0;
    std::size_t warmup_bars = 0;
    double fast_limit = 0.5;
    double slow_limit = 0.05;
    std::vector<std::string> mama;
    std::vector<std::string> fama;
};

inline MamaGoldenReference load_mama_golden_reference(const std::string& fixture_path) {
    std::ifstream input(fixture_path);
    if (!input.is_open()) {
        throw std::runtime_error("Unable to open golden fixture: " + fixture_path);
    }

    const std::string json((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    JsonCursor cursor(json);
    MamaGoldenReference fixture;

    cursor.expect('{');
    while (!cursor.consume('}')) {
        const auto key = cursor.parse_string();
        cursor.expect(':');

        if (key == "data_file") {
            fixture.data_file = cursor.parse_string();
        } else if (key == "bars") {
            fixture.bars = static_cast<std::size_t>(cursor.parse_number());
        } else if (key == "warmup_bars") {
            fixture.warmup_bars = static_cast<std::size_t>(cursor.parse_number());
        } else if (key == "mama") {
            fixture.mama = parse_string_array(cursor);
        } else if (key == "fama") {
            fixture.fama = parse_string_array(cursor);
        } else if (key == "params") {
            cursor.expect('{');
            while (!cursor.consume('}')) {
                const auto param_key = cursor.parse_string();
                cursor.expect(':');
                if (param_key == "fast_limit") {
                    fixture.fast_limit = cursor.parse_number();
                } else if (param_key == "slow_limit") {
                    fixture.slow_limit = cursor.parse_number();
                } else {
                    skip_json_value(cursor);
                }
                static_cast<void>(cursor.consume(','));
            }
        } else {
            skip_json_value(cursor);
        }

        static_cast<void>(cursor.consume(','));
    }

    return fixture;
}

struct ResampleGoldenReference {
    std::string source;
    int target_timeframe = 0;
    int target_compression = 0;
    struct {
        std::vector<std::string> datetime;
        std::vector<double> open;
        std::vector<double> high;
        std::vector<double> low;
        std::vector<double> close;
        std::vector<double> volume;
    } outputs;
};

inline std::vector<double> parse_number_array(JsonCursor& cursor) {
    std::vector<double> values;
    cursor.expect('[');

    while (!cursor.consume(']')) {
        values.push_back(cursor.parse_number());
        static_cast<void>(cursor.consume(','));
    }

    return values;
}

inline ResampleGoldenReference load_resample_golden_reference(const std::string& fixture_path) {
    std::ifstream input(fixture_path);
    if (!input.is_open()) {
        throw std::runtime_error("Unable to open golden fixture: " + fixture_path);
    }

    const std::string json((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    JsonCursor cursor(json);
    ResampleGoldenReference fixture;

    cursor.expect('{');
    while (!cursor.consume('}')) {
        const auto key = cursor.parse_string();
        cursor.expect(':');

        if (key == "source") {
            fixture.source = cursor.parse_string();
        } else if (key == "target_timeframe") {
            fixture.target_timeframe = static_cast<int>(cursor.parse_number());
        } else if (key == "target_compression") {
            fixture.target_compression = static_cast<int>(cursor.parse_number());
        } else if (key == "outputs") {
            cursor.expect('{');
            while (!cursor.consume('}')) {
                const auto out_key = cursor.parse_string();
                cursor.expect(':');
                if (out_key == "datetime") {
                    fixture.outputs.datetime = parse_string_array(cursor);
                } else if (out_key == "open") {
                    fixture.outputs.open = parse_number_array(cursor);
                } else if (out_key == "high") {
                    fixture.outputs.high = parse_number_array(cursor);
                } else if (out_key == "low") {
                    fixture.outputs.low = parse_number_array(cursor);
                } else if (out_key == "close") {
                    fixture.outputs.close = parse_number_array(cursor);
                } else if (out_key == "volume") {
                    fixture.outputs.volume = parse_number_array(cursor);
                } else {
                    skip_json_value(cursor);
                }
                static_cast<void>(cursor.consume(','));
            }
        } else {
            skip_json_value(cursor);
        }

        static_cast<void>(cursor.consume(','));
    }

    return fixture;
}

} // namespace stratforge::test
