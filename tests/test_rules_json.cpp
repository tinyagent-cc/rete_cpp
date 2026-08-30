#include "catch.hpp"

#include <rete/rete.hpp>
#include <rete/rules/action_registry.hpp>
#include <rete/rules/error.hpp>
#include <rete/rules/loader.hpp>
#include <rete/serialization/json_parser.hpp>

#include <cstdint>
#include <cstdio>
#include <string>
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

// A one-rule document with a single numeric comparison, used to walk the
// operator table without repeating twenty near-identical string literals.
std::string comparison_doc(const char* op, int64_t operand) {
    return std::string(
        "{\"rules\": [{"
        "  \"name\": \"cmp\","
        "  \"when\": [{\"fact\": \"battery\", \"attribute\": \"level\","
        "              \"op\": \"") + op + "\", \"value\": " + std::to_string(operand) + "}],"
        "  \"then\": [{\"action\": \"go\"}]"
        "}]}";
}

int fires_for_comparison(const char* op, int64_t operand, int64_t fact_value) {
    ReteEngine     engine;
    ActionRegistry registry;
    int fires = 0;
    registry.register_action("go", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

    LoadResult result = load_rules_from_string(engine, registry, comparison_doc(op, operand),
                                               Format::Json);
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

TEST_CASE("JSON text becomes a rule that fires", "[json][rules]") {
    ReteEngine     engine;
    ActionRegistry registry;
    int fires = 0;
    registry.register_action("return_to_base",
        [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

    const std::string doc =
        "{\n"
        "  \"rules\": [{\n"
        "    \"name\": \"low_battery_return\",\n"
        "    \"salience\": 300,\n"
        "    \"when\": [\n"
        "      {\"fact\": \"battery\", \"attribute\": \"level\", \"op\": \"less_than\", \"value\": 20},\n"
        "      {\"fact\": \"robot\", \"attribute\": \"moving\", \"value\": true}\n"
        "    ],\n"
        "    \"then\": [{\"action\": \"return_to_base\"}]\n"
        "  }]\n"
        "}";

    LoadResult result = load_rules_from_string(engine, registry, doc, Format::Json);

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

TEST_CASE("Auto format recognises a JSON document from its first character", "[json][rules]") {
    ReteEngine     engine;
    ActionRegistry registry;
    int fires = 0;
    registry.register_action("go", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

    const std::string doc =
        "  {\"rules\": [{\"name\": \"r\","
        " \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"idle\"}],"
        " \"then\": [{\"action\": \"go\"}]}]}";

    REQUIRE(load_rules_from_string(engine, registry, doc, Format::Auto).ok());
    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();
    REQUIRE(fires == 1);
}

TEST_CASE("All three document shapes load", "[json][rules]") {
    const std::string rule_body =
        "{\"name\": \"r\","
        " \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"idle\"}],"
        " \"then\": [{\"action\": \"go\"}]}";

    const std::string keyed = "{\"rules\": [" + rule_body + "]}";
    const std::string bare  = "[" + rule_body + "]";
    const std::string one   = rule_body;

    for (const std::string& doc : {keyed, bare, one}) {
        INFO("document: " << doc);
        ReteEngine     engine;
        ActionRegistry registry;
        int fires = 0;
        registry.register_action("go", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

        LoadResult result = load_rules_from_string(engine, registry, doc, Format::Json);
        REQUIRE(result.ok());
        REQUIRE(result.loaded == 1u);

        engine.assert_fact(str("robot"), str("state"), str("idle"));
        engine.run();
        REQUIRE(fires == 1);
    }
}

TEST_CASE("Several rules in one document all load and fire", "[json][rules]") {
    ReteEngine     engine;
    ActionRegistry registry;
    int stops = 0, beeps = 0, reports = 0;
    registry.register_action("stop",   [&](ReteEngine&, const Bindings&, const ActionParams&) { ++stops; });
    registry.register_action("beep",   [&](ReteEngine&, const Bindings&, const ActionParams&) { ++beeps; });
    registry.register_action("report", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++reports; });

    const std::string doc =
        "{\"rules\": ["
        " {\"name\": \"a\", \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"idle\"}],"
        "  \"then\": [{\"action\": \"stop\"}]},"
        " {\"name\": \"b\", \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"idle\"}],"
        "  \"then\": [{\"action\": \"beep\"}]},"
        " {\"name\": \"c\", \"when\": [{\"fact\": \"gripper\", \"attribute\": \"holding\", \"value\": \"B1\"}],"
        "  \"then\": [{\"action\": \"report\"}]}"
        "]}";

    LoadResult result = load_rules_from_string(engine, registry, doc, Format::Json);
    REQUIRE(result.ok());
    REQUIRE(result.loaded == 3u);
    REQUIRE(engine.rule_count() == 3u);

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.assert_fact(str("gripper"), str("holding"), str("B1"));
    engine.run();

    REQUIRE(stops == 1);
    REQUIRE(beeps == 1);
    REQUIRE(reports == 1);
}

TEST_CASE("Salience in a JSON document decides fire order", "[json][rules][salience]") {
    ReteEngine     engine;
    ActionRegistry registry;
    std::vector<std::string> order;
    registry.register_action("first",  [&](ReteEngine&, const Bindings&, const ActionParams&) { order.push_back("first"); });
    registry.register_action("second", [&](ReteEngine&, const Bindings&, const ActionParams&) { order.push_back("second"); });
    registry.register_action("third",  [&](ReteEngine&, const Bindings&, const ActionParams&) { order.push_back("third"); });

    // Written lowest first, so declaration order and salience order disagree.
    const std::string doc =
        "{\"rules\": ["
        " {\"name\": \"c\", \"salience\": -5,"
        "  \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"idle\"}],"
        "  \"then\": [{\"action\": \"third\"}]},"
        " {\"name\": \"a\", \"salience\": 300,"
        "  \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"idle\"}],"
        "  \"then\": [{\"action\": \"first\"}]},"
        " {\"name\": \"b\", \"salience\": 20,"
        "  \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"idle\"}],"
        "  \"then\": [{\"action\": \"second\"}]}"
        "]}";

    REQUIRE(load_rules_from_string(engine, registry, doc, Format::Json).loaded == 3u);

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();

    REQUIRE(order.size() == 3u);
    REQUIRE(order[0] == "first");
    REQUIRE(order[1] == "second");
    REQUIRE(order[2] == "third");
}

// ===========================================================================
// Operators, written the way a rule author writes them
// ===========================================================================

TEST_CASE("Comparison operators and their aliases behave the same", "[json][operator]") {
    struct Row { const char* op; int64_t at_10; int64_t at_20; int64_t at_30; };
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
        REQUIRE(fires_for_comparison(row.op, 20, 10) == static_cast<int>(row.at_10));
        REQUIRE(fires_for_comparison(row.op, 20, 20) == static_cast<int>(row.at_20));
        REQUIRE(fires_for_comparison(row.op, 20, 30) == static_cast<int>(row.at_30));
    }
}

TEST_CASE("equals and its aliases test a constant", "[json][operator]") {
    for (const char* op : {"equals", "eq", "=="}) {
        INFO("operator " << op);
        ReteEngine     engine;
        ActionRegistry registry;
        int fires = 0;
        registry.register_action("go", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

        const std::string doc =
            std::string("{\"rules\": [{\"name\": \"r\","
            " \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\", \"op\": \"") + op +
            "\", \"value\": \"idle\"}],"
            " \"then\": [{\"action\": \"go\"}]}]}";

        REQUIRE(load_rules_from_string(engine, registry, doc, Format::Json).ok());

        engine.assert_fact(str("robot"), str("state"), str("busy"));
        engine.run();
        REQUIRE(fires == 0);

        engine.assert_fact(str("robot"), str("state"), str("idle"));
        engine.run();
        REQUIRE(fires == 1);
    }
}

TEST_CASE("not_equals and its aliases need the fact to be present", "[json][operator]") {
    for (const char* op : {"not_equals", "ne", "!="}) {
        INFO("operator " << op);
        ReteEngine     engine;
        ActionRegistry registry;
        int fires = 0;
        registry.register_action("go", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

        const std::string doc =
            std::string("{\"rules\": [{\"name\": \"r\","
            " \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\", \"op\": \"") + op +
            "\", \"value\": \"idle\"}],"
            " \"then\": [{\"action\": \"go\"}]}]}";

        REQUIRE(load_rules_from_string(engine, registry, doc, Format::Json).ok());

        // No fact at all: a dead sensor must not look like a state change.
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

TEST_CASE("exists matches any value, not_exists matches absence", "[json][operator]") {
    ActionRegistry registry;
    int fires = 0;
    registry.register_action("go", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

    SECTION("exists") {
        ReteEngine engine;
        const std::string doc =
            "{\"rules\": [{\"name\": \"r\","
            " \"when\": [{\"fact\": \"battery\", \"attribute\": \"level\", \"op\": \"exists\"}],"
            " \"then\": [{\"action\": \"go\"}]}]}";
        REQUIRE(load_rules_from_string(engine, registry, doc, Format::Json).ok());

        engine.assert_fact(str("battery"), str("level"), num(97));
        engine.run();
        REQUIRE(fires == 1);
    }

    SECTION("not_exists blocks when the fact is there") {
        ReteEngine engine;
        const std::string doc =
            "{\"rules\": [{\"name\": \"r\", \"when\": ["
            "  {\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"idle\"},"
            "  {\"fact\": \"robot\", \"attribute\": \"fault\", \"op\": \"not_exists\", \"value\": \"overheat\"}"
            " ], \"then\": [{\"action\": \"go\"}]}]}";
        REQUIRE(load_rules_from_string(engine, registry, doc, Format::Json).ok());

        engine.assert_fact(str("robot"), str("state"), str("idle"));
        engine.assert_fact(str("robot"), str("fault"), str("overheat"));
        engine.run();
        REQUIRE(fires == 0);
    }

    SECTION("not_exists passes when the fact is missing") {
        ReteEngine engine;
        const std::string doc =
            "{\"rules\": [{\"name\": \"r\", \"when\": ["
            "  {\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"idle\"},"
            "  {\"fact\": \"robot\", \"attribute\": \"fault\", \"op\": \"not_exists\", \"value\": \"overheat\"}"
            " ], \"then\": [{\"action\": \"go\"}]}]}";
        REQUIRE(load_rules_from_string(engine, registry, doc, Format::Json).ok());

        engine.assert_fact(str("robot"), str("state"), str("idle"));
        engine.run();
        REQUIRE(fires == 1);
    }
}

TEST_CASE("bind and a variable value are the same thing", "[json][operator]") {
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
            ? "{\"fact\": \"battery\", \"attribute\": \"level\", \"op\": \"bind\", \"bind\": \"?level\"}"
            : "{\"fact\": \"battery\", \"attribute\": \"level\", \"value\": \"?level\"}";

        const std::string doc =
            "{\"rules\": [{\"name\": \"r\", \"when\": [" + condition +
            "], \"then\": [{\"action\": \"go\"}]}]}";

        REQUIRE(load_rules_from_string(engine, registry, doc, Format::Json).ok());

        engine.assert_fact(str("battery"), str("level"), num(55));
        engine.run();
        REQUIRE(bound == "55");
    }
}

// ===========================================================================
// Guards
// ===========================================================================

TEST_CASE("A guard rule loaded from JSON waits for the value to arrive", "[json][guard]") {
    ReteEngine     engine;
    ActionRegistry registry;
    int fires = 0;
    registry.register_action("return_to_base",
        [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

    const std::string doc =
        "{\"rules\": [{\"name\": \"low_battery_return\", \"when\": ["
        "  {\"fact\": \"battery\", \"attribute\": \"level\", \"op\": \"less_than\", \"value\": 20}"
        " ], \"then\": [{\"action\": \"return_to_base\"}]}]}";

    REQUIRE(load_rules_from_string(engine, registry, doc, Format::Json).ok());

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

TEST_CASE("JSON parameter types survive into the handler", "[json][action]") {
    ReteEngine     engine;
    ActionRegistry registry;

    std::string mode;
    int64_t     retries = 0;
    double      speed   = 0.0;
    bool        announce = false;
    std::size_t count   = 0;

    registry.register_action("drive", [&](ReteEngine&, const Bindings&, const ActionParams& p) {
        mode     = p.get_string("mode");
        retries  = p.get_int("retries");
        speed    = p.get_double("speed");
        announce = p.get_bool("announce");
        count    = p.size();
    });

    const std::string doc =
        "{\"rules\": [{\"name\": \"r\","
        " \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"idle\"}],"
        " \"then\": [{\"action\": \"drive\", \"params\": {"
        "   \"mode\": \"cautious\", \"retries\": 3, \"speed\": 0.4, \"announce\": true}}]}]}";

    REQUIRE(load_rules_from_string(engine, registry, doc, Format::Json).ok());

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();

    REQUIRE(mode == "cautious");
    REQUIRE(retries == 3);
    REQUIRE(speed == Approx(0.4));
    REQUIRE(announce);
    REQUIRE(count == 4u);
}

TEST_CASE("Parameters outlive the document they came from", "[json][action]") {
    // "Parse once, then release" is only safe if nothing in the engine points
    // back at the text. Destroying the source before the rule ever fires is the
    // test of that claim.
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
            "{\"rules\": [{\"name\": \"r\","
            " \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"idle\"}],"
            " \"then\": [{\"action\": \"drive\", \"params\": {\"mode\": \"cautious\", \"speed\": 0.4}}]}]}");

        REQUIRE(load_rules_from_string(engine, registry, *doc, Format::Json).ok());

        // Scribble over the buffer before freeing it, so a dangling reference
        // shows up as wrong data rather than as luck.
        doc->assign(doc->size(), 'x');
        delete doc;
    }

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();

    REQUIRE(mode == "cautious");
    REQUIRE(speed == Approx(0.4));
}

TEST_CASE("Actions in a then list run in the order written", "[json][action]") {
    ReteEngine     engine;
    ActionRegistry registry;
    std::vector<std::string> order;
    registry.register_action("stop",   [&](ReteEngine&, const Bindings&, const ActionParams&) { order.push_back("stop"); });
    registry.register_action("beep",   [&](ReteEngine&, const Bindings&, const ActionParams&) { order.push_back("beep"); });
    registry.register_action("report", [&](ReteEngine&, const Bindings&, const ActionParams&) { order.push_back("report"); });

    const std::string doc =
        "{\"rules\": [{\"name\": \"r\","
        " \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"idle\"}],"
        " \"then\": [{\"action\": \"stop\"}, {\"action\": \"beep\"}, {\"action\": \"report\"}]}]}";

    REQUIRE(load_rules_from_string(engine, registry, doc, Format::Json).ok());

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();

    REQUIRE(order.size() == 3u);
    REQUIRE(order[0] == "stop");
    REQUIRE(order[1] == "beep");
    REQUIRE(order[2] == "report");
}

TEST_CASE("Bindings from the document reach the handler", "[json][action]") {
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
        "{\"rules\": [{\"name\": \"r\", \"when\": ["
        "  {\"fact\": \"battery\", \"attribute\": \"level\", \"value\": \"?level\"},"
        "  {\"fact\": \"gripper\", \"attribute\": \"holding\", \"value\": \"?block\"}"
        " ], \"then\": [{\"action\": \"record\"}]}]}";

    REQUIRE(load_rules_from_string(engine, registry, doc, Format::Json).ok());

    engine.assert_fact(str("battery"), str("level"), num(31));
    engine.assert_fact(str("gripper"), str("holding"), str("B4"));
    engine.run();

    REQUIRE(level == "31");
    REQUIRE(block == "B4");
}

// ===========================================================================
// Errors
// ===========================================================================

namespace {

// Every error document below sandwiches the broken rule between two healthy
// ones, so each case checks the same second thing: the neighbours still load.
std::string sandwich(const std::string& broken_rule) {
    return "{\"rules\": ["
           " {\"name\": \"before\","
           "  \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"idle\"}],"
           "  \"then\": [{\"action\": \"go\"}]},"
           + broken_rule +
           ", {\"name\": \"after\","
           "  \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"busy\"}],"
           "  \"then\": [{\"action\": \"go\"}]}"
           "]}";
}

LoadResult load_sandwich(ReteEngine& engine, ActionRegistry& registry,
                         const std::string& broken_rule) {
    return load_rules_from_string(engine, registry, sandwich(broken_rule), Format::Json);
}

} // namespace

TEST_CASE("Document errors are specific and cost only their own rule", "[json][error]") {
    struct Case {
        const char* what;
        const char* rule;
        ErrorCode   code;
    };

    const Case cases[] = {
        {"an action nobody registered",
         "{\"name\": \"bad\","
         " \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"idle\"}],"
         " \"then\": [{\"action\": \"launch_missiles\"}]}",
         ErrorCode::UnknownAction},

        {"a value that is not a single value",
         "{\"name\": \"bad\","
         " \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\", \"value\": {\"a\": 1}}],"
         " \"then\": [{\"action\": \"go\"}]}",
         ErrorCode::InvalidValueType},

        {"a salience that is not a number",
         "{\"name\": \"bad\", \"salience\": \"high\","
         " \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"idle\"}],"
         " \"then\": [{\"action\": \"go\"}]}",
         ErrorCode::InvalidSalience},

        {"an operator nobody implements",
         "{\"name\": \"bad\","
         " \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\","
         "             \"op\": \"approximately\", \"value\": \"idle\"}],"
         " \"then\": [{\"action\": \"go\"}]}",
         ErrorCode::UnknownOperator},

        {"an empty when",
         "{\"name\": \"bad\", \"when\": [], \"then\": [{\"action\": \"go\"}]}",
         ErrorCode::EmptyConditions},

        {"an empty then",
         "{\"name\": \"bad\","
         " \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"idle\"}],"
         " \"then\": []}",
         ErrorCode::MissingField},

        {"a rule that is not a map",
         "\"just a string\"",
         ErrorCode::NotAMap},

        {"a condition missing its attribute",
         "{\"name\": \"bad\","
         " \"when\": [{\"fact\": \"robot\", \"value\": \"idle\"}],"
         " \"then\": [{\"action\": \"go\"}]}",
         ErrorCode::MissingField},

        {"a name that is not a string",
         "{\"name\": 7,"
         " \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"idle\"}],"
         " \"then\": [{\"action\": \"go\"}]}",
         ErrorCode::InvalidValueType},

        {"a when that is not a list",
         "{\"name\": \"bad\","
         " \"when\": {\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"idle\"},"
         " \"then\": [{\"action\": \"go\"}]}",
         ErrorCode::NotAMap},

        {"params that are not a map",
         "{\"name\": \"bad\","
         " \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"idle\"}],"
         " \"then\": [{\"action\": \"go\", \"params\": [1, 2]}]}",
         ErrorCode::NotAMap},
    };

    for (const auto& c : cases) {
        INFO("case: " << c.what);
        ReteEngine     engine;
        ActionRegistry registry;
        int fires = 0;
        registry.register_action("go", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

        LoadResult result = load_sandwich(engine, registry, c.rule);

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

TEST_CASE("Malformed JSON is a located parse failure and loads nothing", "[json][error]") {
    struct Case { const char* what; const char* text; };
    const Case cases[] = {
        {"an unterminated array", "{\"rules\": ["},
        {"a trailing comma",      "{\"rules\": [],}"},
        {"an unquoted key",       "{rules: []}"},
        {"single quotes",         "{'rules': []}"},
        {"trailing content",      "{\"rules\": []} garbage"},
        {"a duplicate key",       "{\"rules\": [], \"rules\": []}"},
    };

    for (const auto& c : cases) {
        INFO("case: " << c.what);
        ReteEngine     engine;
        ActionRegistry registry;
        registry.register_action("go", [](ReteEngine&, const Bindings&, const ActionParams&) {});

        LoadResult result = load_rules_from_string(engine, registry, c.text, Format::Json);

        REQUIRE_FALSE(result.ok());
        REQUIRE(result.errors.size() == 1u);
        REQUIRE(result.errors[0].code == ErrorCode::ParseFailure);
        // A document that could not be read has no rules to salvage, so the
        // engine must be left exactly as it was.
        REQUIRE(result.loaded == 0u);
        REQUIRE(engine.rule_count() == 0u);
    }
}

TEST_CASE("A parse failure reports a line", "[json][error]") {
    ReteEngine     engine;
    ActionRegistry registry;

    const std::string doc =
        "{\n"
        "  \"rules\": [\n"
        "    {\"name\": \"r\", \"when\": [],}\n"
        "  ]\n"
        "}\n";

    LoadResult result = load_rules_from_string(engine, registry, doc, Format::Json);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.errors[0].code == ErrorCode::ParseFailure);
    REQUIRE(result.errors[0].line == 3u);
}

TEST_CASE("A document that is neither a rule nor a rule list is rejected", "[json][error]") {
    ReteEngine     engine;
    ActionRegistry registry;

    SECTION("a map with no rules key") {
        LoadResult result = load_rules_from_string(engine, registry, "{\"robots\": []}", Format::Json);
        REQUIRE(has_code(result, ErrorCode::MissingField));
    }

    SECTION("rules is not a list") {
        LoadResult result = load_rules_from_string(engine, registry, "{\"rules\": 3}", Format::Json);
        REQUIRE(has_code(result, ErrorCode::NotAMap));
    }

    SECTION("the root is a scalar") {
        LoadResult result = load_rules_from_string(engine, registry, "[42]", Format::Json);
        REQUIRE(has_code(result, ErrorCode::NotAMap));
    }
}

TEST_CASE("Duplicate rule names in one document", "[json][error]") {
    ReteEngine     engine;
    ActionRegistry registry;
    registry.register_action("go", [](ReteEngine&, const Bindings&, const ActionParams&) {});

    const std::string doc =
        "{\"rules\": ["
        " {\"name\": \"twin\", \"when\": [{\"fact\": \"a\", \"attribute\": \"b\", \"value\": \"c\"}],"
        "  \"then\": [{\"action\": \"go\"}]},"
        " {\"name\": \"twin\", \"when\": [{\"fact\": \"a\", \"attribute\": \"b\", \"value\": \"d\"}],"
        "  \"then\": [{\"action\": \"go\"}]},"
        " {\"name\": \"other\", \"when\": [{\"fact\": \"a\", \"attribute\": \"b\", \"value\": \"e\"}],"
        "  \"then\": [{\"action\": \"go\"}]}"
        "]}";

    LoadResult result = load_rules_from_string(engine, registry, doc, Format::Json);

    REQUIRE(has_code(result, ErrorCode::DuplicateRuleName));
    REQUIRE(result.loaded == 2u);
    REQUIRE(engine.has_rule("twin"));
    REQUIRE(engine.has_rule("other"));
}

TEST_CASE("A rule name the engine already runs", "[json][error]") {
    ReteEngine     engine;
    ActionRegistry registry;
    registry.register_action("go", [](ReteEngine&, const Bindings&, const ActionParams&) {});

    const std::string doc =
        "{\"rules\": [{\"name\": \"behaviour\","
        " \"when\": [{\"fact\": \"a\", \"attribute\": \"b\", \"value\": \"c\"}],"
        " \"then\": [{\"action\": \"go\"}]}]}";

    REQUIRE(load_rules_from_string(engine, registry, doc, Format::Json).ok());

    LoadResult again = load_rules_from_string(engine, registry, doc, Format::Json);
    REQUIRE(has_code(again, ErrorCode::DuplicateRuleName));
    REQUIRE(mentions(again, "replace_existing"));
    REQUIRE(again.loaded == 0u);
    REQUIRE(engine.rule_count() == 1u);
}

// ===========================================================================
// enabled and replacement
// ===========================================================================

TEST_CASE("enabled false in a document parses but does not reach the engine", "[json][enabled]") {
    ReteEngine     engine;
    ActionRegistry registry;
    int parked_fires = 0, live_fires = 0;
    registry.register_action("parked", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++parked_fires; });
    registry.register_action("live",   [&](ReteEngine&, const Bindings&, const ActionParams&) { ++live_fires; });

    const std::string doc =
        "{\"rules\": ["
        " {\"name\": \"parked\", \"enabled\": false,"
        "  \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"idle\"}],"
        "  \"then\": [{\"action\": \"parked\"}]},"
        " {\"name\": \"live\", \"enabled\": true,"
        "  \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"idle\"}],"
        "  \"then\": [{\"action\": \"live\"}]}"
        "]}";

    RuleLoader loader(engine, registry);
    LoadResult result = loader.load_from_string(doc, Format::Json);

    REQUIRE(result.ok());
    REQUIRE(result.loaded == 1u);
    REQUIRE(loader.disabled_rules().size() == 1u);
    REQUIRE(loader.disabled_rules()[0] == "parked");
    REQUIRE_FALSE(engine.has_rule("parked"));
    REQUIRE(engine.has_rule("live"));

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();

    REQUIRE(parked_fires == 0);
    REQUIRE(live_fires == 1);
}

TEST_CASE("enabled must be a boolean", "[json][enabled][error]") {
    ReteEngine     engine;
    ActionRegistry registry;
    registry.register_action("go", [](ReteEngine&, const Bindings&, const ActionParams&) {});

    LoadResult result = load_sandwich(engine, registry,
        "{\"name\": \"bad\", \"enabled\": \"no\","
        " \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"idle\"}],"
        " \"then\": [{\"action\": \"go\"}]}");

    REQUIRE(has_code(result, ErrorCode::InvalidValueType));
    REQUIRE(result.loaded == 2u);
}

TEST_CASE("replace_existing swaps a rule loaded from a document", "[json][replace]") {
    ReteEngine     engine;
    ActionRegistry registry;
    int old_fires = 0, new_fires = 0;
    registry.register_action("old", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++old_fires; });
    registry.register_action("new", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++new_fires; });

    const std::string first =
        "{\"rules\": [{\"name\": \"behaviour\","
        " \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"idle\"}],"
        " \"then\": [{\"action\": \"old\"}]}]}";
    const std::string second =
        "{\"rules\": [{\"name\": \"behaviour\","
        " \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"idle\"}],"
        " \"then\": [{\"action\": \"new\"}]}]}";

    REQUIRE(load_rules_from_string(engine, registry, first, Format::Json).ok());

    LoadOptions opts;
    opts.replace_existing = true;
    LoadResult result = load_rules_from_string(engine, registry, second, Format::Json, opts);

    REQUIRE(result.ok());
    REQUIRE(result.loaded == 1u);
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

// ctest runs each test in the build tree, so a relative name is a writable
// scratch path without pulling in <filesystem> or a POSIX temp API.
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
    return std::string("{\"rules\": [{\"name\": \"") + name + "\","
           " \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"" + state + "\"}],"
           " \"then\": [{\"action\": \"" + action + "\"}]}]}";
}

} // namespace

TEST_CASE("A rule file loads and its format comes from the extension", "[json][file]") {
    ReteEngine     engine;
    ActionRegistry registry;
    int fires = 0;
    registry.register_action("go", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++fires; });

    TempFile file("rete_test_rules_one.json", rule_doc("from_file", "go", "idle"));

    LoadResult result = load_rules_from_file(engine, registry, file.path);
    REQUIRE(result.ok());
    REQUIRE(result.loaded == 1u);

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.run();
    REQUIRE(fires == 1);
}

TEST_CASE("Several rule files load into one engine", "[json][file]") {
    ReteEngine     engine;
    ActionRegistry registry;
    int a_fires = 0, b_fires = 0;
    registry.register_action("a", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++a_fires; });
    registry.register_action("b", [&](ReteEngine&, const Bindings&, const ActionParams&) { ++b_fires; });

    TempFile first("rete_test_rules_a.json", rule_doc("alpha", "a", "idle"));
    TempFile second("rete_test_rules_b.json", rule_doc("beta", "b", "busy"));

    RuleLoader loader(engine, registry);
    LoadResult result = loader.load_from_files({first.path, second.path});

    REQUIRE(result.ok());
    REQUIRE(result.loaded == 2u);
    REQUIRE(result.rule_names.size() == 2u);
    REQUIRE(result.rule_names[0] == "alpha");
    REQUIRE(result.rule_names[1] == "beta");

    engine.assert_fact(str("robot"), str("state"), str("idle"));
    engine.assert_fact(str("robot"), str("state"), str("busy"));
    engine.run();

    REQUIRE(a_fires == 1);
    REQUIRE(b_fires == 1);
}

TEST_CASE("Several files share one rule namespace", "[json][file][error]") {
    ReteEngine     engine;
    ActionRegistry registry;
    registry.register_action("a", [](ReteEngine&, const Bindings&, const ActionParams&) {});

    TempFile first("rete_test_dup_a.json", rule_doc("shared", "a", "idle"));
    TempFile second("rete_test_dup_b.json", rule_doc("shared", "a", "busy"));

    RuleLoader loader(engine, registry);
    LoadResult result = loader.load_from_files({first.path, second.path});

    REQUIRE_FALSE(result.ok());
    REQUIRE(has_code(result, ErrorCode::DuplicateRuleName));
    REQUIRE(result.loaded == 1u);
    REQUIRE(engine.rule_count() == 1u);
}

TEST_CASE("A missing file is an error, not a silent empty load", "[json][file][error]") {
    ReteEngine     engine;
    ActionRegistry registry;

    LoadResult result = load_rules_from_file(engine, registry, "rete_test_no_such_file.json");

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.errors.size() == 1u);
    REQUIRE(result.errors[0].code == ErrorCode::FileNotFound);
    REQUIRE(result.loaded == 0u);
}

#endif // !RETE_NO_IOSTREAM
