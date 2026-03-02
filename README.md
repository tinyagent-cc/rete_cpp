# RETE Expert System in Modern C++

This project provides a header-only RETE-based expert system engine implemented
in modern C++17, with:

- Alpha network (constant tests, alpha memories)
- Beta network (join nodes, negative nodes, production nodes)
- Agenda with conflict resolution strategies
- Refraction support
- Fluent rule-building API
- Catch2 tests, runnable examples, and benchmarks

The implementation uses only the C++ standard library by default. Boost is
optional and only used for DOT export when available.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Build options:

- `RETE_BUILD_TESTS` (default `ON`)
- `RETE_BUILD_EXAMPLES` (default `ON`)
- `RETE_BUILD_BENCHMARKS` (default `ON`)

## Run Tests

```bash
cd build
ctest --output-on-failure
```

## Run Examples

```bash
./build/examples/animal_classification
./build/examples/blocks_world
./build/examples/medical_diagnosis
```

## Run Benchmarks

```bash
./build/benchmarks/bench_rete
```

## Quick Start

```cpp
#include <rete.hpp>
#include <string>

int main() {
    rete::ReteEngine engine;

    engine.add_rule("bird-rule")
        .salience(10)
        .when(std::string("?x"), std::string("has"), std::string("feathers"))
        .when(std::string("?x"), std::string("can"), std::string("fly"))
        .then([](rete::ReteEngine& e, const rete::Bindings& b) {
            e.assert_fact(b.at("?x"), std::string("is"), std::string("bird"));
        })
        .build();

    engine.assert_fact(std::string("tweety"), std::string("has"), std::string("feathers"));
    engine.assert_fact(std::string("tweety"), std::string("can"), std::string("fly"));
    engine.run();
}
```

## Project Layout

- `include/rete/` core engine headers
- `include/rete.hpp` convenience include
- `tests/` Catch2 unit/integration tests
- `examples/` runnable expert-system scenarios
- `benchmarks/` performance/scaling benchmarks

## Notes

- WME representation follows the classic triple model:
  `(identifier, attribute, value)`
- Conditions can use constants (e.g. `"on"`) and variables (e.g. `"?x"`).
- Negation is supported through `when_not(...)`.
- Conflict strategies available:
  - `Priority`
  - `Recency`
  - `Specificity`
  - `FIFO`
