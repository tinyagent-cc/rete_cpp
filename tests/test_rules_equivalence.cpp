// ---------------------------------------------------------------------------
// The claim the whole design rests on: JSON and YAML are two spellings of one
// document, not two loaders that happen to agree on the easy cases.
//
// So this file does not smoke-test each format separately. It reads the same
// rules written both ways and compares them at three levels, each one stronger
// than the last:
//
//   1. the ConfigNode trees, node for node, because that is the boundary both
//      parsers were built to meet;
//   2. the RuleDefinition values the reader derives from them;
//   3. the Production the loader compiles, and the exact sequence of firings
//      two identically fed engines produce.
//
// A difference at any level is a difference a robot would eventually behave.
// ---------------------------------------------------------------------------

#include "catch.hpp"

#include <rete/rete.hpp>
#include <rete/rules/action_registry.hpp>
#include <rete/rules/error.hpp>
#include <rete/rules/loader.hpp>
#include <rete/rules/validator.hpp>
#include <rete/serialization/config_node.hpp>
#include <rete/serialization/json_parser.hpp>
#include <rete/serialization/yaml_parser.hpp>

#include <cstdint>
#include <string>
#include <vector>

using namespace rete;
using namespace rete::rules;

namespace {

Value str(const char* s) { return Value(std::string(s)); }
Value num(int64_t v) { return Value(v); }

// The same rule set, written twice. Key order matches on purpose: ConfigMap
// preserves insertion order, so a reordering here would be a real difference
// and the node comparison below would rightly report it.
const char* kJson =
    "{\n"
    "  \"rules\": [\n"
    "    {\n"
    "      \"name\": \"low_battery_return\",\n"
    "      \"salience\": 300,\n"
    "      \"enabled\": true,\n"
    "      \"when\": [\n"
    "        {\"fact\": \"battery\", \"attribute\": \"level\", \"op\": \"less_than\", \"value\": 20},\n"
    "        {\"fact\": \"robot\", \"attribute\": \"moving\", \"value\": true},\n"
    "        {\"fact\": \"gripper\", \"attribute\": \"holding\", \"op\": \"bind\", \"bind\": \"?block\"}\n"
    "      ],\n"
    "      \"then\": [\n"
    "        {\"action\": \"return_to_base\", \"params\": {\"speed\": 0.4, \"retries\": 3, \"mode\": \"cautious\", \"announce\": true}},\n"
    "        {\"action\": \"log\"}\n"
    "      ]\n"
    "    },\n"
    "    {\n"
    "      \"name\": \"idle_report\",\n"
    "      \"salience\": 10,\n"
    "      \"enabled\": true,\n"
    "      \"when\": [\n"
    "        {\"fact\": \"robot\", \"attribute\": \"state\", \"value\": \"idle\"}\n"
    "      ],\n"
    "      \"then\": [\n"
    "        {\"action\": \"log\"}\n"
    "      ]\n"
    "    }\n"
    "  ]\n"
    "}\n";

const char* kYaml =
    "rules:\n"
    "  - name: low_battery_return\n"
    "    salience: 300\n"
    "    enabled: true\n"
    "    when:\n"
    "      - fact: battery\n"
    "        attribute: level\n"
    "        op: less_than\n"
    "        value: 20\n"
    "      - fact: robot\n"
    "        attribute: moving\n"
    "        value: true\n"
    "      - fact: gripper\n"
    "        attribute: holding\n"
    "        op: bind\n"
    "        bind: \"?block\"\n"
    "    then:\n"
    "      - action: return_to_base\n"
    "        params:\n"
    "          speed: 0.4\n"
    "          retries: 3\n"
    "          mode: cautious\n"
    "          announce: true\n"
    "      - action: log\n"
    "  - name: idle_report\n"
    "    salience: 10\n"
    "    enabled: true\n"
    "    when:\n"
    "      - fact: robot\n"
    "        attribute: state\n"
    "        value: idle\n"
    "    then:\n"
    "      - action: log\n";

// Node-for-node comparison. `path` is threaded through so a mismatch names the
// key that differs rather than just failing somewhere in a document.
void require_same_node(const config::ConfigNode& a, const config::ConfigNode& b,
                       const std::string& path) {
    INFO("at " << (path.empty() ? "<root>" : path));
    REQUIRE(a.kind() == b.kind());

    switch (a.kind()) {
    case config::ConfigNode::Kind::Null:
        break;
    case config::ConfigNode::Kind::Scalar:
        REQUIRE(a.scalar().index() == b.scalar().index());
        REQUIRE(a.scalar() == b.scalar());
        break;
    case config::ConfigNode::Kind::Sequence:
        REQUIRE(a.seq().size() == b.seq().size());
        for (std::size_t i = 0; i < a.seq().size(); ++i)
            require_same_node(a.seq()[i], b.seq()[i], path + "[" + std::to_string(i) + "]");
        break;
    case config::ConfigNode::Kind::Map:
        REQUIRE(a.map().size() == b.map().size());
        for (std::size_t i = 0; i < a.map().size(); ++i) {
            REQUIRE(a.map()[i].first == b.map()[i].first);
            require_same_node(a.map()[i].second, b.map()[i].second,
                              path.empty() ? a.map()[i].first : path + "." + a.map()[i].first);
        }
        break;
    }
}

void require_same_definition(const RuleDefinition& a, const RuleDefinition& b) {
    INFO("rule " << a.name);
    REQUIRE(a.name == b.name);
    REQUIRE(a.salience == b.salience);
    REQUIRE(a.enabled == b.enabled);

    REQUIRE(a.conditions.size() == b.conditions.size());
    for (std::size_t i = 0; i < a.conditions.size(); ++i) {
        INFO("when[" << i << "]");
        const ConditionDefinition& ca = a.conditions[i];
        const ConditionDefinition& cb = b.conditions[i];
        REQUIRE(ca.fact == cb.fact);
        REQUIRE(ca.attribute == cb.attribute);
        REQUIRE(ca.op == cb.op);
        REQUIRE(ca.bind == cb.bind);
        // Index first: a value that is 20 as an int and 20.0 as a double would
        // compare unequal anyway, but the index says which of the two happened.
        REQUIRE(ca.value.index() == cb.value.index());
        REQUIRE(ca.value == cb.value);
    }

    REQUIRE(a.actions.size() == b.actions.size());
    for (std::size_t i = 0; i < a.actions.size(); ++i) {
        INFO("then[" << i << "]");
        const ActionDefinition& aa = a.actions[i];
        const ActionDefinition& ab = b.actions[i];
        REQUIRE(aa.id == ab.id);
        REQUIRE(aa.parameters.size() == ab.parameters.size());
        for (std::size_t k = 0; k < aa.parameters.size(); ++k) {
            INFO("param " << aa.parameters[k].first);
            REQUIRE(aa.parameters[k].first == ab.parameters[k].first);
            REQUIRE(aa.parameters[k].second.index() == ab.parameters[k].second.index());
            REQUIRE(aa.parameters[k].second == ab.parameters[k].second);
        }
    }
}

void require_same_production(const Production& a, const Production& b) {
    INFO("production " << a.name);
    REQUIRE(a.name == b.name);
    REQUIRE(a.salience == b.salience);
    REQUIRE(a.conditions.size() == b.conditions.size());
    REQUIRE(a.guards.size() == b.guards.size());
    for (std::size_t i = 0; i < a.guards.size(); ++i) {
        REQUIRE(a.guards[i].variable == b.guards[i].variable);
        REQUIRE(a.guards[i].op == b.guards[i].op);
        REQUIRE(a.guards[i].operand == b.guards[i].operand);
    }
    REQUIRE(static_cast<bool>(a.action) == static_cast<bool>(b.action));
}

std::vector<RuleDefinition> read_json(const char* text) {
    config::ConfigNode     root;
    config::ParseError     err;
    std::vector<RuleError> errors;
    std::vector<RuleDefinition> defs;

    REQUIRE(config::parse_json(std::string(text), root, err));
    REQUIRE(read_rule_document(root, defs, errors));
    REQUIRE(errors.empty());
    return defs;
}

std::vector<RuleDefinition> read_yaml(const char* text) {
    config::ConfigNode     root;
    config::ParseError     err;
    std::vector<RuleError> errors;
    std::vector<RuleDefinition> defs;

    REQUIRE(config::parse_yaml(std::string(text), root, err));
    REQUIRE(read_rule_document(root, defs, errors));
    REQUIRE(errors.empty());
    return defs;
}

// One scripted run, recorded as a transcript. Two engines fed the same script
// must produce the same transcript, character for character.
std::vector<std::string> run_scripted(const char* document, Format format) {
    std::vector<std::string> transcript;

    ReteEngine     engine;
    ActionRegistry registry;

    registry.register_action("return_to_base",
        [&](ReteEngine&, const Bindings& b, const ActionParams& p) {
            std::string line = "return_to_base speed=" + std::to_string(p.get_double("speed")) +
                               " retries=" + std::to_string(p.get_int("retries")) +
                               " mode=" + p.get_string("mode") +
                               " announce=" + (p.get_bool("announce") ? "true" : "false");
            auto it = b.find("?block");
            line += " block=" + (it == b.end() ? std::string("<none>") : value_to_string(it->second));
            transcript.push_back(line);
        });
    registry.register_action("log", [&](ReteEngine&, const Bindings&, const ActionParams& p) {
        transcript.push_back("log params=" + std::to_string(p.size()));
    });

    LoadResult result = load_rules_from_string(engine, registry, std::string(document), format);
    REQUIRE(result.ok());
    REQUIRE(result.loaded == 2u);

    for (const auto& name : result.rule_names)
        transcript.push_back("loaded " + name);

    auto battery = engine.assert_fact(str("battery"), str("level"), num(80));
    engine.assert_fact(str("robot"), str("moving"), Value(true));
    engine.assert_fact(str("gripper"), str("holding"), str("B7"));
    engine.assert_fact(str("robot"), str("state"), str("idle"));

    engine.run();
    transcript.push_back("--- cycle 1");

    engine.modify_fact(battery, str("battery"), str("level"), num(12));
    engine.run();
    transcript.push_back("--- cycle 2");

    engine.run();
    transcript.push_back("--- cycle 3");

    return transcript;
}

} // namespace

