#pragma once
// A fake data source: a random walk with occasional CPU spikes.
// In a real app this would read the system, a socket or a device - the UI
// only sees `samples`, a hot Pulse stream, and does not care where it comes from.
#include <pulse/pulse.hpp>

#include <algorithm>
#include <cmath>
#include <random>

struct Sample {
    double cpu = 0;      // percent
    double memory = 0;   // GB
    double network = 0;  // MB/s
};

class Sensors {
public:
    static constexpr double kMemoryTotal = 16.0;

    pulse::subject<Sample> samples;

    void tick() {
        std::normal_distribution<double> noise(0.0, 1.0);
        std::uniform_real_distribution<double> chance(0.0, 1.0);

        if (burst_ == 0 && chance(rng_) < 0.03) burst_ = 10 + static_cast<int>(chance(rng_) * 15);
        if (burst_ > 0) {
            cpu_ = std::min(99.0, cpu_ + 6 + noise(rng_) * 3);
            --burst_;
        } else {
            cpu_ += noise(rng_) * 5 + (30 - cpu_) * 0.08; // drifts back to ~30 %
        }
        cpu_ = std::clamp(cpu_, 2.0, 100.0);
        memory_ = std::clamp(memory_ + noise(rng_) * 0.04 + cpu_ * 0.0004 - 0.012, 4.0, kMemoryTotal - 0.5);
        network_ = std::max(0.0, network_ * 0.85 + std::abs(noise(rng_)) * (chance(rng_) < 0.1 ? 12.0 : 1.5));

        samples.on_next({cpu_, memory_, network_});
    }

private:
    std::mt19937 rng_{std::random_device{}()};
    double cpu_ = 30, memory_ = 7.5, network_ = 1;
    int burst_ = 0;
};
