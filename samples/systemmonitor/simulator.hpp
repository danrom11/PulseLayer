#pragma once
// A pretend machine: smooth random walks for the CPU, memory and disk, and a
// process list whose numbers drift. (A real monitor would read the OS here.)
#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <vector>

struct Process {
    int pid = 0;
    std::string name;
    double cpu = 0;    // %
    double memory = 0; // MB
    int threads = 1;
    bool operator==(const Process&) const = default;
};

class Simulator {
public:
    Simulator() {
        const char* names[] = {"browser",  "editor",       "music",     "indexer", "compiler", "mail",
                               "terminal", "sync-service", "updater",   "chat",    "spooler",  "audio-engine",
                               "explorer", "antivirus",    "telemetry", "photos",  "notes",    "dock"};
        int pid = 1200;
        for (const char* n : names) processes.push_back({pid += 1 + static_cast<int>(rnd() % 400), n, 0, 40 + static_cast<double>(rnd() % 900),
                                                         1 + static_cast<int>(rnd() % 40)});
        step();
    }

    // one tick: everything drifts a little (with an occasional spike)
    void step() {
        auto walk = [this](double v, double lo, double hi, double amount) {
            std::normal_distribution<double> d(0, amount);
            return std::clamp(v + d(rnd), lo, hi);
        };
        double total = 0;
        for (auto& p : processes) {
            p.cpu = walk(p.cpu * 0.85 + (p.name == std::string("compiler") ? 6 : 1), 0, 60, 2.5);
            if (rnd() % 60 == 0) p.cpu = std::min(90.0, p.cpu + 35); // a spike
            p.memory = walk(p.memory, 20, 2400, 6);
            total += p.cpu;
        }
        cpu = std::clamp(total / 4.5, 1.0, 100.0);
        memory = walk(memory, 25, 95, 0.8);
        disk = walk(disk * 0.7 + 4, 0, 100, 6);
    }

    double cpu = 20, memory = 55, disk = 8; // %
    std::vector<Process> processes;

private:
    std::mt19937 rnd{42};
};
