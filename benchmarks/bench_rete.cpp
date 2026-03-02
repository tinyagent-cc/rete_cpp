// RETE Engine Benchmarks
//
// Measures throughput, scaling behaviour, and memory characteristics.
// Uses std::chrono for timing (no external benchmark framework needed).

#include <rete/rete.hpp>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace rete;
using Clock = std::chrono::high_resolution_clock;

static double elapsed_ms(Clock::time_point start) {
    auto end = Clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// --------------------------------------------------------------------------
// 1. WME assertion throughput
// --------------------------------------------------------------------------
static void bench_assertion_throughput(int n_wmes) {
    ReteEngine engine;

    engine.add_rule("match-type")
        .when(std::string("?x"), std::string("type"), std::string("item"))
        .then([](ReteEngine&, const Bindings&) {})
        .build();

    auto start = Clock::now();
    for (int i = 0; i < n_wmes; ++i) {
        engine.assert_fact(
            std::string("obj" + std::to_string(i)),
            std::string("type"),
            std::string("item"));
    }
    double ms = elapsed_ms(start);

    std::cout << "  assert " << n_wmes << " WMEs: "
              << std::fixed << std::setprecision(2) << ms << " ms ("
              << static_cast<int>(n_wmes / (ms / 1000.0)) << " WME/s)\n";
}

// --------------------------------------------------------------------------
// 2. Rule scaling: match time vs number of rules
// --------------------------------------------------------------------------
static void bench_rule_scaling(int n_rules, int n_wmes) {
    ReteEngine engine;

    for (int r = 0; r < n_rules; ++r) {
        engine.add_rule("rule-" + std::to_string(r))
            .when(std::string("?x"), std::string("attr" + std::to_string(r)), std::string("?v"))
            .then([](ReteEngine&, const Bindings&) {})
            .build();
    }

    auto start = Clock::now();
    for (int i = 0; i < n_wmes; ++i) {
        engine.assert_fact(
            std::string("obj" + std::to_string(i)),
            std::string("attr0"),
            std::string("val" + std::to_string(i)));
    }
    engine.run();
    double ms = elapsed_ms(start);

    std::cout << "  " << n_rules << " rules, " << n_wmes << " WMEs: "
              << std::fixed << std::setprecision(2) << ms << " ms\n";
}

// --------------------------------------------------------------------------
// 3. Multi-condition join throughput
// --------------------------------------------------------------------------
static void bench_join_throughput(int n) {
    ReteEngine engine;

    engine.add_rule("two-condition-join")
        .when(std::string("?x"), std::string("on"), std::string("?y"))
        .when(std::string("?y"), std::string("color"), std::string("?c"))
        .then([](ReteEngine&, const Bindings&) {})
        .build();

    auto start = Clock::now();
    for (int i = 0; i < n; ++i) {
        auto a = std::string("B" + std::to_string(i));
        auto b = std::string("B" + std::to_string(i + 1));
        engine.assert_fact(a, std::string("on"), b);
        engine.assert_fact(b, std::string("color"), std::string("red"));
    }
    engine.run();
    double ms = elapsed_ms(start);

    std::cout << "  " << n << " join pairs: "
              << std::fixed << std::setprecision(2) << ms << " ms, "
              << engine.activation_count() << " pending activations\n";
}

// --------------------------------------------------------------------------
// 4. Naive matcher comparison
// --------------------------------------------------------------------------
struct NaiveRule {
    std::string attr_match;
    std::string val_match;
};

static void bench_naive_vs_rete(int n_rules, int n_wmes) {
    // --- Naive approach ---
    std::vector<NaiveRule> rules;
    for (int r = 0; r < n_rules; ++r)
        rules.push_back({"type", "item"});

    struct SimpleWME {
        std::string id, attr, val;
    };
    std::vector<SimpleWME> wmes;
    for (int i = 0; i < n_wmes; ++i)
        wmes.push_back({"obj" + std::to_string(i), "type", "item"});

    auto start_naive = Clock::now();
    int naive_matches = 0;
    for (auto& w : wmes)
        for (auto& r : rules)
            if (w.attr == r.attr_match && w.val == r.val_match)
                ++naive_matches;
    double naive_ms = elapsed_ms(start_naive);

    // --- RETE approach ---
    ReteEngine engine;
    for (int r = 0; r < n_rules; ++r) {
        engine.add_rule("r" + std::to_string(r))
            .when(std::string("?x"), std::string("type"), std::string("item"))
            .then([](ReteEngine&, const Bindings&) {})
            .build();
    }

    auto start_rete = Clock::now();
    for (int i = 0; i < n_wmes; ++i)
        engine.assert_fact(std::string("obj" + std::to_string(i)),
                           std::string("type"), std::string("item"));
    double rete_ms = elapsed_ms(start_rete);

    std::cout << "  Naive (" << n_rules << " rules x " << n_wmes << " WMEs): "
              << std::fixed << std::setprecision(2) << naive_ms << " ms ("
              << naive_matches << " matches)\n";
    std::cout << "  RETE  (" << n_rules << " rules x " << n_wmes << " WMEs): "
              << std::fixed << std::setprecision(2) << rete_ms << " ms ("
              << engine.activation_count() << " activations)\n";
    if (rete_ms > 0)
        std::cout << "  Speedup factor: "
                  << std::setprecision(1) << (naive_ms / rete_ms) << "x\n";
}

// --------------------------------------------------------------------------
// 5. Memory measurement
// --------------------------------------------------------------------------
static void bench_memory(int n_wmes) {
    ReteEngine engine;

    engine.add_rule("mem-rule")
        .when(std::string("?x"), std::string("on"), std::string("?y"))
        .when(std::string("?y"), std::string("on"), std::string("?z"))
        .then([](ReteEngine&, const Bindings&) {})
        .build();

    for (int i = 0; i < n_wmes; ++i) {
        engine.assert_fact(
            std::string("B" + std::to_string(i)),
            std::string("on"),
            std::string("B" + std::to_string(i + 1)));
    }

    std::cout << "  " << n_wmes << " chained WMEs: "
              << engine.wme_count() << " WMEs, "
              << engine.activation_count() << " activations, "
              << engine.rule_count() << " rules\n";
    std::cout << "  (Approximate WME memory: ~"
              << n_wmes * sizeof(WME) << " bytes for WME structs)\n";
}

int main() {
    std::cout << "=== RETE Engine Benchmarks ===\n\n";

    std::cout << "[1] WME Assertion Throughput\n";
    bench_assertion_throughput(100);
    bench_assertion_throughput(1000);
    bench_assertion_throughput(10000);

    std::cout << "\n[2] Rule Scaling (WME matching time vs rule count)\n";
    bench_rule_scaling(10,  1000);
    bench_rule_scaling(50,  1000);
    bench_rule_scaling(100, 1000);
    bench_rule_scaling(500, 1000);

    std::cout << "\n[3] Two-Condition Join Throughput\n";
    bench_join_throughput(100);
    bench_join_throughput(1000);
    bench_join_throughput(5000);

    std::cout << "\n[4] Naive Matcher vs RETE\n";
    bench_naive_vs_rete(100, 1000);
    bench_naive_vs_rete(100, 10000);

    std::cout << "\n[5] Memory Characteristics\n";
    bench_memory(100);
    bench_memory(1000);
    bench_memory(10000);

    std::cout << "\nDone.\n";
    return 0;
}
