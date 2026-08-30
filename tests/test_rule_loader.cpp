#include "catch.hpp"

#include <rete/rete.hpp>
#include <rete/rules/action_registry.hpp>
#include <rete/rules/definition.hpp>
#include <rete/rules/error.hpp>
#include <rete/rules/loader.hpp>
#include <rete/rules/validator.hpp>

#include <cstdint>
#include <string>
#include <vector>

using namespace rete;
using namespace rete::rules;

namespace {

// Value's first non-monostate alternative is bool, so a bare "battery" would
// silently become true. Every literal below goes through these.
Value str(const char* s) { return Value(std::string(s)); }
Value num(int64_t v) { return Value(v); }

ConditionDefinition cond(const char* fact, const char* attribute, Operator op,
                         Value value = Value{}, const char* bind = "") {
    ConditionDefinition c;
    c.fact      = fact;
    c.attribute = attribute;
    c.op        = op;
    c.value     = std::move(value);
    c.bind      = bind;
    return c;
}

ActionDefinition action(const char* id, ParameterMap params = {}) {
    ActionDefinition a;
    a.id         = id;
    a.parameters = std::move(params);
    return a;
}

RuleDefinition make_rule(const char* name, int salience,
                         std::vector<ConditionDefinition> conditions,
                         std::vector<ActionDefinition> actions) {
    RuleDefinition d;
    d.name       = name;
    d.salience   = salience;
    d.conditions = std::move(conditions);
    d.actions    = std::move(actions);
    return d;
}

bool has_code(const LoadResult& r, ErrorCode code) {
    for (const auto& e : r.errors)
        if (e.code == code) return true;
    return false;
}

bool mentions(const LoadResult& r, const char* needle) {
    for (const auto& e : r.errors)
        if (e.message.find(needle) != std::string::npos) return true;
    return false;
}

} // namespace

// ===========================================================================
// The hand-written API the loader was added next to
// ===========================================================================

TEST_CASE("Programmatic rule builder still builds and fires", "[rules][api]") {
    ReteEngine engine;
    int fires = 0;

    engine.add_rule("classic")
        .salience(50)
        .when(std::string("?x"), std::string("on"), std::string("?y"))
        .then([&](ReteEngine&, const Bindings& b) {
            REQUIRE(b.count("?x") == 1);
            ++fires;
        })
        .build();

    engine.assert_fact(str("B1"), str("on"), str("B2"));
    engine.run();

    REQUIRE(fires == 1);
    REQUIRE(engine.has_rule("classic"));
    REQUIRE(engine.rule_names().size() == 1u);
}

TEST_CASE("where() adds a guard to a hand-written rule", "[rules][api][guard]") {
    ReteEngine engine;
    int fires = 0;

    engine.add_rule("low-battery")
        .when(std::string("battery"), std::string("level"), std::string("?lvl"))
        .where("?lvl", GuardOp::LessThan, num(20))
        .then([&](ReteEngine&, const Bindings&) { ++fires; })
        .build();

    auto wme = engine.assert_fact(str("battery"), str("level"), num(80));
    engine.run();
    REQUIRE(fires == 0);

    engine.modify_fact(wme, str("battery"), str("level"), num(12));
    engine.run();
    REQUIRE(fires == 1);

    // Refraction is recorded only once the guard has passed, so a second cycle
    // over the same match must not fire again.
    engine.run();
    REQUIRE(fires == 1);
}

TEST_CASE("Loaded rules and hand-written rules share one engine", "[rules][api]") {
    ReteEngine    engine;
    ActionRegistry registry;
    int loaded_fires = 0;
    int classic_fires = 0;

    registry.register_action("count", [&](ReteEngine&, const Bindings&, const ActionParams&) {
        ++loaded_fires;
    });

    engine.add_rule("classic")
        .when(std::string("robot"), std::string("state"), std::string("idle"))
        .then([&](ReteEngine&, const Bindings&) { ++classic_fires; })
        .build();

    std::vector<RuleDefinition> defs{
        make_rule("loaded", 0, {cond("robot", "state", Operator::Equals, str("idle"))},
                  {action("count")})};

    LoadResult result = load_rules(engine, registry, defs);
    REQUIRE(result.ok());
    REQUIRE(result.loaded == 1u);

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();

    REQUIRE(classic_fires == 1);
    REQUIRE(loaded_fires == 1);
    REQUIRE(engine.rule_count() == 2u);
}

// ===========================================================================
// Operators
// ===========================================================================

TEST_CASE("Every operator spelling and alias parses", "[rules][operator]") {
    struct Row { const char* text; Operator op; };
    const Row rows[] = {
        {"equals", Operator::Equals}, {"eq", Operator::Equals}, {"==", Operator::Equals},
        {"not_equals", Operator::NotEquals}, {"ne", Operator::NotEquals}, {"!=", Operator::NotEquals},
        {"exists", Operator::Exists},
        {"not_exists", Operator::NotExists},
        {"bind", Operator::Bind},
        {"greater_than", Operator::GreaterThan}, {"gt", Operator::GreaterThan}, {">", Operator::GreaterThan},
        {"greater_or_equal", Operator::GreaterOrEqual}, {"gte", Operator::GreaterOrEqual}, {">=", Operator::GreaterOrEqual},
        {"less_than", Operator::LessThan}, {"lt", Operator::LessThan}, {"<", Operator::LessThan},
        {"less_or_equal", Operator::LessOrEqual}, {"lte", Operator::LessOrEqual}, {"<=", Operator::LessOrEqual},
    };

    for (const auto& row : rows) {
        Operator op = Operator::NotExists;
        INFO("operator spelling: " << row.text);
        REQUIRE(parse_operator(row.text, op));
        REQUIRE(op == row.op);
    }

    Operator ignored = Operator::Equals;
    REQUIRE_FALSE(parse_operator("approximately", ignored));
    REQUIRE_FALSE(parse_operator("=", ignored));
    REQUIRE_FALSE(parse_operator("", ignored));
}

