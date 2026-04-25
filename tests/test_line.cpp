#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/core/line.hpp>
#include <stratforge/core/line_series.hpp>

using namespace stratforge;

TEST_CASE("Line basic operations", "[core][line]") {
    SECTION("Default construction") {
        Line<double> line;
        REQUIRE(line.empty());
        REQUIRE(line.size() == 0);
        REQUIRE(line.index() == 0);
    }

    SECTION("Forward adds data and advances cursor") {
        Line<double> line;
        line.forward(10.0);
        REQUIRE(line.size() == 1);
        REQUIRE(line[0] == 10.0);

        line.forward(20.0);
        REQUIRE(line.size() == 2);
        REQUIRE(line[0] == 20.0);
        REQUIRE(line[-1] == 10.0);
    }

    SECTION("Set overwrites current value") {
        Line<double> line;
        line.forward(10.0);
        line.set(15.0);
        REQUIRE(line[0] == 15.0);
    }

    SECTION("Backtrader-style indexing") {
        Line<double> line;
        line.forward(1.0);
        line.forward(2.0);
        line.forward(3.0);
        line.forward(4.0);
        line.forward(5.0);

        // Cursor is at index 4 (last element)
        REQUIRE(line[0] == 5.0);
        REQUIRE(line[-1] == 4.0);
        REQUIRE(line[-2] == 3.0);
        REQUIRE(line[-3] == 2.0);
        REQUIRE(line[-4] == 1.0);
    }

    SECTION("Out of range throws") {
        Line<double> line;
        line.forward(1.0);
        REQUIRE_THROWS_AS(line[-1], std::out_of_range);
        REQUIRE_THROWS_AS(line[1], std::out_of_range);
    }

    SECTION("Home resets cursor") {
        Line<double> line;
        line.forward(1.0);
        line.forward(2.0);
        line.forward(3.0);

        line.home();
        REQUIRE(line.index() == 0);
        REQUIRE(line[0] == 1.0);
    }

    SECTION("Advance moves cursor forward") {
        Line<double> line;
        line.forward(1.0);
        line.forward(2.0);
        line.forward(3.0);

        line.home();
        REQUIRE(line[0] == 1.0);

        line.advance();
        REQUIRE(line[0] == 2.0);
        REQUIRE(line[-1] == 1.0);

        line.advance();
        REQUIRE(line[0] == 3.0);
    }

    SECTION("Extend pre-allocates with default values") {
        Line<double> line;
        line.extend(5, 0.0);
        REQUIRE(line.size() == 5);
    }

    SECTION("Buflen reports remaining bars") {
        Line<double> line;
        line.forward(1.0);
        line.forward(2.0);
        line.forward(3.0);

        line.home();
        REQUIRE(line.buflen() == 3);

        line.advance();
        REQUIRE(line.buflen() == 2);
    }

    SECTION("Reserve construction") {
        Line<double> line(100);
        REQUIRE(line.empty());
        REQUIRE(line.size() == 0);
    }

    SECTION("Data access") {
        Line<double> line;
        line.forward(10.0);
        line.forward(20.0);

        const auto& data = line.data();
        REQUIRE(data.size() == 2);
        REQUIRE(data[0] == 10.0);
        REQUIRE(data[1] == 20.0);
    }
}

TEST_CASE("Line with integer type", "[core][line]") {
    Line<int> line;
    line.forward(1);
    line.forward(2);
    line.forward(3);

    REQUIRE(line[0] == 3);
    REQUIRE(line[-1] == 2);
    REQUIRE(line[-2] == 1);
}

TEST_CASE("LineSeries operations", "[core][line_series]") {
    SECTION("Add and access lines") {
        LineSeries<double> series;
        series.add_line("open");
        series.add_line("close");

        REQUIRE(series.num_lines() == 2);
        REQUIRE(series.has_line("open"));
        REQUIRE(series.has_line("close"));
        REQUIRE_FALSE(series.has_line("volume"));
    }

    SECTION("Line data operations through series") {
        LineSeries<double> series;
        series.add_line("close");

        series.line("close").forward(100.0);
        series.line("close").forward(101.0);

        REQUIRE(series.line("close")[0] == 101.0);
        REQUIRE(series.line("close")[-1] == 100.0);
    }

    SECTION("Advance all lines") {
        LineSeries<double> series;
        series.add_line("open");
        series.add_line("close");

        series.line("open").forward(10.0);
        series.line("open").forward(11.0);
        series.line("close").forward(100.0);
        series.line("close").forward(101.0);

        series.home();
        REQUIRE(series.line("open")[0] == 10.0);
        REQUIRE(series.line("close")[0] == 100.0);

        series.advance();
        REQUIRE(series.line("open")[0] == 11.0);
        REQUIRE(series.line("close")[0] == 101.0);
    }

    SECTION("Names accessor") {
        LineSeries<double> series;
        series.add_line("a");
        series.add_line("b");

        auto names = series.names();
        REQUIRE(names.size() == 2);
    }

    SECTION("Missing line throws") {
        LineSeries<double> series;
        REQUIRE_THROWS(series.line("nonexistent"));
    }
}
