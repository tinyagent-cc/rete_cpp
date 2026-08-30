# Configuration-driven rules

Rules can be written in a JSON or YAML file and loaded at runtime instead of
being compiled into the binary. The fluent `add_rule()` API is unchanged and
nothing about the engine moved; this is a layer on top of it.

- [Why](#why)
- [Architecture](#architecture)
- [The rule schema](#the-rule-schema)
- [Operators](#operators)
- [The Action Registry](#the-action-registry)
- [Loading](#loading)
- [Validation and errors](#validation-and-errors)
- [Build options](#build-options)
- [Embedded targets](#embedded-targets)
- [Platform guidance](#platform-guidance)
- [Known limitations](#known-limitations)
- [Backwards compatibility](#backwards-compatibility)

## Why

Behaviour written in C++ can only be changed by a person with a toolchain. On a
robot that means a cross-compile, a flash cycle and a reboot for every threshold
somebody wants to try, and it means the person tuning the behaviour has to be
the person who can build the firmware. Those are usually not the same person.

A rule file separates the two. The application declares what facts exist and
what actions it can perform, both in C++, both reviewed and tested. The rules
that connect them live in a file you can edit, diff and ship without a compiler.
`examples/robotics` is the demonstration: change the low-battery threshold from
20 to 25 in `rules.json`, run the same binary again, and the robot breaks off to
return to base a tick earlier.

The tradeoff is real and worth naming. A rule file is input, so it can be wrong
in ways the compiler used to catch. That is what the validator is for, and why
an unknown action id fails the load instead of becoming a no-op.

## Architecture

Three layers, and the dependency arrows only point one way.

```
include/rete/                 the engine. No knowledge of files or formats.
  types, wme, token, condition, production, alpha_network,
  beta_network, agenda, rete

include/rete/rules/           format-independent rule handling.
  definition   RuleDefinition, Operator, ConditionDefinition
  error        ErrorCode, RuleError, LoadResult
  action_registry  id -> handler
  validator    ConfigNode -> RuleDefinition, then check it
  loader       RuleDefinition -> Production, installed in the engine

include/rete/serialization/   the only files that know a text format.
  config_node  the one document shape both parsers produce
  parse_error  message, line, column
  json_parser  hand-written, #if RETE_ENABLE_JSON
  yaml_parser  yaml-cpp adapter, #if RETE_ENABLE_YAML
```

### Why the core has no JSON or YAML library

rete_cpp has no required dependencies, and that is a load-bearing property, not
a badge. The targets it is meant to run on include an RP2040 with 264 KB of RAM
and no filesystem. Pulling in a JSON library so the engine can read a file it
will never see would cost every one of those targets flash and build time for
nothing.

So the split is enforced by the include graph. The engine headers do not include
`config.hpp`, `serialization/` or `rules/`. Everything under those two
directories is opt-in, and turning `RETE_ENABLE_JSON` off leaves the engine
exactly as it was before this feature existed.

The JSON reader is hand-written for the same reason. It is a few hundred lines
sized to a schema with a dozen keys in it, and all of the JSON knowledge sits in
one file behind `parse_json()`. Swapping in ArduinoJson on a target that already
links it is a one-file change.

`ConfigNode` is the seam. Both parsers produce it and nothing downstream can
tell which one ran, which is what makes "the same rule in either format compiles
to the same production" structural rather than a promise. Run the robotics
example against `rules.json` and then against `rules.yaml`: the trace is
identical from the loaded-rules line down, and the only thing that differs above
it is the file name.

## The rule schema

The same document in both formats. YAML carries the annotations because JSON has
no comments.

```yaml
rules:                             # a map with `rules:`, or a bare list of
                                   # rules, or a single bare rule map
  - name: low_battery_return       # required, non-empty, unique in the document
                                   # and against the rules the engine holds
    salience: 300                  # optional integer, default 0. Higher fires
                                   # first. Must fit in an int.
    enabled: true                  # optional, default true. `false` parses and
                                   # validates the rule but does not install it.
    when:                          # required, at least one entry
      - fact: battery              # required. The WME identifier.
        attribute: level           # required. The WME attribute.
        op: less_than              # optional, default equals. `operator` is
                                   # accepted as a spelling of `op`.
        value: 20                  # required except for exists and not_exists
        bind: "?level"             # optional. Names the variable the matched
                                   # value binds to, visible to the handler.
      - fact: robot
        attribute: moving
        value: true                # booleans, integers, doubles and strings
    then:                          # required, at least one entry
      - action: return_to_base     # required. Must be in the ActionRegistry.
        params:                    # optional map of names to single values
          speed: 0.4
          announce: true
```

```json
{
  "rules": [
    {
      "name": "low_battery_return",
      "salience": 300,
      "enabled": true,
      "when": [
        { "fact": "battery", "attribute": "level", "op": "less_than", "value": 20, "bind": "?level" },
        { "fact": "robot",   "attribute": "moving", "value": true }
      ],
      "then": [
        { "action": "return_to_base", "params": { "speed": 0.4, "announce": true } }
      ]
    }
  ]
}
```

A few things the schema does on purpose.

`value: "?x"` is a variable binding, the same as `op: bind` with `bind: "?x"`.
A condition may test a constant or bind a variable, never both, because the
engine can only do the first and would silently drop the second.

Nothing is coerced. `salience: "high"` is an error rather than a quiet 0, and a
quoted `value: "20"` stays the string `"20"`. In YAML that also means the 1.2
core schema applies rather than yaml-cpp's YAML 1.1 converters, so `announce: no`
is the string `no` and not `false`.

The `?$` prefix is reserved. The loader generates variables for guards and
wildcards under it, and a document that uses the prefix itself is rejected, so
a collision cannot happen.

## Operators

| `op` | aliases | kind | value |
|---|---|---|---|
| `equals` | `eq`, `==` | network, constant test | required |
| `bind` | | network, variable binding | `bind:` or a `"?var"` value |
| `exists` | | network, binds a wildcard | optional |
| `not_exists` | | network, negated node | optional, cannot `bind` |
| `not_equals` | `ne`, `!=` | guard | required |
| `greater_than` | `gt`, `>` | guard | required, orderable |
| `greater_or_equal` | `gte`, `>=` | guard | required, orderable |
| `less_than` | `lt`, `<` | guard | required, orderable |
| `less_or_equal` | `lte`, `<=` | guard | required, orderable |

Network operators compile into the RETE discrimination network. They cost
nothing at match time because the network is what executes them.

### Why guards exist

The alpha network discriminates on equality. A WME is routed by hashing its
constant fields, which is what makes RETE fast and what makes it unable to
answer `battery.level < 20` without turning the trie into an interval tree.

So a comparison compiles into two pieces. The condition becomes a plain variable
binding, which the network handles natively, and the comparison becomes a
`Guard` on the production: a variable name, an operator and a literal operand.
The guard is checked once, on the bindings, when the activation is about to
fire. Cost is one comparison per activation of that rule, not per fact, and a
rule with no guards pays nothing because the vector is empty and the loop is
skipped.

A guard that cannot answer fails closed. Comparing a string against a number
yields no ordering, so the rule does not fire and does not pretend to. The
validator catches the common version of that at load time: `greater_than` with a
boolean or a non-numeric string operand is rejected with a message naming the
field, because a rule that can never fire is worse than a rule that will not
load.

### Guards and refraction

Guards are checked **before** refraction is recorded. In `ReteEngine::run()` the
order is: pop the activation, evaluate the guards, and only then call
`mark_fired`. A match whose comparison fails is not marked, so it stays eligible
and can fire later when the value moves into range.

If the order were reversed, a rule that failed its comparison once would be
permanently spent on that match. `low_battery_return` would be checked at 80%,
fail, and never fire again for the rest of the run.

### Why `not_equals` is a guard

It is the operator most people expect to be a negated condition, and that would
be wrong.

A negated condition matches when no WME satisfies it. Write
`obstacle.front != true` as a negation and it matches both when the sensor
reports `false` and when the sensor reports nothing at all. A robot with a dead
front sensor would then fire every rule written for a clear path.

As a guard, `not_equals` means what people read it to mean: the fact is present
and its value differs. Genuine absence has its own operator, `not_exists`, and
the two are different questions with different answers when hardware fails.

`exists` binds a variable because a binding is this engine's wildcard: it
matches any value and costs one entry in the bindings the handler receives.
`not_exists` cannot bind, because a negated match contributes an empty slot to
the token and there is no value to hand over.

## The Action Registry

A rule file names an action. It never carries one. There is no expression
evaluator in this layer, no script host, no `dlopen` and no shell, and adding
one would turn a config file into an execution vector.

```cpp
rete::rules::ActionRegistry registry;

registry.register_action("return_to_base",
    [](rete::ReteEngine& engine,
       const rete::Bindings& bindings,
       const rete::rules::ActionParams& params) {
        const double speed = params.get_double("speed", 0.25);
        const bool announce = params.get_bool("announce", false);
        // bindings holds the variables the rule named, e.g. "?level"
        drive_to_dock(speed, announce);
    });
```

`register_action` returns false if the id is already taken, so a second
registration cannot silently shadow the first. Pass `overwrite = true` when
replacement is what you mean.

`ActionParams` getters take a fallback rather than throwing, because embedded
builds have no exceptions and a missing optional parameter is not an error:
`get_string`, `get_int`, `get_double`, `get_bool`, plus `find`, `has` and
`all()` for the whole ordered map. Parameters keep the order the document wrote
them in.

An action id that is not in the registry is a validation failure. The rule is
not loaded and `LoadResult::errors` says which rule and which field. Dropping it
silently would leave an operator convinced a behaviour was live when it was not.

Handlers are resolved once, at load time, and copied into the production with
their parameters. A per-firing lookup would cost a hash on every activation, and
it would let a later `unregister_action()` empty a rule out from under the
running engine.

## Loading

```cpp
#include <rete/rules/loader.hpp>

rete::ReteEngine engine;
rete::rules::ActionRegistry registry;
register_my_actions(registry);           // before the load, always

rete::rules::RuleLoader loader(engine, registry);
rete::rules::LoadResult result = loader.load_from_file("rules.json");
if (!result.ok()) {
    log(result.summary());
    return 1;
}
```

Entry points:

| Call | Notes |
|---|---|
| `load_from_string(text, format, opts)` | `Format::Auto` sniffs the first non-space character: `{` or `[` means JSON, anything else means YAML |
| `load_from_file(path, format, opts)` | Auto resolves by extension first (`.json`, `.yaml`, `.yml`), then by content. Needs `<fstream>`, so it is absent when `RETE_NO_IOSTREAM` is set |
| `load_from_files(paths, opts)` | In order, sharing one name space. The second file's copy of a rule the first file loaded is a duplicate, not a shadow |
| `load_node(root, opts)` | For a `ConfigNode` you built or parsed yourself |
| `load_definitions(defs, opts)` | Skips parsing entirely. `RuleDefinition` structs in, productions out |
| `disabled_rules()` | Rules the last load read and validated but left out because `enabled: false`. Not errors, not loaded, and `LoadResult` has nowhere to put that third case. With `replace_existing`, a rule turned off this way is also removed from the engine |

The free functions `load_rules`, `load_rules_from_string` and
`load_rules_from_file` wrap a one-shot `RuleLoader` for the common case where
nobody needs `disabled_rules()`.

`LoadOptions` has two fields:

- `replace_existing` (default false). A name the engine already runs is a
  replacement rather than a clash. Without it, reloading the same file twice
  reports every rule as a duplicate.
- `stop_on_first_error` (default false). Give up at the first problem instead of
  reporting everything wrong with the document. Nothing is installed when this
  stops a load.

Loading is all-or-nothing **per rule**. A rule with any error is not installed
and its healthy neighbours still are, so one bad rule in a robot's config does
not disarm the whole behaviour set. `LoadResult` carries both numbers, so the
caller can tell a partial load from a clean one and decide what a partial load
means for that machine.

The order inside a load is fixed and it matters: read the whole document, then
validate every rule, then install the ones that passed. Validating the whole
document first is what lets a duplicate name be caught against the entire file
rather than only against the rules ahead of it, and it keeps the engine from
being half-loaded when the last rule turns out to be broken.

Nothing retains the parsed document. Handlers and parameters are copied into the
production by value, and the `ConfigNode` tree is released as soon as the rules
compile.

## Validation and errors

Every failure in this layer is a return value. Nothing throws, because embedded
builds have no exceptions.

```cpp
struct RuleError {
    ErrorCode   code;
    std::string rule;      // rule name when known
    std::string field;     // dotted path, e.g. "when[1].op"
    std::string message;
    std::size_t line;      // 0 when no line is known
};

struct LoadResult {
    std::size_t              loaded;
    std::vector<std::string> rule_names;   // size() == loaded
    std::vector<RuleError>   errors;
    bool ok() const;                       // errors.empty()
    std::string summary() const;
};
```

`ErrorCode`:

| Code | Means |
|---|---|
| `None` | No error |
| `ParseFailure` | The document is not valid JSON or YAML |
| `NotAMap` | A node has the wrong shape: a rule that is not a map, a `when` that is not a list |
| `MissingField` | A required field is absent |
| `EmptyRuleName` | `name` is missing or empty |
| `DuplicateRuleName` | Twice in one document, or already in the engine without `replace_existing` |
| `UnknownOperator` | `op` is not one of the operators or their aliases |
| `InvalidCondition` | The condition contradicts itself: bind and a constant value, a reserved `?$` variable, a guard compared against another variable |
| `InvalidValueType` | A field has the wrong type: a non-string `fact`, a list where a scalar belongs, an unorderable guard operand |
| `InvalidSalience` | Not a whole number, or outside the range of an `int` |
| `UnknownAction` | No handler with that id is registered |
| `EmptyConditions` | No `when` clauses. A rule matching nothing would fire every cycle |
| `UnsupportedFeature` | Valid schema, not available in this build: YAML on a JSON-only binary, `bind` on `not_exists` |
| `FileNotFound` | The path could not be opened |

A worked example. This document has one good rule and three broken ones:

```json
{
  "rules": [
    { "name": "emergency_stop", "salience": 1000,
      "when": [ { "fact": "obstacle", "attribute": "front", "value": true } ],
      "then": [ { "action": "stop" } ] },
    { "name": "hover", "salience": 200,
      "when": [ { "fact": "robot", "attribute": "moving", "value": true } ],
      "then": [ { "action": "engage_thrusters" } ] },
    { "name": "emergency_stop", "salience": 900,
      "when": [ { "fact": "robot", "attribute": "moving", "value": true } ],
      "then": [ { "action": "stop" } ] },
    { "name": "no_conditions",
      "then": [ { "action": "stop" } ] }
  ]
}
```

`result.summary()` on a registry holding `stop` but not `engage_thrusters`:

```
loaded 1 rule(s), 3 error(s):
  - action not in registry in rule 'hover' at then[0].action: no action called 'engage_thrusters' is registered; a rule file names actions, it never carries them, so register it before loading
  - duplicate rule name in rule 'emergency_stop' at name: this document already defines a rule called 'emergency_stop'
  - rule has no conditions in rule 'no_conditions' at when: a rule needs at least one when clause; a rule that matches nothing would fire on every cycle
```

`emergency_stop` is loaded and running. The other three are not. `result.ok()`
is false and `result.loaded` is 1, and what the application does with that is
its own decision: a robot might refuse to start, a desktop tool might warn and
carry on.

Read the errors rather than only the flag. Walk `result.errors` and use `code`
to branch when the response differs by failure, or call `RuleError::str()` for
one already-specific sentence.

## Build options

| Profile | cmake |
|---|---|
| core only | `cmake -S . -B build -DRETE_ENABLE_JSON=OFF -DRETE_ENABLE_YAML=OFF` |
| core + JSON | `cmake -S . -B build` |
| core + JSON + YAML | `cmake -S . -B build -DRETE_ENABLE_YAML=ON` |
| embedded, JSON only | `cmake -S . -B build -DRETE_EMBEDDED=ON -DRETE_ENABLE_JSON=ON -DRETE_ENABLE_YAML=OFF` |

`RETE_ENABLE_YAML=ON` needs yaml-cpp:

```bash
brew install yaml-cpp                 # macOS
sudo apt install libyaml-cpp-dev      # Debian, Ubuntu
```

If CMake cannot find it, point `CMAKE_PREFIX_PATH` at the install. The configure
step fails rather than falling back to JSON, because a silent fallback would
produce a robot that boots, loads nothing and runs with no behaviour.

`RETE_EMBEDDED=ON` with `RETE_ENABLE_YAML=ON` is rejected at configure time.
`config.hpp` also `#error`s on it, but a compile error inside a header is a poor
way to learn that two cache variables disagree.

The macros are readable at compile time through `rete::BuildFeatures`:

```cpp
static_assert(rete::BuildFeatures::json, "this target needs the JSON reader");
if (rete::BuildFeatures::yaml) { /* ... */ }
```

## Embedded targets

`RETE_EMBEDDED=1` is a profile, not a platform test. Nothing in the library
branches on a device name. It forces JSON only, sets `RETE_NO_IOSTREAM`, and
turns off exceptions in this layer.

What that means in practice:

**No exceptions.** Every failure is a `LoadResult` or a `bool` with an out
parameter. `ConfigNode::find()` returns `nullptr` for a missing key rather than
throwing, and the scalar accessors report success through an `ok` out parameter.

**No RTTI dependency from this layer.** The rules and serialization headers use
no `dynamic_cast` and no `typeid`. (The engine's `compile_production` does use
`dynamic_cast` on beta nodes, so `-fno-rtti` is not a supported configuration
for the engine itself yet.)

**No iostreams.** `RETE_NO_IOSTREAM` removes `<fstream>` and with it
`load_from_file` and `load_from_files`. It also drops `<ostream>` and the Boost
DOT export from `rete.hpp`, so the embedded profile pulls in no stream headers
at all. An MCU target holds its rules in a flash
string and calls `load_from_string`. This is also why `examples/robotics` is not
built on the embedded profile: it prints a trace to a console the target does
not have.

**Parse once, then release.** The `ConfigNode` tree exists only for the duration
of the load and is cleared before `load_from_string` returns. Peak allocation
during a load is roughly the document plus the tree; steady state afterwards is
the productions alone. Load rules at init, when the heap is least fragmented,
and do not reload in a control loop.

**JSON nesting is capped at 64.** `kJsonMaxDepth` bounds the recursion in
`parse_value`. This is a security property rather than a style choice: a few
hundred bytes of `[[[[[...` must return an error, not run the stack off the end.
A rule document bottoms out around five levels, so 64 is far past anything the
schema can produce.

**YAML is not recommended below Linux-class parts.** yaml-cpp reports every
failure by throwing, so it cannot be built with `-fno-exceptions`, and it brings
a C++ standard library dependency and tens of kilobytes of code for a file that
holds a dozen rules. On a part with a few hundred kilobytes of flash that is the
wrong trade. Write the rules in YAML on a workstation if you prefer it, convert
to JSON as a build step, and ship the JSON.

## Platform guidance

Guidance, nothing more. No device name appears anywhere in the library logic:
the only switches are `RETE_ENABLE_JSON`, `RETE_ENABLE_YAML` and
`RETE_EMBEDDED`, and the table below is a summary of what those tend to be set
to, not something the code consults.

| Platform | Recommended | Supported |
|---|---|---|
| Linux, desktop | YAML or JSON | both |
| Pi Zero, Zero 2 W | YAML | both |
| Pi 4, Pi 5 | YAML | both |
| Jetson | YAML | both |
| RP2040 / Pico, RP2350 / Pico 2 | JSON | JSON only |
| Arduino MCU | JSON | JSON only |
| ESP32 | JSON | YAML only if deliberately enabled |

## Known limitations

**No source line numbers from the parsers.** `ConfigNode` carries no source
position, so `RuleError::line` reports `RuleDefinition::line`, which is 0 unless
the caller filled it in. Errors name the rule and a dotted field path
(`when[1].op`), which locates the problem in the document even without a line.
The number is propagated everywhere it is known, so a parser that starts
recording positions needs no change in the validator. Parse failures do carry a
line, because `ParseError` has one.

**A successful YAML parse is not proof the file is well formed.** yaml-cpp
accepts an unterminated quote in block context: `name: "x` folds to the end of
the input rather than erroring. That is scanner laxness below the adapter, not
something this project can fix from above. JSON is strict here and rejects it.

**One rule can hide its own second error.** Document-shape problems are caught
while reading and the rule is dropped before the validator sees it, so a rule
with a bad `salience` will not also report its unknown action id until the
salience is fixed. Errors across different rules are all reported in one pass.

**No hot reload.** There is no file watcher, no atomic swap and no versioning.
Reloading means calling the loader again with `replace_existing = true`, and
that is not safe to do from inside an action handler while `run()` is walking
the agenda. Load at init, or reload between `run()` calls.

A reload with `replace_existing` does what it says: a rule whose new definition
says `enabled: false` is removed from the engine rather than left running, and
a replaced rule starts with clean refraction rather than inheriting the old
one's. Both of those were wrong at first and are covered by regression tests,
because a hot-reloaded behaviour that silently never runs is the failure mode
that costs a field day to find.

**Salience ties resolve in definition order.** Two rules at the same salience
fire earliest-defined first, which for a rule file means document order, and
for `load_from_files` means file order. This is deterministic, but it is a
weak signal: adding a rule earlier in the file changes it. Give rules that
genuinely must be ordered different saliences.

**No arithmetic, no functions, no cross-condition comparison.** A guard compares
a bound variable against a literal. Comparing two bound variables, or computing
`a + b > c`, is not expressible and is rejected at load time rather than
silently ignored. That is a deliberate limit on what a config file can do; put
the arithmetic in an action handler or in the code that publishes the facts.

**`bind` on `not_exists` is rejected**, not ignored. There is no value to bind
when the match is an absence.

## Backwards compatibility

The programmatic API is unchanged. Existing code compiles and behaves exactly as
before, and nothing here is required to use the engine.

```cpp
#include <rete/rete.hpp>

rete::ReteEngine engine;

engine.add_rule("bird-rule")
    .salience(10)
    .when(std::string("?x"), std::string("has"), std::string("feathers"))
    .when(std::string("?x"), std::string("can"), std::string("fly"))
    .then([](rete::ReteEngine& e, const rete::Bindings& b) {
        e.assert_fact(b.at("?x"), std::string("is"), std::string("bird"));
    })
    .build();
```

Three additions to the engine, all of them new surface rather than changed
surface:

- `ReteEngine::has_rule(name)` and `ReteEngine::rule_names()`, which the loader
  uses to detect a name the engine already holds.
- `RuleBuilder::where(variable, GuardOp, operand)`, the hand-written equivalent
  of a guard operator. `Production::guards` is empty for every rule built
  without it, and `run()` skips the check when it is.
- `Agenda::clear_refraction_for(production)`, called by `remove_rule`.

Two behaviours changed rather than only being added to, and both are fixes a
programmatic user benefits from too. `remove_rule` now discards the removed
rule's refraction entries; previously they were left keyed on a freed
`Production*` and the next allocation at that address inherited them. And a
salience tie now resolves in definition order instead of in hash order.

A rule loaded from a file and the same rule written with `add_rule()` produce
the same `Production` and behave identically. The loader is a different way to
build one, not a different engine.
