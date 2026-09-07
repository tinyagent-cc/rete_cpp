// ---------------------------------------------------------------------------
// The embedded profile, compiled the way an MCU target compiles it.
//
// This translation unit is the contract, not a demonstration. It is built with
// RETE_EMBEDDED=1, RETE_ENABLE_YAML=0 and -fno-exceptions, and it uses no
// iostream, no <filesystem>, no POSIX and no Catch (which needs exceptions).
// If a host-only dependency ever reaches the rules layer, this stops compiling
// on the machine that added it rather than on the bench weeks later.
//
// It still runs: a compile-only check would pass on a loader that returns
// nothing, and "the rules parsed" is the claim being made.
// ---------------------------------------------------------------------------

#include <rete/rules/action_registry.hpp>
#include <rete/rules/error.hpp>
#include <rete/rules/loader.hpp>

#include <cstdio>
#include <string>
#include <vector>

#if RETE_HAS_EXCEPTIONS
#error "this translation unit must be built with exceptions off"
#endif
#if RETE_ENABLE_YAML
#error "the embedded profile is JSON only"
#endif
#if !RETE_EMBEDDED
#error "this translation unit must be built with RETE_EMBEDDED=1"
#endif

using namespace rete;
using namespace rete::rules;

namespace {

int failures = 0;

void check(bool condition, const char* what, int line) {
    if (condition) return;
    ++failures;
    std::printf("FAIL line %d: %s\n", line, what);
}

#define CHECK(expr) check((expr), #expr, __LINE__)

Value str(const char* s) { return Value(std::string(s)); }

const char* kDocument =
    "{\n"
    "  \"rules\": [\n"
    "    {\n"
    "      \"name\": \"low_battery_return\",\n"
    "      \"salience\": 300,\n"
    "      \"when\": [\n"
    "        {\"fact\": \"battery\", \"attribute\": \"level\", \"op\": \"less_than\", \"value\": 20}\n"
    "      ],\n"
    "      \"then\": [\n"
    "        {\"action\": \"return_to_base\", \"params\": {\"speed\": 0.4, \"announce\": true}}\n"
    "      ]\n"
    "    },\n"
    "    {\n"
    "      \"name\": \"idle_report\",\n"
    "      \"salience\": 10,\n"
    "      \"when\": [\n"
    "        {\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"idle\"}\n"
    "      ],\n"
    "      \"then\": [{\"action\": \"report\"}]\n"
    "    }\n"
    "  ]\n"
    "}\n";

} // namespace

int main() {
    CHECK(BuildFeatures::embedded);
    CHECK(BuildFeatures::json);
    CHECK(!BuildFeatures::yaml);
    CHECK(!BuildFeatures::exceptions);

    ReteEngine     engine;
    ActionRegistry registry;

    int    returns  = 0;
    int    reports  = 0;
    double speed    = 0.0;
    bool   announce = false;

    registry.register_action("return_to_base",
        [&](ReteEngine&, const Bindings&, const ActionParams& p) {
            ++returns;
            speed    = p.get_double("speed");
            announce = p.get_bool("announce");
        });
    registry.register_action("report",
        [&](ReteEngine&, const Bindings&, const ActionParams&) { ++reports; });

    RuleLoader loader(engine, registry);
    LoadResult result = loader.load_from_string(std::string(kDocument), Format::Json);

    CHECK(result.ok());
    CHECK(result.loaded == 2u);
    CHECK(result.rule_names.size() == result.loaded);
    CHECK(engine.has_rule("low_battery_return"));
    CHECK(engine.has_rule("idle_report"));

    auto battery = engine.assert_fact(str("battery"), str("level"), Value(int64_t(90)));
    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();

    CHECK(returns == 0);
    CHECK(reports == 1);

    engine.modify_fact(battery, str("battery"), str("level"), Value(int64_t(8)));
    engine.run();

    CHECK(returns == 1);
    CHECK(speed > 0.39 && speed < 0.41);
    CHECK(announce);

    // Same match, second cycle: refraction has to hold here too.
    engine.run();
    CHECK(returns == 1);

    // A malformed document is a return value, never a thrown exception.
    ReteEngine     spare;
    RuleLoader     spare_loader(spare, registry);
    LoadResult     bad = spare_loader.load_from_string(std::string("{\"rules\": ["), Format::Json);
    CHECK(!bad.ok());
    CHECK(bad.errors.size() == 1u);
    CHECK(bad.errors[0].code == ErrorCode::ParseFailure);
    CHECK(spare.rule_count() == 0u);

    // YAML is not merely absent from this build, it is refused by name.
    LoadResult yaml = spare_loader.load_from_string(
        std::string("rules:\n  - name: r\n"), Format::Yaml);
    CHECK(!yaml.ok());
    CHECK(yaml.errors.size() == 1u);
    CHECK(yaml.errors[0].code == ErrorCode::UnsupportedFeature);

    if (failures == 0) {
        std::printf("embedded profile: all checks passed\n");
        return 0;
    }
    std::printf("embedded profile: %d check(s) failed\n", failures);
    return 1;
}
