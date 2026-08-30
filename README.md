# RETE Expert System in Modern C++

Part of [tinyagent](https://github.com/tinyagent-cc): rete_cpp is the
stack's reflex layer. [tiny_agent](https://github.com/tinyagent-cc/tiny_agent)'s
`middleware/reflex.hpp` uses it to answer easy cases in microseconds without
a model call and to veto bad tool calls deterministically. rete_cpp itself
has no dependency on tiny_agent and works standalone.

This project provides a header-only RETE-based expert system engine implemented
in modern C++17, with:

- Alpha network (constant tests, alpha memories)
- Beta network (join nodes, negative nodes, production nodes)
- Agenda with conflict resolution strategies
- Refraction support
- Fluent rule-building API
- Rules loaded from a JSON or YAML file at runtime, with no code in the file
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
- `RETE_ENABLE_JSON` (default `ON`) JSON rule loading
- `RETE_ENABLE_YAML` (default `OFF`) YAML rule loading, needs yaml-cpp
- `RETE_EMBEDDED` (default `OFF`) constrained-MCU profile: JSON only, no
  iostream, no exceptions

The four profiles and their exact cmake lines are in [docs/rules.md](docs/rules.md#build-options).

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
./build/examples/robotics_behaviour                          # rules.json
./build/examples/robotics_behaviour examples/robotics/rules.yaml
```

`robotics_behaviour` needs `RETE_ENABLE_JSON` and is skipped on a core-only or
embedded build.

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

## Rules from a config file

Rules can also live in a JSON or YAML file and be loaded at runtime. The
application declares the facts and the actions in C++; the file decides which
facts trigger which action, and with what priority. Changing behaviour then
takes an editor rather than a toolchain, which matters on anything you have to
cross-compile and flash.

```yaml
rules:
  - name: low_battery_return
    salience: 300
    when:
      - fact: battery
        attribute: level
        op: less_than
        value: 20
        bind: "?level"
      - fact: robot
        attribute: moving
        value: true
    then:
      - action: return_to_base
        params:
          speed: 0.4
```

```cpp
#include <rete/rules/loader.hpp>

rete::ReteEngine engine;
rete::rules::ActionRegistry registry;

registry.register_action("return_to_base",
    [](rete::ReteEngine&, const rete::Bindings& b, const rete::rules::ActionParams& p) {
        drive_to_dock(p.get_double("speed", 0.25));
    });

auto result = rete::rules::load_rules_from_file(engine, registry, "rules.json");
if (!result.ok()) {
    std::cerr << result.summary() << "\n";
    return 1;
}
```

A rule file names actions and never carries code. There is no expression
evaluator, no script host and no shell, and an action id that nobody registered
fails the load rather than becoming a silent no-op. Loading is all-or-nothing
per rule, so one broken rule does not take the rest of the behaviour set with
it.

`examples/robotics` is the worked version: seven rules, both file formats, a
scripted sensor scenario, and salience deciding which action wins each tick.

Comparisons such as `less_than` are guards rather than network tests, because
the alpha network discriminates on equality. The full explanation, the operator
table, the error codes and the embedded guidance are in
[docs/rules.md](docs/rules.md).

The programmatic API above is unchanged. None of this is required to use the
engine, and `RETE_ENABLE_JSON=OFF` leaves the engine exactly as it was.

## Project Layout

- `include/rete/` core engine headers
- `include/rete/rules/` rule definitions, validation, loading, the action registry
- `include/rete/serialization/` the JSON and YAML readers, and nothing else
- `include/rete.hpp` convenience include
- `docs/rules.md` the config-driven rules reference
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
