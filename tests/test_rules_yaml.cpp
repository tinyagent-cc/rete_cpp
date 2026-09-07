#include "catch.hpp"

#include <rete/rete.hpp>
#include <rete/rules/action_registry.hpp>
#include <rete/rules/error.hpp>
#include <rete/rules/loader.hpp>
#include <rete/serialization/yaml_parser.hpp>

#include <cstdint>
#include <cstdio>
#include <string>
#include <variant>
#include <vector>

#if !RETE_NO_IOSTREAM
#include <fstream>
#endif

using namespace rete;
using namespace rete::rules;

namespace {

Value str(const char* s) { return Value(std::string(s)); }
Value num(int64_t v) { return Value(v); }

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

std::string comparison_doc(const char* op, int64_t operand) {
    return std::string(
        "rules:\n"
        "  - name: cmp\n"
        "    when:\n"
        "      - fact: battery\n"
        "        attribute: level\n"
        "        op: \"") + op + "\"\n" +
        "        value: " + std::to_string(operand) + "\n" +
        "    then:\n"
        "      - action: go\n";
}

int fires_for_comparison(const char* op, int64_t operand, int64_t fact_value) {
    ReteEngine     engine;
    ActionRegistry registry;
    int fires = 0;
    registry.register_action("go", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

    LoadResult result = load_rules_from_string(engine, registry, comparison_doc(op, operand),
                                               Format::Yaml);
    REQUIRE(result.ok());
    REQUIRE(result.loaded == 1u);

    engine.assert_fact(str("battery"), str("level"), num(fact_value));
    engine.run();
    return fires;
}

} // namespace

// ===========================================================================
// A document becomes a live rule
// ===========================================================================

TEST_CASE("YAML text becomes a rule that fires", "[yaml][rules]") {
    ReteEngine     engine;
    ActionRegistry registry;
    int fires = 0;
    registry.register_action("return_to_base",
        [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

    const std::string doc =
        "rules:\n"
        "  - name: low_battery_return\n"
        "    salience: 300\n"
        "    when:\n"
        "      - fact: battery\n"
        "        attribute: level\n"
        "        op: less_than\n"
        "        value: 20\n"
        "      - fact: robot\n"
        "        attribute: moving\n"
        "        value: true\n"
        "    then:\n"
        "      - action: return_to_base\n";

    LoadResult result = load_rules_from_string(engine, registry, doc, Format::Yaml);

    REQUIRE(result.ok());
    REQUIRE(result.loaded == 1u);
    REQUIRE(result.rule_names.size() == 1u);
    REQUIRE(result.rule_names[0] == "low_battery_return");
    REQUIRE(engine.has_rule("low_battery_return"));

    engine.assert_fact(str("battery"), str("level"), num(11));
    engine.assert_fact(str("robot"), str("moving"), Value(true));
    engine.run();

    REQUIRE(fires == 1);
}

TEST_CASE("Auto format sends a YAML document to the YAML reader", "[yaml][rules]") {
    ReteEngine     engine;
    ActionRegistry registry;
    int fires = 0;
    registry.register_action("go", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

    const std::string doc =
        "rules:\n"
        "  - name: r\n"
        "    when:\n"
        "      - fact: robot\n"
        "        attribute: state\n"
        "        value: idle\n"
        "    then:\n"
        "      - action: go\n";

    REQUIRE(load_rules_from_string(engine, registry, doc, Format::Auto).ok());
    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();
    REQUIRE(fires == 1);
}

TEST_CASE("All three YAML document shapes load", "[yaml][rules]") {
    const std::string keyed =
        "rules:\n"
        "  - name: r\n"
        "    when:\n"
        "      - fact: robot\n"
        "        attribute: state\n"
        "        value: idle\n"
        "    then:\n"
        "      - action: go\n";
    const std::string bare =
        "- name: r\n"
        "  when:\n"
        "    - fact: robot\n"
        "      attribute: state\n"
        "      value: idle\n"
        "  then:\n"
        "    - action: go\n";
    const std::string one =
        "name: r\n"
        "when:\n"
        "  - fact: robot\n"
        "    attribute: state\n"
        "    value: idle\n"
        "then:\n"
        "  - action: go\n";

    for (const std::string& doc : {keyed, bare, one}) {
        INFO("document:\n" << doc);
        ReteEngine     engine;
        ActionRegistry registry;
        int fires = 0;
        registry.register_action("go", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

        LoadResult result = load_rules_from_string(engine, registry, doc, Format::Yaml);
        REQUIRE(result.ok());
        REQUIRE(result.loaded == 1u);

        engine.assert_fact(str("robot"), str("state"), str("idle"));
        engine.run();
        REQUIRE(fires == 1);
    }
}

TEST_CASE("Several YAML rules in one document all load and fire", "[yaml][rules]") {
    ReteEngine     engine;
    ActionRegistry registry;
    int stops = 0, beeps = 0, reports = 0;
    registry.register_action("stop",   [&](ReteEngine&, const Bindings&, const ActionParams&) { ++stops; });
    registry.register_action("beep",   [&](ReteEngine&, const Bindings&, const ActionParams&) { ++beeps; });
    registry.register_action("report", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++reports; });

    const std::string doc =
        "rules:\n"
        "  - name: a\n"
        "    when: [{fact: robot, attribute: state, value: idle}]\n"
        "    then: [{action: stop}]\n"
        "  - name: b\n"
        "    when: [{fact: robot, attribute: state, value: idle}]\n"
        "    then: [{action: beep}]\n"
        "  - name: c\n"
        "    when: [{fact: gripper, attribute: holding, value: B1}]\n"
        "    then: [{action: report}]\n";

    LoadResult result = load_rules_from_string(engine, registry, doc, Format::Yaml);
    REQUIRE(result.ok());
    REQUIRE(result.loaded == 3u);

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.assert_fact(str("gripper"), str("holding"), str("B1"));
    engine.run();

    REQUIRE(stops == 1);
    REQUIRE(beeps == 1);
    REQUIRE(reports == 1);
}

TEST_CASE("Salience in a YAML document decides fire order", "[yaml][rules][salience]") {
    ReteEngine     engine;
    ActionRegistry registry;
    std::vector<std::string> order;
    registry.register_action("first",  [&](ReteEngine&, const Bindings&, const ActionParams&) { order.push_back("first"); });
    registry.register_action("second", [&](ReteEngine&, const Bindings&, const ActionParams&) { order.push_back("second"); });
    registry.register_action("third",  [&](ReteEngine&, const Bindings&, const ActionParams&) { order.push_back("third"); });

    const std::string doc =
        "rules:\n"
        "  - name: c\n"
        "    salience: -5\n"
        "    when: [{fact: robot, attribute: state, value: idle}]\n"
        "    then: [{action: third}]\n"
        "  - name: a\n"
        "    salience: 300\n"
        "    when: [{fact: robot, attribute: state, value: idle}]\n"
        "    then: [{action: first}]\n"
        "  - name: b\n"
        "    salience: 20\n"
        "    when: [{fact: robot, attribute: state, value: idle}]\n"
        "    then: [{action: second}]\n";

    REQUIRE(load_rules_from_string(engine, registry, doc, Format::Yaml).loaded == 3u);

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();

    REQUIRE(order.size() == 3u);
    REQUIRE(order[0] == "first");
    REQUIRE(order[1] == "second");
    REQUIRE(order[2] == "third");
}

// ===========================================================================
// Operators
// ===========================================================================

TEST_CASE("YAML comparison operators and their aliases behave the same", "[yaml][operator]") {
    struct Row { const char* op; int at_10; int at_20; int at_30; };
    const Row rows[] = {
        {"greater_than",     0, 0, 1},
        {"gt",               0, 0, 1},
        {">",                0, 0, 1},
        {"greater_or_equal", 0, 1, 1},
        {"gte",              0, 1, 1},
        {">=",               0, 1, 1},
        {"less_than",        1, 0, 0},
        {"lt",               1, 0, 0},
        {"<",                1, 0, 0},
        {"less_or_equal",    1, 1, 0},
        {"lte",              1, 1, 0},
        {"<=",               1, 1, 0},
    };

    for (const auto& row : rows) {
        INFO("operator " << row.op);
        REQUIRE(fires_for_comparison(row.op, 20, 10) == row.at_10);
        REQUIRE(fires_for_comparison(row.op, 20, 20) == row.at_20);
        REQUIRE(fires_for_comparison(row.op, 20, 30) == row.at_30);
    }
}

TEST_CASE("YAML equals and its aliases test a constant", "[yaml][operator]") {
    for (const char* op : {"equals", "eq", "=="}) {
        INFO("operator " << op);
        ReteEngine     engine;
        ActionRegistry registry;
        int fires = 0;
        registry.register_action("go", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

        const std::string doc =
            std::string("rules:\n"
            "  - name: r\n"
            "    when:\n"
            "      - fact: robot\n"
            "        attribute: state\n"
            "        op: \"") + op + "\"\n"
            "        value: idle\n"
            "    then: [{action: go}]\n";

        REQUIRE(load_rules_from_string(engine, registry, doc, Format::Yaml).ok());

        engine.assert_fact(str("robot"), str("state"), str("busy"));
        engine.run();
        REQUIRE(fires == 0);

        engine.assert_fact(str("robot"), str("state"), str("idle"));
        engine.run();
        REQUIRE(fires == 1);
    }
}

TEST_CASE("YAML not_equals and its aliases need the fact to be present", "[yaml][operator]") {
    for (const char* op : {"not_equals", "ne", "!="}) {
        INFO("operator " << op);
        ReteEngine     engine;
        ActionRegistry registry;
        int fires = 0;
        registry.register_action("go", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

        const std::string doc =
            std::string("rules:\n"
            "  - name: r\n"
            "    when:\n"
            "      - fact: robot\n"
            "        attribute: state\n"
            "        op: \"") + op + "\"\n"
            "        value: idle\n"
            "    then: [{action: go}]\n";

        REQUIRE(load_rules_from_string(engine, registry, doc, Format::Yaml).ok());

        engine.run();
        REQUIRE(fires == 0);

        auto wme = engine.assert_fact(str("robot"), str("state"), str("idle"));
        engine.run();
        REQUIRE(fires == 0);

        engine.modify_fact(wme, str("robot"), str("state"), str("charging"));
        engine.run();
        REQUIRE(fires == 1);
    }
}

TEST_CASE("YAML exists and not_exists", "[yaml][operator]") {
    ActionRegistry registry;
    int fires = 0;
    registry.register_action("go", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

    SECTION("exists matches any value") {
        ReteEngine engine;
        const std::string doc =
            "rules:\n"
            "  - name: r\n"
            "    when: [{fact: battery, attribute: level, op: exists}]\n"
            "    then: [{action: go}]\n";
        REQUIRE(load_rules_from_string(engine, registry, doc, Format::Yaml).ok());

        engine.assert_fact(str("battery"), str("level"), num(97));
        engine.run();
        REQUIRE(fires == 1);
    }

    SECTION("not_exists blocks when the fact is there") {
        ReteEngine engine;
        const std::string doc =
            "rules:\n"
            "  - name: r\n"
            "    when:\n"
            "      - {fact: robot, attribute: state, value: idle}\n"
            "      - {fact: robot, attribute: fault, op: not_exists, value: overheat}\n"
            "    then: [{action: go}]\n";
        REQUIRE(load_rules_from_string(engine, registry, doc, Format::Yaml).ok());

        engine.assert_fact(str("robot"), str("state"), str("idle"));
        engine.assert_fact(str("robot"), str("fault"), str("overheat"));
        engine.run();
        REQUIRE(fires == 0);
    }

    SECTION("not_exists passes when the fact is missing") {
        ReteEngine engine;
        const std::string doc =
            "rules:\n"
            "  - name: r\n"
            "    when:\n"
            "      - {fact: robot, attribute: state, value: idle}\n"
            "      - {fact: robot, attribute: fault, op: not_exists, value: overheat}\n"
            "    then: [{action: go}]\n";
        REQUIRE(load_rules_from_string(engine, registry, doc, Format::Yaml).ok());

        engine.assert_fact(str("robot"), str("state"), str("idle"));
        engine.run();
        REQUIRE(fires == 1);
    }
}

TEST_CASE("YAML bind and a variable value are the same thing", "[yaml][operator]") {
    for (bool use_op : {true, false}) {
        INFO((use_op ? "op: bind" : "value: \"?level\""));
        ReteEngine     engine;
        ActionRegistry registry;
        std::string bound;
        registry.register_action("go", [&](ReteEngine&, const Bindings& b, const ActionParams&) {
            auto it = b.find("?level");
            if (it != b.end()) bound = value_to_string(it->second);
        });

        const std::string condition = use_op
            ? "      - {fact: battery, attribute: level, op: bind, bind: \"?level\"}\n"
            : "      - {fact: battery, attribute: level, value: \"?level\"}\n";

        const std::string doc =
            "rules:\n"
            "  - name: r\n"
            "    when:\n" + condition +
            "    then: [{action: go}]\n";

        REQUIRE(load_rules_from_string(engine, registry, doc, Format::Yaml).ok());

        engine.assert_fact(str("battery"), str("level"), num(55));
        engine.run();
        REQUIRE(bound == "55");
    }
}

// ===========================================================================
// Guards
// ===========================================================================

TEST_CASE("A guard rule loaded from YAML waits for the value to arrive", "[yaml][guard]") {
    ReteEngine     engine;
    ActionRegistry registry;
    int fires = 0;
    registry.register_action("return_to_base",
        [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

    const std::string doc =
        "rules:\n"
        "  - name: low_battery_return\n"
        "    when:\n"
        "      - fact: battery\n"
        "        attribute: level\n"
        "        op: less_than\n"
        "        value: 20\n"
        "    then:\n"
        "      - action: return_to_base\n";

    REQUIRE(load_rules_from_string(engine, registry, doc, Format::Yaml).ok());

    auto wme = engine.assert_fact(str("battery"), str("level"), num(64));
    engine.run();
    REQUIRE(fires == 0);

    engine.modify_fact(wme, str("battery"), str("level"), num(19));
    engine.run();
    REQUIRE(fires == 1);

    engine.run();
    REQUIRE(fires == 1);
}

// ===========================================================================
// Actions and parameters
// ===========================================================================

TEST_CASE("YAML parameter types survive into the handler", "[yaml][action]") {
    ReteEngine     engine;
    ActionRegistry registry;

    std::string mode;
    int64_t     retries = 0;
    double      speed = 0.0;
    bool        announce = false;
    std::size_t count = 0;

    registry.register_action("drive", [&](ReteEngine&, const Bindings&, const ActionParams& p) {
        mode     = p.get_string("mode");
        retries  = p.get_int("retries");
        speed    = p.get_double("speed");
        announce = p.get_bool("announce");
        count    = p.size();
    });

    const std::string doc =
        "rules:\n"
        "  - name: r\n"
        "    when: [{fact: robot, attribute: state, value: idle}]\n"
        "    then:\n"
        "      - action: drive\n"
        "        params:\n"
        "          mode: cautious\n"
        "          retries: 3\n"
        "          speed: 0.4\n"
        "          announce: true\n";

    REQUIRE(load_rules_from_string(engine, registry, doc, Format::Yaml).ok());

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();

    REQUIRE(mode == "cautious");
    REQUIRE(retries == 3);
    REQUIRE(speed == Approx(0.4));
    REQUIRE(announce);
    REQUIRE(count == 4u);
}

TEST_CASE("YAML parameters outlive the document they came from", "[yaml][action]") {
    ReteEngine     engine;
    ActionRegistry registry;
    std::string mode;
    double      speed = 0.0;

    registry.register_action("drive", [&](ReteEngine&, const Bindings&, const ActionParams& p) {
        mode  = p.get_string("mode");
        speed = p.get_double("speed");
    });

    {
        std::string* doc = new std::string(
            "rules:\n"
            "  - name: r\n"
            "    when: [{fact: robot, attribute: state, value: idle}]\n"
            "    then:\n"
            "      - action: drive\n"
            "        params: {mode: cautious, speed: 0.4}\n");

        REQUIRE(load_rules_from_string(engine, registry, *doc, Format::Yaml).ok());

        doc->assign(doc->size(), 'x');
        delete doc;
    }

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();

    REQUIRE(mode == "cautious");
    REQUIRE(speed == Approx(0.4));
}

TEST_CASE("YAML actions run in the order written", "[yaml][action]") {
    ReteEngine     engine;
    ActionRegistry registry;
    std::vector<std::string> order;
    registry.register_action("stop",   [&](ReteEngine&, const Bindings&, const ActionParams&) { order.push_back("stop"); });
    registry.register_action("beep",   [&](ReteEngine&, const Bindings&, const ActionParams&) { order.push_back("beep"); });
    registry.register_action("report", [&](ReteEngine&, const Bindings&, const ActionParams&) { order.push_back("report"); });

    const std::string doc =
        "rules:\n"
        "  - name: r\n"
        "    when: [{fact: robot, attribute: state, value: idle}]\n"
        "    then:\n"
        "      - action: stop\n"
        "      - action: beep\n"
        "      - action: report\n";

    REQUIRE(load_rules_from_string(engine, registry, doc, Format::Yaml).ok());

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();

    REQUIRE(order.size() == 3u);
    REQUIRE(order[0] == "stop");
    REQUIRE(order[1] == "beep");
    REQUIRE(order[2] == "report");
}

TEST_CASE("Bindings from a YAML document reach the handler", "[yaml][action]") {
    ReteEngine     engine;
    ActionRegistry registry;
    std::string block, level;
    registry.register_action("record", [&](ReteEngine&, const Bindings& b, const ActionParams&) {
        auto l = b.find("?level");
        auto x = b.find("?block");
        if (l != b.end()) level = value_to_string(l->second);
        if (x != b.end()) block = value_to_string(x->second);
    });

    const std::string doc =
        "rules:\n"
        "  - name: r\n"
        "    when:\n"
        "      - {fact: battery, attribute: level, value: \"?level\"}\n"
        "      - {fact: gripper, attribute: holding, value: \"?block\"}\n"
        "    then: [{action: record}]\n";

    REQUIRE(load_rules_from_string(engine, registry, doc, Format::Yaml).ok());

    engine.assert_fact(str("battery"), str("level"), num(31));
    engine.assert_fact(str("gripper"), str("holding"), str("B4"));
    engine.run();

    REQUIRE(level == "31");
    REQUIRE(block == "B4");
}

// ===========================================================================
// YAML scalar typing, which is where a rule author gets surprised
// ===========================================================================

TEST_CASE("Quoting is honoured, so a quoted number stays a string", "[yaml][scalar]") {
    config::ConfigNode  root;
    config::ParseError  err;

    REQUIRE(config::parse_yaml("plain: 20\nquoted: \"20\"\nreal: 0.4\nflag: true\n", root, err));
    REQUIRE(err.ok());
    REQUIRE(root.is_map());

    const config::ConfigNode* plain  = root.find("plain");
    const config::ConfigNode* quoted = root.find("quoted");
    const config::ConfigNode* real   = root.find("real");
    const config::ConfigNode* flag   = root.find("flag");

    REQUIRE(plain != nullptr);
    REQUIRE(quoted != nullptr);
    REQUIRE(real != nullptr);
    REQUIRE(flag != nullptr);

    REQUIRE(std::holds_alternative<int64_t>(plain->scalar()));
    REQUIRE(std::holds_alternative<std::string>(quoted->scalar()));
    REQUIRE(std::holds_alternative<double>(real->scalar()));
    REQUIRE(std::holds_alternative<bool>(flag->scalar()));
}

TEST_CASE("YAML 1.1 booleans stay strings", "[yaml][scalar]") {
    // `announce: no` must not become false. A robot told to announce would
    // otherwise go quiet because of a spelling YAML 1.1 happened to like.
    ReteEngine     engine;
    ActionRegistry registry;
    std::string announce_text;
    bool        announce_bool = true;

    registry.register_action("drive", [&](ReteEngine&, const Bindings&, const ActionParams& p) {
        announce_text = p.get_string("announce");
        announce_bool = p.get_bool("announce", true);
    });

    const std::string doc =
        "rules:\n"
        "  - name: r\n"
        "    when: [{fact: robot, attribute: state, value: idle}]\n"
        "    then:\n"
        "      - action: drive\n"
        "        params: {announce: no}\n";

    REQUIRE(load_rules_from_string(engine, registry, doc, Format::Yaml).ok());

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();

    REQUIRE(announce_text == "no");
    REQUIRE(announce_bool);  // the fallback, because "no" is not a boolean
}

// ===========================================================================
// Errors
// ===========================================================================

namespace {

std::string sandwich(const std::string& broken_rule) {
    return std::string(
        "rules:\n"
        "  - name: before\n"
        "    when: [{fact: robot, attribute: state, value: idle}]\n"
        "    then: [{action: go}]\n") +
        broken_rule +
        "  - name: after\n"
        "    when: [{fact: robot, attribute: state, value: busy}]\n"
        "    then: [{action: go}]\n";
}

} // namespace

TEST_CASE("YAML document errors are specific and cost only their own rule", "[yaml][error]") {
    struct Case {
        const char* what;
        const char* rule;
        ErrorCode   code;
    };

    const Case cases[] = {
        {"an action nobody registered",
         "  - name: bad\n"
         "    when: [{fact: robot, attribute: state, value: idle}]\n"
         "    then: [{action: launch_missiles}]\n",
         ErrorCode::UnknownAction},

        {"a value that is not a single value",
         "  - name: bad\n"
         "    when: [{fact: robot, attribute: state, value: {a: 1}}]\n"
         "    then: [{action: go}]\n",
         ErrorCode::InvalidValueType},

        {"a salience that is not a number",
         "  - name: bad\n"
         "    salience: high\n"
         "    when: [{fact: robot, attribute: state, value: idle}]\n"
         "    then: [{action: go}]\n",
         ErrorCode::InvalidSalience},

        {"an operator nobody implements",
         "  - name: bad\n"
         "    when: [{fact: robot, attribute: state, op: approximately, value: idle}]\n"
         "    then: [{action: go}]\n",
         ErrorCode::UnknownOperator},

        {"an empty when",
         "  - name: bad\n"
         "    when: []\n"
         "    then: [{action: go}]\n",
         ErrorCode::EmptyConditions},

        {"an empty then",
         "  - name: bad\n"
         "    when: [{fact: robot, attribute: state, value: idle}]\n"
         "    then: []\n",
         ErrorCode::MissingField},

        {"a rule that is not a map",
         "  - just a string\n",
         ErrorCode::NotAMap},

        {"a condition missing its attribute",
         "  - name: bad\n"
         "    when: [{fact: robot, value: idle}]\n"
         "    then: [{action: go}]\n",
         ErrorCode::MissingField},

        {"a name that is not a string",
         "  - name: 7\n"
         "    when: [{fact: robot, attribute: state, value: idle}]\n"
         "    then: [{action: go}]\n",
         ErrorCode::InvalidValueType},

        {"a when that is not a list",
         "  - name: bad\n"
         "    when: {fact: robot, attribute: state, value: idle}\n"
         "    then: [{action: go}]\n",
         ErrorCode::NotAMap},

        {"params that are not a map",
         "  - name: bad\n"
         "    when: [{fact: robot, attribute: state, value: idle}]\n"
         "    then: [{action: go, params: [1, 2]}]\n",
         ErrorCode::NotAMap},

        {"enabled that is not a boolean",
         "  - name: bad\n"
         "    enabled: maybe\n"
         "    when: [{fact: robot, attribute: state, value: idle}]\n"
         "    then: [{action: go}]\n",
         ErrorCode::InvalidValueType},
    };

    for (const auto& c : cases) {
        INFO("case: " << c.what);
        ReteEngine     engine;
        ActionRegistry registry;
        int fires = 0;
        registry.register_action("go", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

        LoadResult result = load_rules_from_string(engine, registry, sandwich(c.rule), Format::Yaml);

        REQUIRE_FALSE(result.ok());
        REQUIRE(has_code(result, c.code));
        REQUIRE(result.loaded == 2u);
        REQUIRE(result.rule_names.size() == result.loaded);
        REQUIRE(engine.has_rule("before"));
        REQUIRE(engine.has_rule("after"));
        REQUIRE_FALSE(engine.has_rule("bad"));

        engine.assert_fact(str("robot"), str("state"), str("idle"));
        engine.assert_fact(str("robot"), str("state"), str("busy"));
        engine.run();
        REQUIRE(fires == 2);
    }
}

TEST_CASE("Malformed YAML is a parse failure and loads nothing", "[yaml][error]") {
    struct Case { const char* what; const char* text; };
    const Case cases[] = {
        {"an unterminated flow sequence", "rules: [{name: r}"},
        {"an unterminated flow mapping",  "rules: [{name: r]"},
        {"a mis-indented key",            "rules:\n  - name: r\n   when: []\n"},
        {"a tab used for indentation",    "rules:\n\t- name: r\n"},
        {"two documents in one stream",   "rules: []\n---\nrules: []\n"},
    };

    for (const auto& c : cases) {
        INFO("case: " << c.what);
        ReteEngine     engine;
        ActionRegistry registry;
        registry.register_action("go", [](ReteEngine&, const Bindings&, const ActionParams&) {});

        LoadResult result = load_rules_from_string(engine, registry, c.text, Format::Yaml);

        REQUIRE_FALSE(result.ok());
        REQUIRE(result.errors.size() == 1u);
        REQUIRE(result.errors[0].code == ErrorCode::ParseFailure);
        REQUIRE(result.loaded == 0u);
        REQUIRE(engine.rule_count() == 0u);
    }
}

TEST_CASE("A YAML document that is not a rule document is rejected", "[yaml][error]") {
    ReteEngine     engine;
    ActionRegistry registry;

    SECTION("a map with no rules key") {
        LoadResult result = load_rules_from_string(engine, registry, "robots: []\n", Format::Yaml);
        REQUIRE(has_code(result, ErrorCode::MissingField));
    }

    SECTION("rules is not a list") {
        LoadResult result = load_rules_from_string(engine, registry, "rules: 3\n", Format::Yaml);
        REQUIRE(has_code(result, ErrorCode::NotAMap));
    }

    SECTION("the root is a list of scalars") {
        LoadResult result = load_rules_from_string(engine, registry, "- 42\n", Format::Yaml);
        REQUIRE(has_code(result, ErrorCode::NotAMap));
    }
}

TEST_CASE("Duplicate rule names in one YAML document", "[yaml][error]") {
    ReteEngine     engine;
    ActionRegistry registry;
    registry.register_action("go", [](ReteEngine&, const Bindings&, const ActionParams&) {});

    const std::string doc =
        "rules:\n"
        "  - name: twin\n"
        "    when: [{fact: a, attribute: b, value: c}]\n"
        "    then: [{action: go}]\n"
        "  - name: twin\n"
        "    when: [{fact: a, attribute: b, value: d}]\n"
        "    then: [{action: go}]\n"
        "  - name: other\n"
        "    when: [{fact: a, attribute: b, value: e}]\n"
        "    then: [{action: go}]\n";

    LoadResult result = load_rules_from_string(engine, registry, doc, Format::Yaml);

    REQUIRE(has_code(result, ErrorCode::DuplicateRuleName));
    REQUIRE(result.loaded == 2u);
    REQUIRE(engine.has_rule("twin"));
    REQUIRE(engine.has_rule("other"));
}

TEST_CASE("A YAML rule name the engine already runs", "[yaml][error]") {
    ReteEngine     engine;
    ActionRegistry registry;
    registry.register_action("go", [](ReteEngine&, const Bindings&, const ActionParams&) {});

    const std::string doc =
        "rules:\n"
        "  - name: behaviour\n"
        "    when: [{fact: a, attribute: b, value: c}]\n"
        "    then: [{action: go}]\n";

    REQUIRE(load_rules_from_string(engine, registry, doc, Format::Yaml).ok());

    LoadResult again = load_rules_from_string(engine, registry, doc, Format::Yaml);
    REQUIRE(has_code(again, ErrorCode::DuplicateRuleName));
    REQUIRE(mentions(again, "replace_existing"));
    REQUIRE(again.loaded == 0u);
    REQUIRE(engine.rule_count() == 1u);
}

// ===========================================================================
// enabled and replacement
// ===========================================================================

TEST_CASE("enabled false in a YAML document parses but does not reach the engine", "[yaml][enabled]") {
    ReteEngine     engine;
    ActionRegistry registry;
    int parked_fires = 0, live_fires = 0;
    registry.register_action("parked", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++parked_fires; });
    registry.register_action("live",   [&](ReteEngine&, const Bindings&, const ActionParams&) { ++live_fires; });

    const std::string doc =
        "rules:\n"
        "  - name: parked\n"
        "    enabled: false\n"
        "    when: [{fact: robot, attribute: state, value: idle}]\n"
        "    then: [{action: parked}]\n"
        "  - name: live\n"
        "    when: [{fact: robot, attribute: state, value: idle}]\n"
        "    then: [{action: live}]\n";

    RuleLoader loader(engine, registry);
    LoadResult result = loader.load_from_string(doc, Format::Yaml);

    REQUIRE(result.ok());
    REQUIRE(result.loaded == 1u);
    REQUIRE(loader.disabled_rules().size() == 1u);
    REQUIRE(loader.disabled_rules()[0] == "parked");
    REQUIRE_FALSE(engine.has_rule("parked"));

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();

    REQUIRE(parked_fires == 0);
    REQUIRE(live_fires == 1);
}

TEST_CASE("replace_existing swaps a rule loaded from YAML", "[yaml][replace]") {
    ReteEngine     engine;
    ActionRegistry registry;
    int old_fires = 0, new_fires = 0;
    registry.register_action("old", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++old_fires; });
    registry.register_action("new", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++new_fires; });

    const std::string first =
        "rules:\n"
        "  - name: behaviour\n"
        "    when: [{fact: robot, attribute: state, value: idle}]\n"
        "    then: [{action: old}]\n";
    const std::string second =
        "rules:\n"
        "  - name: behaviour\n"
        "    when: [{fact: robot, attribute: state, value: idle}]\n"
        "    then: [{action: new}]\n";

    REQUIRE(load_rules_from_string(engine, registry, first, Format::Yaml).ok());

    LoadOptions opts;
    opts.replace_existing = true;
    REQUIRE(load_rules_from_string(engine, registry, second, Format::Yaml, opts).ok());
    REQUIRE(engine.rule_count() == 1u);

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();

    REQUIRE(old_fires == 0);
    REQUIRE(new_fires == 1);
}

// ===========================================================================
// Files
// ===========================================================================

#if !RETE_NO_IOSTREAM

namespace {

struct TempFile {
    std::string path;

    TempFile(const std::string& name, const std::string& contents) : path(name) {
        std::ofstream out(path, std::ios::binary);
        out << contents;
    }
    ~TempFile() { std::remove(path.c_str()); }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
};

std::string rule_doc(const char* name, const char* action, const char* state) {
    return std::string("rules:\n  - name: ") + name + "\n"
           "    when: [{fact: robot, attribute: state, value: " + state + "}]\n"
           "    then: [{action: " + action + "}]\n";
}

} // namespace

TEST_CASE("A .yaml file is read as YAML because of its extension", "[yaml][file]") {
    ReteEngine     engine;
    ActionRegistry registry;
    int fires = 0;
    registry.register_action("go", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

    TempFile file("rete_test_rules_one.yaml", rule_doc("from_file", "go", "idle"));

    LoadResult result = load_rules_from_file(engine, registry, file.path);
    REQUIRE(result.ok());
    REQUIRE(result.loaded == 1u);

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();
    REQUIRE(fires == 1);
}

TEST_CASE("A YAML file and a JSON file load into one engine", "[yaml][file]") {
    ReteEngine     engine;
    ActionRegistry registry;
    int a_fires = 0, b_fires = 0;
    registry.register_action("a", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++a_fires; });
    registry.register_action("b", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++b_fires; });

    TempFile first("rete_test_mixed_a.yml", rule_doc("alpha", "a", "idle"));
    TempFile second("rete_test_mixed_b.json",
        "{\"rules\": [{\"name\": \"beta\","
        " \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"busy\"}],"
        " \"then\": [{\"action\": \"b\"}]}]}");

    RuleLoader loader(engine, registry);
    LoadResult result = loader.load_from_files({first.path, second.path});

    REQUIRE(result.ok());
    REQUIRE(result.loaded == 2u);
    REQUIRE(result.rule_names[0] == "alpha");
    REQUIRE(result.rule_names[1] == "beta");

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.assert_fact(str("robot"), str("state"), str("busy"));
    engine.run();

    REQUIRE(a_fires == 1);
    REQUIRE(b_fires == 1);
}

#endif // !RETE_NO_IOSTREAM