TEST_CASE("Both parsers produce the same ConfigNode tree", "[equivalence]") {
    config::ConfigNode json_root, yaml_root;
    config::ParseError json_err, yaml_err;

    REQUIRE(config::parse_json(std::string(kJson), json_root, json_err));
    REQUIRE(config::parse_yaml(std::string(kYaml), yaml_root, yaml_err));

    require_same_node(json_root, yaml_root, std::string());
}

TEST_CASE("Both documents read into the same RuleDefinitions", "[equivalence]") {
    const std::vector<RuleDefinition> from_json = read_json(kJson);
    const std::vector<RuleDefinition> from_yaml = read_yaml(kYaml);

    REQUIRE(from_json.size() == 2u);
    REQUIRE(from_json.size() == from_yaml.size());

    for (std::size_t i = 0; i < from_json.size(); ++i)
        require_same_definition(from_json[i], from_yaml[i]);
}

TEST_CASE("Both documents compile to the same Productions", "[equivalence]") {
    ActionRegistry registry;
    registry.register_action("return_to_base", [](ReteEngine&, const Bindings&, const ActionParams&) {});
    registry.register_action("log", [](ReteEngine&, const Bindings&, const ActionParams&) {});

    const std::vector<RuleDefinition> from_json = read_json(kJson);
    const std::vector<RuleDefinition> from_yaml = read_yaml(kYaml);
    REQUIRE(from_json.size() == from_yaml.size());

    for (std::size_t i = 0; i < from_json.size(); ++i) {
        Production a = compile_rule(from_json[i], registry);
        Production b = compile_rule(from_yaml[i], registry);
        require_same_production(a, b);
    }
}