TEST_CASE("Guard operators are the ones the network cannot test", "[rules][operator]") {
    REQUIRE(is_guard_operator(Operator::NotEquals));
    REQUIRE(is_guard_operator(Operator::GreaterThan));
    REQUIRE(is_guard_operator(Operator::GreaterOrEqual));
    REQUIRE(is_guard_operator(Operator::LessThan));
    REQUIRE(is_guard_operator(Operator::LessOrEqual));

    REQUIRE_FALSE(is_guard_operator(Operator::Equals));
    REQUIRE_FALSE(is_guard_operator(Operator::Exists));
    REQUIRE_FALSE(is_guard_operator(Operator::NotExists));
    REQUIRE_FALSE(is_guard_operator(Operator::Bind));
}

TEST_CASE("equals matches a constant", "[rules][operator]") {
    ReteEngine     engine;
    ActionRegistry registry;
    int fires = 0;
    registry.register_action("go", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

    std::vector<RuleDefinition> defs{
        make_rule("r", 0, {cond("robot", "state", Operator::Equals, str("idle"))}, {action("go")})};
    REQUIRE(load_rules(engine, registry, defs).ok());

    engine.assert_fact(str("robot"), str("state"), str("busy"));
    engine.run();
    REQUIRE(fires == 0);

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();
    REQUIRE(fires == 1);
}

TEST_CASE("not_equals fires only when the fact is present and differs", "[rules][operator][guard]") {
    ReteEngine     engine;
    ActionRegistry registry;
    int fires = 0;
    registry.register_action("go", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

    std::vector<RuleDefinition> defs{
        make_rule("r", 0, {cond("robot", "state", Operator::NotEquals, str("idle"))}, {action("go")})};
    REQUIRE(load_rules(engine, registry, defs).ok());

    // Absent fact: nothing matches, which is the whole reason not_equals is a
    // guard rather than a negated condition.
    engine.run();
    REQUIRE(fires == 0);

    auto wme = engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();
    REQUIRE(fires == 0);

    engine.modify_fact(wme, str("robot"), str("state"), str("charging"));
    engine.run();
    REQUIRE(fires == 1);
}

TEST_CASE("exists matches any value and binds it", "[rules][operator]") {
    ReteEngine     engine;
    ActionRegistry registry;
    std::vector<std::string> seen;
    registry.register_action("go", [&](ReteEngine&, const Bindings& b, const ActionParams&) {
        for (const auto& kv : b) seen.push_back(value_to_string(kv.second));
    });

    std::vector<RuleDefinition> defs{
        make_rule("r", 0, {cond("battery", "level", Operator::Exists)}, {action("go")})};
    REQUIRE(load_rules(engine, registry, defs).ok());

    engine.assert_fact(str("battery"), str("level"), num(73));
    engine.run();

    REQUIRE(seen.size() == 1u);
    REQUIRE(seen[0] == "73");
}

TEST_CASE("not_exists matches the absence of a fact", "[rules][operator]") {
    ReteEngine     engine;
    ActionRegistry registry;
    int fires = 0;
    registry.register_action("go", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

    // The negated clause is second because a negation needs a token to test
    // against; that is an engine property, not a loader one.
    std::vector<RuleDefinition> defs{
        make_rule("r", 0,
                  {cond("robot", "state", Operator::Equals, str("idle")),
                   cond("robot", "fault", Operator::NotExists, str("overheat"))},
                  {action("go")})};
    REQUIRE(load_rules(engine, registry, defs).ok());

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.assert_fact(str("robot"), str("fault"), str("overheat"));
    engine.run();
    REQUIRE(fires == 0);
}

TEST_CASE("not_exists lets a rule through when the fact is missing", "[rules][operator]") {
    ReteEngine     engine;
    ActionRegistry registry;
    int fires = 0;
    registry.register_action("go", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

    std::vector<RuleDefinition> defs{
        make_rule("r", 0,
                  {cond("robot", "state", Operator::Equals, str("idle")),
                   cond("robot", "fault", Operator::NotExists, str("overheat"))},
                  {action("go")})};
    REQUIRE(load_rules(engine, registry, defs).ok());

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.assert_fact(str("robot"), str("fault"), str("low_ink"));
    engine.run();
    REQUIRE(fires == 1);
}

TEST_CASE("bind names the matched value for the handler", "[rules][operator]") {
    ReteEngine     engine;
    ActionRegistry registry;
    std::string bound;
    registry.register_action("go", [&](ReteEngine&, const Bindings& b, const ActionParams&) {
        auto it = b.find("?level");
        if (it != b.end()) bound = value_to_string(it->second);
    });

    std::vector<RuleDefinition> defs{
        make_rule("r", 0, {cond("battery", "level", Operator::Bind, Value{}, "?level")},
                  {action("go")})};
    REQUIRE(load_rules(engine, registry, defs).ok());

    engine.assert_fact(str("battery"), str("level"), num(42));
    engine.run();
    REQUIRE(bound == "42");
}

TEST_CASE("Ordering operators compare the matched value against a literal", "[rules][operator][guard]") {
    struct Row { const char* name; Operator op; int64_t operand; bool at_10; bool at_20; bool at_30; };
    const Row rows[] = {
        {"gt",  Operator::GreaterThan,    20, false, false, true},
        {"gte", Operator::GreaterOrEqual, 20, false, true,  true},
        {"lt",  Operator::LessThan,       20, true,  false, false},
        {"lte", Operator::LessOrEqual,    20, true,  true,  false},
    };

    for (const auto& row : rows) {
        const int64_t samples[] = {10, 20, 30};
        const bool    expect[]  = {row.at_10, row.at_20, row.at_30};

        for (int i = 0; i < 3; ++i) {
            INFO("operator " << row.name << " against " << samples[i]);
            ReteEngine     engine;
            ActionRegistry registry;
            int fires = 0;
            registry.register_action("go",
                [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

            std::vector<RuleDefinition> defs{
                make_rule("r", 0, {cond("battery", "level", row.op, num(row.operand))},
                          {action("go")})};
            REQUIRE(load_rules(engine, registry, defs).ok());

            engine.assert_fact(str("battery"), str("level"), num(samples[i]));
            engine.run();
            REQUIRE(fires == (expect[i] ? 1 : 0));
        }
    }
}

// ===========================================================================
// Guard semantics: the behaviour a naive implementation gets wrong
// ===========================================================================

TEST_CASE("A guard rule waits for the value to move into range", "[rules][guard]") {
    ReteEngine     engine;
    ActionRegistry registry;
    int fires = 0;
    registry.register_action("return_to_base",
        [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

    std::vector<RuleDefinition> defs{
        make_rule("low_battery_return", 0,
                  {cond("battery", "level", Operator::LessThan, num(20))},
                  {action("return_to_base")})};
    REQUIRE(load_rules(engine, registry, defs).ok());

    auto wme = engine.assert_fact(str("battery"), str("level"), num(80));

    SECTION("out of range does not fire") {
        engine.run();
        REQUIRE(fires == 0);
    }

    SECTION("moving into range fires exactly once") {
        engine.run();
        REQUIRE(fires == 0);

        engine.modify_fact(wme, str("battery"), str("level"), num(15));
        engine.run();
        REQUIRE(fires == 1);

        // Refraction: the same match must not fire a second time.
        engine.run();
        REQUIRE(fires == 1);

        // Still in range but a different fact, so it is a different match.
        engine.modify_fact(wme, str("battery"), str("level"), num(5));
        engine.run();
        REQUIRE(fires == 2);
    }

    SECTION("moving back out of range stops it firing again") {
        engine.modify_fact(wme, str("battery"), str("level"), num(15));
        engine.run();
        REQUIRE(fires == 1);

        engine.modify_fact(wme, str("battery"), str("level"), num(90));
        engine.run();
        REQUIRE(fires == 1);
    }
}

TEST_CASE("A failed guard leaves no refraction entry behind", "[rules][guard]") {
    // The distinction that matters: a match whose guard failed is not "fired
    // and done". Running twice while out of range, then moving into range,
    // must still fire.
    ReteEngine     engine;
    ActionRegistry registry;
    int fires = 0;
    registry.register_action("go", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

    std::vector<RuleDefinition> defs{
        make_rule("r", 0, {cond("battery", "level", Operator::LessThan, num(20))}, {action("go")})};
    REQUIRE(load_rules(engine, registry, defs).ok());

    auto wme = engine.assert_fact(str("battery"), str("level"), num(50));
    engine.run();
    engine.run();
    engine.run();
    REQUIRE(fires == 0);

    engine.modify_fact(wme, str("battery"), str("level"), num(1));
    engine.run();
    REQUIRE(fires == 1);
}

TEST_CASE("A guard that cannot compare fails closed", "[rules][guard]") {
    ReteEngine     engine;
    ActionRegistry registry;
    int fires = 0;
    registry.register_action("go", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

    std::vector<RuleDefinition> defs{
        make_rule("r", 0, {cond("battery", "level", Operator::LessThan, num(20))}, {action("go")})};
    REQUIRE(load_rules(engine, registry, defs).ok());

    // A string where the rule expects a number has no ordering, so the rule
    // stays quiet rather than guessing.
    engine.assert_fact(str("battery"), str("level"), str("unknown"));
    engine.run();
    REQUIRE(fires == 0);
}

// ===========================================================================
// Salience
// ===========================================================================

TEST_CASE("Salience decides the order rules fire in", "[rules][salience]") {
    ReteEngine     engine;
    ActionRegistry registry;
    std::vector<std::string> order;

    registry.register_action("first",  [&](ReteEngine&, const Bindings&, const ActionParams&) { order.push_back("first"); });
    registry.register_action("second", [&](ReteEngine&, const Bindings&, const ActionParams&) { order.push_back("second"); });
    registry.register_action("third",  [&](ReteEngine&, const Bindings&, const ActionParams&) { order.push_back("third"); });

    // Declared lowest first on purpose: if salience were ignored the order
    // would come out declaration-shaped and the test would notice.
    std::vector<RuleDefinition> defs{
        make_rule("c", 1,   {cond("robot", "state", Operator::Equals, str("idle"))}, {action("third")}),
        make_rule("a", 300, {cond("robot", "state", Operator::Equals, str("idle"))}, {action("first")}),
        make_rule("b", 100, {cond("robot", "state", Operator::Equals, str("idle"))}, {action("second")}),
    };
    REQUIRE(load_rules(engine, registry, defs).loaded == 3u);

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();

    REQUIRE(order.size() == 3u);
    REQUIRE(order[0] == "first");
    REQUIRE(order[1] == "second");
    REQUIRE(order[2] == "third");
}

TEST_CASE("Salience survives compilation into a Production", "[rules][salience]") {
    ActionRegistry registry;
    registry.register_action("go", [](ReteEngine&, const Bindings&, const ActionParams&) {});

    RuleDefinition def = make_rule("r", -42,
                                   {cond("a", "b", Operator::Equals, str("c"))}, {action("go")});
    Production prod = compile_rule(def, registry);

    REQUIRE(prod.name == "r");
    REQUIRE(prod.salience == -42);
    REQUIRE(prod.conditions.size() == 1u);
    REQUIRE(prod.guards.empty());
    REQUIRE(static_cast<bool>(prod.action));
}

TEST_CASE("A comparison compiles to a binding plus a guard", "[rules][salience][guard]") {
    ActionRegistry registry;
    registry.register_action("go", [](ReteEngine&, const Bindings&, const ActionParams&) {});

    RuleDefinition def = make_rule("r", 0,
                                   {cond("battery", "level", Operator::LessThan, num(20))},
                                   {action("go")});
    Production prod = compile_rule(def, registry);

    REQUIRE(prod.conditions.size() == 1u);
    REQUIRE(prod.guards.size() == 1u);
    REQUIRE(prod.guards[0].op == GuardOp::LessThan);
    REQUIRE(is_generated_variable(prod.guards[0].variable));
}

// ===========================================================================
// Actions
// ===========================================================================

TEST_CASE("Typed action parameters reach the handler", "[rules][action]") {
    ReteEngine     engine;
    ActionRegistry registry;

    std::string captured_string;
    int64_t     captured_int    = 0;
    double      captured_double = 0.0;
    bool        captured_bool   = false;
    std::size_t captured_size   = 0;

    registry.register_action("drive", [&](ReteEngine&, const Bindings&, const ActionParams& p) {
        captured_string = p.get_string("mode");
        captured_int    = p.get_int("retries");
        captured_double = p.get_double("speed");
        captured_bool   = p.get_bool("announce");
        captured_size   = p.size();

        // A missing parameter is not an error; it is the fallback.
        REQUIRE(p.get_string("nothing", "fallback") == "fallback");
        REQUIRE(p.get_int("nothing", 7) == 7);
        REQUIRE_FALSE(p.has("nothing"));
        REQUIRE(p.has("mode"));
    });

    ParameterMap params{
        {"mode", str("cautious")},
        {"retries", num(3)},
        {"speed", Value(0.4)},
        {"announce", Value(true)},
    };

    std::vector<RuleDefinition> defs{
        make_rule("r", 0, {cond("robot", "state", Operator::Equals, str("idle"))},
                  {action("drive", params)})};
    REQUIRE(load_rules(engine, registry, defs).ok());

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();

    REQUIRE(captured_string == "cautious");
    REQUIRE(captured_int == 3);
    REQUIRE(captured_double == Approx(0.4));
    REQUIRE(captured_bool);
    REQUIRE(captured_size == 4u);
}

TEST_CASE("Multiple actions run in document order", "[rules][action]") {
    ReteEngine     engine;
    ActionRegistry registry;
    std::vector<std::string> order;

    registry.register_action("stop",   [&](ReteEngine&, const Bindings&, const ActionParams&) { order.push_back("stop"); });
    registry.register_action("beep",   [&](ReteEngine&, const Bindings&, const ActionParams&) { order.push_back("beep"); });
    registry.register_action("report", [&](ReteEngine&, const Bindings&, const ActionParams&) { order.push_back("report"); });

    std::vector<RuleDefinition> defs{
        make_rule("r", 0, {cond("robot", "state", Operator::Equals, str("idle"))},
                  {action("stop"), action("beep"), action("report")})};
    REQUIRE(load_rules(engine, registry, defs).ok());

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();

    REQUIRE(order.size() == 3u);
    REQUIRE(order[0] == "stop");
    REQUIRE(order[1] == "beep");
    REQUIRE(order[2] == "report");
}

TEST_CASE("The handler receives the bindings the match produced", "[rules][action]") {
    ReteEngine     engine;
    ActionRegistry registry;
    std::string block;
    std::string level;

    registry.register_action("record", [&](ReteEngine&, const Bindings& b, const ActionParams&) {
        auto x = b.find("?block");
        auto l = b.find("?level");
        if (x != b.end()) block = value_to_string(x->second);
        if (l != b.end()) level = value_to_string(l->second);
    });

    std::vector<RuleDefinition> defs{
        make_rule("r", 0,
                  {cond("battery", "level", Operator::Bind, Value{}, "?level"),
                   cond("gripper", "holding", Operator::Bind, Value{}, "?block")},
                  {action("record")})};
    REQUIRE(load_rules(engine, registry, defs).ok());

    engine.assert_fact(str("battery"), str("level"), num(64));
    engine.assert_fact(str("gripper"), str("holding"), str("B7"));
    engine.run();

    REQUIRE(level == "64");
    REQUIRE(block == "B7");
}

TEST_CASE("An action the registry does not know stops only its own rule", "[rules][error]") {
    ReteEngine     engine;
    ActionRegistry registry;
    int good_fires = 0;
    registry.register_action("known", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++good_fires; });

    std::vector<RuleDefinition> defs{
        make_rule("before", 0, {cond("robot", "state", Operator::Equals, str("idle"))}, {action("known")}),
        make_rule("broken", 0, {cond("robot", "state", Operator::Equals, str("idle"))}, {action("nowhere")}),
        make_rule("after",  0, {cond("robot", "state", Operator::Equals, str("idle"))}, {action("known")}),
    };

    LoadResult result = load_rules(engine, registry, defs);

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.errors.size() == 1u);
    REQUIRE(result.errors[0].code == ErrorCode::UnknownAction);
    REQUIRE(result.errors[0].rule == "broken");
    REQUIRE(result.loaded == 2u);
    REQUIRE(result.rule_names.size() == result.loaded);
    REQUIRE_FALSE(engine.has_rule("broken"));

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();
    REQUIRE(good_fires == 2);
}

TEST_CASE("The registry refuses to shadow a name silently", "[rules][action]") {
    ActionRegistry registry;
    REQUIRE(registry.register_action("go", [](ReteEngine&, const Bindings&, const ActionParams&) {}));
    REQUIRE_FALSE(registry.register_action("go", [](ReteEngine&, const Bindings&, const ActionParams&) {}));
    REQUIRE(registry.register_action("go", [](ReteEngine&, const Bindings&, const ActionParams&) {}, true));
    REQUIRE(registry.contains("go"));
    REQUIRE(registry.size() == 1u);
    REQUIRE(registry.unregister_action("go"));
    REQUIRE_FALSE(registry.contains("go"));
}

// ===========================================================================
// Validation errors that do not need a parser
// ===========================================================================

TEST_CASE("A rule needs a name", "[rules][error]") {
    ReteEngine     engine;
    ActionRegistry registry;
    registry.register_action("go", [](ReteEngine&, const Bindings&, const ActionParams&) {});

    std::vector<RuleDefinition> defs{
        make_rule("", 0, {cond("a", "b", Operator::Equals, str("c"))}, {action("go")})};

    LoadResult result = load_rules(engine, registry, defs);
    REQUIRE(has_code(result, ErrorCode::EmptyRuleName));
    REQUIRE(result.loaded == 0u);
}

TEST_CASE("A rule needs at least one when clause", "[rules][error]") {
    ReteEngine     engine;
    ActionRegistry registry;
    registry.register_action("go", [](ReteEngine&, const Bindings&, const ActionParams&) {});

    std::vector<RuleDefinition> defs{make_rule("r", 0, {}, {action("go")})};

    LoadResult result = load_rules(engine, registry, defs);
    REQUIRE(has_code(result, ErrorCode::EmptyConditions));
    REQUIRE(result.loaded == 0u);
}

TEST_CASE("A rule needs at least one then action", "[rules][error]") {
    ReteEngine     engine;
    ActionRegistry registry;

    std::vector<RuleDefinition> defs{
        make_rule("r", 0, {cond("a", "b", Operator::Equals, str("c"))}, {})};

    LoadResult result = load_rules(engine, registry, defs);
    REQUIRE(has_code(result, ErrorCode::MissingField));
    REQUIRE(result.loaded == 0u);
}

TEST_CASE("A duplicate name inside one batch is an error", "[rules][error]") {
    ReteEngine     engine;
    ActionRegistry registry;
    registry.register_action("go", [](ReteEngine&, const Bindings&, const ActionParams&) {});

    std::vector<RuleDefinition> defs{
        make_rule("twin", 0, {cond("a", "b", Operator::Equals, str("c"))}, {action("go")}),
        make_rule("twin", 0, {cond("a", "b", Operator::Equals, str("d"))}, {action("go")}),
        make_rule("other", 0, {cond("a", "b", Operator::Equals, str("e"))}, {action("go")}),
    };

    LoadResult result = load_rules(engine, registry, defs);
    REQUIRE(has_code(result, ErrorCode::DuplicateRuleName));
    REQUIRE(result.loaded == 2u);
    REQUIRE(engine.has_rule("twin"));
    REQUIRE(engine.has_rule("other"));
    REQUIRE(engine.rule_count() == 2u);
}

TEST_CASE("A name the engine already runs is an error unless replacing", "[rules][error]") {
    ReteEngine     engine;
    ActionRegistry registry;
    registry.register_action("go", [](ReteEngine&, const Bindings&, const ActionParams&) {});

    engine.add_rule("taken")
        .when(std::string("a"), std::string("b"), std::string("c"))
        .then([](ReteEngine&, const Bindings&) {})
        .build();

    std::vector<RuleDefinition> defs{
        make_rule("taken", 0, {cond("a", "b", Operator::Equals, str("c"))}, {action("go")})};

    LoadResult result = load_rules(engine, registry, defs);
    REQUIRE(has_code(result, ErrorCode::DuplicateRuleName));
    REQUIRE(mentions(result, "replace_existing"));
    REQUIRE(result.loaded == 0u);
    REQUIRE(engine.rule_count() == 1u);
}

TEST_CASE("An ordering operator refuses an operand it cannot order", "[rules][error]") {
    ReteEngine     engine;
    ActionRegistry registry;
    registry.register_action("go", [](ReteEngine&, const Bindings&, const ActionParams&) {});

    std::vector<RuleDefinition> defs{
        make_rule("r", 0, {cond("battery", "level", Operator::LessThan, Value(true))}, {action("go")})};

    LoadResult result = load_rules(engine, registry, defs);
    REQUIRE(has_code(result, ErrorCode::InvalidValueType));
    REQUIRE(result.loaded == 0u);
}

TEST_CASE("A comparison needs a value to compare against", "[rules][error]") {
    ReteEngine     engine;
    ActionRegistry registry;
    registry.register_action("go", [](ReteEngine&, const Bindings&, const ActionParams&) {});

    std::vector<RuleDefinition> defs{
        make_rule("r", 0, {cond("battery", "level", Operator::GreaterThan)}, {action("go")})};

    LoadResult result = load_rules(engine, registry, defs);
    REQUIRE(has_code(result, ErrorCode::MissingField));
    REQUIRE(result.loaded == 0u);
}

TEST_CASE("not_exists cannot bind", "[rules][error]") {
    ReteEngine     engine;
    ActionRegistry registry;
    registry.register_action("go", [](ReteEngine&, const Bindings&, const ActionParams&) {});

    std::vector<RuleDefinition> defs{
        make_rule("r", 0,
                  {cond("a", "b", Operator::Equals, str("c")),
                   cond("robot", "fault", Operator::NotExists, Value{}, "?fault")},
                  {action("go")})};

    LoadResult result = load_rules(engine, registry, defs);
    REQUIRE(has_code(result, ErrorCode::UnsupportedFeature));
    REQUIRE(result.loaded == 0u);
}

TEST_CASE("The generated-variable prefix is reserved", "[rules][error]") {
    ReteEngine     engine;
    ActionRegistry registry;
    registry.register_action("go", [](ReteEngine&, const Bindings&, const ActionParams&) {});

    REQUIRE(is_generated_variable("?$g0"));
    REQUIRE_FALSE(is_generated_variable("?level"));
    REQUIRE(generated_variable('g', 3) == "?$g3");

    std::vector<RuleDefinition> defs{
        make_rule("r", 0, {cond("battery", "level", Operator::Bind, Value{}, "?$g0")}, {action("go")})};

    LoadResult result = load_rules(engine, registry, defs);
    REQUIRE(has_code(result, ErrorCode::InvalidCondition));
    REQUIRE(result.loaded == 0u);
}

TEST_CASE("A condition cannot both pin a value and bind it", "[rules][error]") {
    ReteEngine     engine;
    ActionRegistry registry;
    registry.register_action("go", [](ReteEngine&, const Bindings&, const ActionParams&) {});

    std::vector<RuleDefinition> defs{
        make_rule("r", 0, {cond("battery", "level", Operator::Equals, num(20), "?level")}, {action("go")})};

    LoadResult result = load_rules(engine, registry, defs);
    REQUIRE(has_code(result, ErrorCode::InvalidCondition));
    REQUIRE(result.loaded == 0u);
}

TEST_CASE("stop_on_first_error adds nothing at all", "[rules][error]") {
    ReteEngine     engine;
    ActionRegistry registry;
    registry.register_action("go", [](ReteEngine&, const Bindings&, const ActionParams&) {});

    std::vector<RuleDefinition> defs{
        make_rule("good", 0, {cond("a", "b", Operator::Equals, str("c"))}, {action("go")}),
        make_rule("bad",  0, {cond("a", "b", Operator::Equals, str("c"))}, {action("missing")}),
        make_rule("also_good", 0, {cond("a", "b", Operator::Equals, str("d"))}, {action("go")}),
    };

    LoadOptions opts;
    opts.stop_on_first_error = true;

    LoadResult result = load_rules(engine, registry, defs, opts);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.loaded == 0u);
    REQUIRE(engine.rule_count() == 0u);
}

// ===========================================================================
// enabled, replacement and removal
// ===========================================================================

TEST_CASE("enabled false validates but never reaches the engine", "[rules][enabled]") {
    ReteEngine     engine;
    ActionRegistry registry;
    int fires = 0;
    registry.register_action("go", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

    RuleDefinition off = make_rule("parked", 0,
                                   {cond("robot", "state", Operator::Equals, str("idle"))},
                                   {action("go")});
    off.enabled = false;

    RuleLoader loader(engine, registry);
    LoadResult result = loader.load_definitions({
        off,
        make_rule("live", 0, {cond("robot", "state", Operator::Equals, str("idle"))}, {action("go")}),
    });

    REQUIRE(result.ok());
    REQUIRE(result.loaded == 1u);
    REQUIRE(result.rule_names.size() == 1u);
    REQUIRE(result.rule_names[0] == "live");
    REQUIRE(loader.disabled_rules().size() == 1u);
    REQUIRE(loader.disabled_rules()[0] == "parked");
    REQUIRE_FALSE(engine.has_rule("parked"));

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();
    REQUIRE(fires == 1);
}

TEST_CASE("replace_existing swaps a rule in place", "[rules][replace]") {
    ReteEngine     engine;
    ActionRegistry registry;
    int old_fires = 0;
    int new_fires = 0;
    registry.register_action("old", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++old_fires; });
    registry.register_action("new", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++new_fires; });

    RuleLoader loader(engine, registry);
    REQUIRE(loader.load_definitions({
        make_rule("behaviour", 0, {cond("robot", "state", Operator::Equals, str("idle"))},
                  {action("old")})}).ok());
    REQUIRE(engine.rule_count() == 1u);

    LoadOptions opts;
    opts.replace_existing = true;
    LoadResult result = loader.load_definitions({
        make_rule("behaviour", 0, {cond("robot", "state", Operator::Equals, str("idle"))},
                  {action("new")})}, opts);

    REQUIRE(result.ok());
    REQUIRE(result.loaded == 1u);
    REQUIRE(engine.rule_count() == 1u);
    REQUIRE(engine.has_rule("behaviour"));

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();

    REQUIRE(old_fires == 0);
    REQUIRE(new_fires == 1);
}

// ---------------------------------------------------------------------------
// KNOWN BUG, not fixed here. Tagged [!mayfail] because it fails
// intermittently and the intermittency is the point.
//
// Agenda's refraction set is keyed on a raw Production*, and
// ReteEngine::remove_rule() clears the agenda's pending activations for a
// removed production but not its refraction entries. replace_existing is
// remove-then-add, so the replacement Production is very often allocated at the
// address the old one just freed and inherits its refraction: for every match
// the old rule already fired on, the new rule stays silent.
//
// Whether it happens depends on what the allocator hands back, which is the
// worst possible failure mode. It passes on a laptop and goes quiet on a robot.
// A hot-reloaded behaviour that never runs is not a cosmetic defect.
//
// engine.clear_refraction() after a replace is the workaround; the test below
// this one pins that down and does pass reliably.
// ---------------------------------------------------------------------------
TEST_CASE("A rule can be replaced after it has already fired",
          "[rules][replace][!mayfail]") {
    ReteEngine     engine;
    ActionRegistry registry;
    int old_fires = 0, new_fires = 0;
    registry.register_action("old", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++old_fires; });
    registry.register_action("new", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++new_fires; });

    REQUIRE(load_rules(engine, registry, {
        make_rule("behaviour", 0, {cond("robot", "state", Operator::Equals, str("idle"))},
                  {action("old")})}).ok());

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();
    REQUIRE(old_fires == 1);

    LoadOptions opts;
    opts.replace_existing = true;
    REQUIRE(load_rules(engine, registry, {
        make_rule("behaviour", 0, {cond("robot", "state", Operator::Equals, str("idle"))},
                  {action("new")})}, opts).ok());

    engine.run();
    REQUIRE(engine.rule_count() == 1u);
    REQUIRE(old_fires == 1);
    REQUIRE(new_fires == 1);   // fails whenever the allocator reuses the address
}

TEST_CASE("clear_refraction lets a replaced rule see the matches it inherited",
          "[rules][replace]") {
    // The reliable half of the case above: whatever the allocator did, dropping
    // the refraction set puts the replacement in front of the existing match.
    ReteEngine     engine;
    ActionRegistry registry;
    int old_fires = 0, new_fires = 0;
    registry.register_action("old", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++old_fires; });
    registry.register_action("new", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++new_fires; });

    REQUIRE(load_rules(engine, registry, {
        make_rule("behaviour", 0, {cond("robot", "state", Operator::Equals, str("idle"))},
                  {action("old")})}).ok());

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();
    REQUIRE(old_fires == 1);

    LoadOptions opts;
    opts.replace_existing = true;
    REQUIRE(load_rules(engine, registry, {
        make_rule("behaviour", 0, {cond("robot", "state", Operator::Equals, str("idle"))},
                  {action("new")})}, opts).ok());

    engine.clear_refraction();
    engine.run();

    REQUIRE(engine.rule_count() == 1u);
    REQUIRE(old_fires == 1);
    REQUIRE(new_fires >= 1);
}

TEST_CASE("enabled false does not switch off a rule the engine already runs",
          "[rules][replace][enabled][known-sharp-edge]") {
    // KNOWN SHARP EDGE, pinned here rather than fixed: this is the loader's
    // behaviour today, not an endorsement of it.
    //
    // RuleLoader::install() takes the `!enabled` branch before the
    // replace_existing removal, so reloading a document with `enabled: false`
    // reports success, lists the rule as disabled, and leaves the previously
    // loaded version running. An operator who edits a file to switch a
    // behaviour off and reloads it gets ok() == true and a robot that still
    // does the thing. Call remove_rule() to actually stop it.
    ReteEngine     engine;
    ActionRegistry registry;
    int fires = 0;
    registry.register_action("go", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

    RuleLoader loader(engine, registry);
    REQUIRE(loader.load_definitions({
        make_rule("behaviour", 0, {cond("robot", "state", Operator::Equals, str("idle"))},
                  {action("go")})}).ok());
    REQUIRE(engine.has_rule("behaviour"));

    RuleDefinition off = make_rule("behaviour", 0,
                                   {cond("robot", "state", Operator::Equals, str("idle"))},
                                   {action("go")});
    off.enabled = false;

    LoadOptions opts;
    opts.replace_existing = true;
    LoadResult result = loader.load_definitions({off}, opts);

    REQUIRE(result.ok());
    REQUIRE(result.loaded == 0u);
    REQUIRE(loader.disabled_rules().size() == 1u);

    // The part that surprises: the rule is still in the engine and still fires.
    REQUIRE(engine.has_rule("behaviour"));
    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();
    REQUIRE(fires == 1);
}

TEST_CASE("remove_rule takes a loaded rule out of the engine", "[rules][replace]") {
    ReteEngine     engine;
    ActionRegistry registry;
    int fires = 0;
    registry.register_action("go", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

    REQUIRE(load_rules(engine, registry, {
        make_rule("temporary", 0, {cond("robot", "state", Operator::Equals, str("idle"))},
                  {action("go")})}).ok());
    REQUIRE(engine.has_rule("temporary"));

    engine.remove_rule("temporary");

    REQUIRE_FALSE(engine.has_rule("temporary"));
    REQUIRE(engine.rule_count() == 0u);

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();
    REQUIRE(fires == 0);

    // The name is free again, which is what makes remove-then-load a
    // supported way to swap behaviour.
    REQUIRE(load_rules(engine, registry, {
        make_rule("temporary", 0, {cond("robot", "state", Operator::Equals, str("idle"))},
                  {action("go")})}).ok());
}

// ===========================================================================
// Format resolution and build features
// ===========================================================================

TEST_CASE("Format is guessed from the path, then from the text", "[rules][format]") {
    REQUIRE(format_from_path("rules.json") == Format::Json);
    REQUIRE(format_from_path("RULES.JSON") == Format::Json);
    REQUIRE(format_from_path("rules.yaml") == Format::Yaml);
    REQUIRE(format_from_path("rules.YML")  == Format::Yaml);
    REQUIRE(format_from_path("rules.txt")  == Format::Auto);

    REQUIRE(format_from_text("  {\"rules\": []}") == Format::Json);
    REQUIRE(format_from_text("\n[]") == Format::Json);
    REQUIRE(format_from_text("rules:\n  - name: r\n") == Format::Yaml);
    REQUIRE(format_from_text("") == Format::Json);
}

TEST_CASE("BuildFeatures reports the profile this binary was built with", "[rules][build]") {
    REQUIRE(BuildFeatures::json == (RETE_ENABLE_JSON != 0));
    REQUIRE(BuildFeatures::yaml == (RETE_ENABLE_YAML != 0));
    REQUIRE(BuildFeatures::embedded == (RETE_EMBEDDED != 0));

#if RETE_EMBEDDED
    // The embedded profile is JSON-only by construction, not by convention.
    REQUIRE_FALSE(BuildFeatures::yaml);
    REQUIRE_FALSE(BuildFeatures::exceptions);
#endif
}

#if !RETE_ENABLE_YAML
TEST_CASE("YAML on a build without it is refused, not silently retried as JSON",
          "[rules][build][unsupported]") {
    ReteEngine     engine;
    ActionRegistry registry;
    registry.register_action("go", [](ReteEngine&, const Bindings&, const ActionParams&) {});

    const std::string doc =
        "rules:\n"
        "  - name: r\n"
        "    when:\n"
        "      - fact: robot\n"
        "        attribute: state\n"
        "        value: idle\n"
        "    then:\n"
        "      - action: go\n";

    SECTION("asked for explicitly") {
        LoadResult result = load_rules_from_string(engine, registry, doc, Format::Yaml);
        REQUIRE_FALSE(result.ok());
        REQUIRE(result.errors.size() == 1u);
        REQUIRE(result.errors[0].code == ErrorCode::UnsupportedFeature);
        REQUIRE(mentions(result, "RETE_ENABLE_YAML"));
        REQUIRE(result.loaded == 0u);
        REQUIRE(engine.rule_count() == 0u);
    }

    SECTION("recognised from the text") {
        LoadResult result = load_rules_from_string(engine, registry, doc, Format::Auto);
        REQUIRE_FALSE(result.ok());
        REQUIRE(result.errors[0].code == ErrorCode::UnsupportedFeature);
        REQUIRE(mentions(result, "RETE_ENABLE_YAML"));
        REQUIRE(result.loaded == 0u);
    }
}
#endif

#if !RETE_ENABLE_JSON
TEST_CASE("JSON on a build without it is refused and names the flag",
          "[rules][build][unsupported]") {
    ReteEngine     engine;
    ActionRegistry registry;
    registry.register_action("go", [](ReteEngine&, const Bindings&, const ActionParams&) {});

    const std::string doc =
        "{\"rules\": [{\"name\": \"r\","
        " \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"idle\"}],"
        " \"then\": [{\"action\": \"go\"}]}]}";

    SECTION("asked for explicitly") {
        LoadResult result = load_rules_from_string(engine, registry, doc, Format::Json);
        REQUIRE_FALSE(result.ok());
        REQUIRE(result.errors.size() == 1u);
        REQUIRE(result.errors[0].code == ErrorCode::UnsupportedFeature);
        REQUIRE(mentions(result, "RETE_ENABLE_JSON"));
        REQUIRE(result.loaded == 0u);
        REQUIRE(engine.rule_count() == 0u);
    }

    SECTION("recognised from the text") {
        LoadResult result = load_rules_from_string(engine, registry, doc, Format::Auto);
        REQUIRE_FALSE(result.ok());
        REQUIRE(result.errors[0].code == ErrorCode::UnsupportedFeature);
        REQUIRE(mentions(result, "RETE_ENABLE_JSON"));
    }
}
#endif
