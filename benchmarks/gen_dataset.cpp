#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>

/// Generate a synthetic OHLCV CSV dataset for benchmarking.
/// Uses geometric Brownian motion for realistic price walk.
int main(int argc, char* argv[]) {
    const std::size_t num_bars = (argc > 1) ? static_cast<std::size_t>(std::atoi(argv[1])) : 100'000;

    // Output directory
    const std::string output_dir = (argc > 2) ? argv[2] : "build/bench_data";
    const std::string output_file = output_dir + "/synthetic_100k.csv";

    // Create output directory
    std::filesystem::create_directories(output_dir);

    std::ofstream out(output_file);
    if (!out.is_open()) {
        std::cerr << "Error: cannot open " << output_file << " for writing\n";
        return 1;
    }

    // Parameters for geometric Brownian motion
    constexpr double initial_price = 100.0;
    constexpr double drift = 0.0001;         // daily drift (annualized ~2.5%)
    constexpr double volatility = 0.02;      // daily vol (~32% annualized)
    constexpr double volume_base = 1'000'000.0;
    constexpr double volume_vol = 0.3;       // volume variability

    std::mt19937_64 rng(42);  // fixed seed for reproducibility
    std::normal_distribution<double> price_dist(0.0, 1.0);
    std::normal_distribution<double> vol_dist(0.0, 1.0);
    std::uniform_real_distribution<double> bar_shape(0.0, 1.0);

    // Write header
    out << "Date,Open,High,Low,Close,Volume,OpenInterest\n";

    double close_prev = initial_price;

    // Start date: 2000-01-03
    std::tm date = {};
    date.tm_year = 100;  // 2000
    date.tm_mon = 0;     // January
    date.tm_mday = 3;
    date.tm_hour = 0;
    date.tm_min = 0;
    date.tm_sec = 0;

    for (std::size_t i = 0; i < num_bars; ++i) {
        // Advance date (skip weekends)
        if (i > 0) {
            date.tm_mday += 1;
            std::mktime(&date);  // normalize
            // Skip Saturday (6) and Sunday (0)
            while (date.tm_wday == 0 || date.tm_wday == 6) {
                date.tm_mday += 1;
                std::mktime(&date);
            }
        }

        // GBM step for close price
        const double z = price_dist(rng);
        const double ret = drift + volatility * z;
        double close_price = close_prev * std::exp(ret);

        // Generate OHLC from close
        const double intraday_range = close_prev * volatility * (0.5 + bar_shape(rng));
        const double open_offset = (bar_shape(rng) - 0.5) * intraday_range * 0.5;
        double open_price = close_prev + open_offset;

        double high_price = std::max(open_price, close_price) + std::abs(price_dist(rng)) * intraday_range * 0.3;
        double low_price = std::min(open_price, close_price) - std::abs(price_dist(rng)) * intraday_range * 0.3;

        // Ensure validity: low <= open,close <= high
        high_price = std::max({high_price, open_price, close_price});
        low_price = std::min({low_price, open_price, close_price});

        // Clamp to positive
        open_price = std::max(open_price, 0.01);
        high_price = std::max(high_price, 0.01);
        low_price = std::max(low_price, 0.01);
        close_price = std::max(close_price, 0.01);

        // Volume with log-normal distribution
        const double vol_z = vol_dist(rng);
        const double volume = volume_base * std::exp(volume_vol * vol_z);

        // Format date
        char date_buf[16];
        std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &date);

        out << date_buf << ','
            << std::fixed << std::setprecision(2)
            << open_price << ','
            << high_price << ','
            << low_price << ','
            << close_price << ','
            << static_cast<std::int64_t>(volume) << ','
            << 0 << '\n';

        close_prev = close_price;
    }

    out.close();

    std::cout << "Generated " << num_bars << " bars -> " << output_file << '\n';
    std::cout << "Final price: " << std::fixed << std::setprecision(2) << close_prev << '\n';

    return 0;
}