TEST_CASE("Both documents drive an engine identically", "[equivalence]") {
    const std::vector<std::string> from_json = run_scripted(kJson, Format::Json);
    const std::vector<std::string> from_yaml = run_scripted(kYaml, Format::Yaml);

    REQUIRE(from_json.size() == from_yaml.size());
    for (std::size_t i = 0; i < from_json.size(); ++i) {
        INFO("transcript line " << i);
        REQUIRE(from_json[i] == from_yaml[i]);
    }

    // A transcript both formats produce but which says nothing would pass the
    // comparison above, so pin down what actually happened.
    REQUIRE(from_json.size() > 5u);
    REQUIRE(from_json[0] == "loaded low_battery_return");
    REQUIRE(from_json[1] == "loaded idle_report");

    int returns = 0;
    for (const auto& line : from_json)
        if (line.compare(0, 15, "return_to_base ") == 0) ++returns;
    REQUIRE(returns == 1);
}

TEST_CASE("Both formats reject the same rule for the same reason", "[equivalence][error]") {
    // Equivalence has to hold on the failure path too, or a rule file that is
    // legal in one format quietly becomes a different rule in the other.
    const char* json_bad =
        "{\"rules\": [{\"name\": \"bad\","
        " \"when\": [{\"fact\": \"robot\", \"attribute\": \"state\","
        "             \"op\": \"approximately\", \"value\": \"idle\"}],"
        " \"then\": [{\"action\": \"go\"}]}]}";
    const char* yaml_bad =
        "rules:\n"
        "  - name: bad\n"
        "    when:\n"
        "      - fact: robot\n"
        "        attribute: state\n"
        "        op: approximately\n"
        "        value: idle\n"
        "    then:\n"
        "      - action: go\n";

    ReteEngine     json_engine, yaml_engine;
    ActionRegistry registry;
    registry.register_action("go", [](ReteEngine&, const Bindings&, const ActionParams&) {});

    LoadResult a = load_rules_from_string(json_engine, registry, json_bad, Format::Json);
    LoadResult b = load_rules_from_string(yaml_engine, registry, yaml_bad, Format::Yaml);

    REQUIRE(a.errors.size() == b.errors.size());
    REQUIRE(a.errors.size() == 1u);
    REQUIRE(a.errors[0].code == b.errors[0].code);
    REQUIRE(a.errors[0].code == ErrorCode::UnknownOperator);
    REQUIRE(a.errors[0].rule == b.errors[0].rule);
    REQUIRE(a.errors[0].field == b.errors[0].field);
    REQUIRE(a.errors[0].message == b.errors[0].message);
    REQUIRE(a.loaded == b.loaded);
    REQUIRE(a.loaded == 0u);
}
