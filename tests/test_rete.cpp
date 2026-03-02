#include "catch.hpp"
#include <rete/rete.hpp>
#include <set>

using namespace rete;

TEST_CASE("Single rule compiles and matches", "[rete]") {
    ReteEngine engine;
    int fires = 0;

    engine.add_rule("test-rule")
        .when(std::string("?x"), std::string("on"), std::string("?y"))
        .then([&](ReteEngine&, const Bindings& b) {
            REQUIRE(b.count("?x"));
            REQUIRE(b.count("?y"));
            ++fires;
        })
        .build();

    engine.assert_fact(std::string("B1"), std::string("on"), std::string("B2"));
    engine.run();

    REQUIRE(fires == 1);
}

TEST_CASE("Rule does not fire without matching facts", "[rete]") {
    ReteEngine engine;
    int fires = 0;

    engine.add_rule("no-match")
        .when(std::string("?x"), std::string("color"), std::string("red"))
        .then([&](ReteEngine&, const Bindings&) { ++fires; })
        .build();

    engine.assert_fact(std::string("B1"), std::string("on"), std::string("B2"));
    engine.run();

    REQUIRE(fires == 0);
}

TEST_CASE("Multi-condition rule with variable binding", "[rete]") {
    ReteEngine engine;
    std::vector<std::string> results;

    engine.add_rule("transitive")
        .when(std::string("?x"), std::string("on"), std::string("?y"))
        .when(std::string("?y"), std::string("on"), std::string("?z"))
        .then([&](ReteEngine&, const Bindings& b) {
            results.push_back(
                value_to_string(b.at("?x")) + "-on-" +
                value_to_string(b.at("?y")) + "-on-" +
                value_to_string(b.at("?z")));
        })
        .build();

    engine.assert_fact(std::string("A"), std::string("on"), std::string("B"));
    engine.assert_fact(std::string("B"), std::string("on"), std::string("C"));
    engine.run();

    REQUIRE(results.size() == 1);
    REQUIRE(results[0] == "A-on-B-on-C");
}

TEST_CASE("Multiple rules fire independently", "[rete]") {
    ReteEngine engine;
    std::set<std::string> fired;

    engine.add_rule("rule-1")
        .when(std::string("?x"), std::string("color"), std::string("red"))
        .then([&](ReteEngine&, const Bindings&) { fired.insert("rule-1"); })
        .build();

    engine.add_rule("rule-2")
        .when(std::string("?x"), std::string("shape"), std::string("circle"))
        .then([&](ReteEngine&, const Bindings&) { fired.insert("rule-2"); })
        .build();

    engine.assert_fact(std::string("obj1"), std::string("color"), std::string("red"));
    engine.assert_fact(std::string("obj1"), std::string("shape"), std::string("circle"));
    engine.run();

    REQUIRE(fired.count("rule-1") == 1);
    REQUIRE(fired.count("rule-2") == 1);
}

TEST_CASE("Alpha memory sharing between rules", "[rete]") {
    ReteEngine engine;
    int fires_r1 = 0, fires_r2 = 0;

    engine.add_rule("r1")
        .when(std::string("?x"), std::string("on"), std::string("?y"))
        .then([&](ReteEngine&, const Bindings&) { ++fires_r1; })
        .build();

    engine.add_rule("r2")
        .when(std::string("?a"), std::string("on"), std::string("?b"))
        .when(std::string("?a"), std::string("color"), std::string("?c"))
        .then([&](ReteEngine&, const Bindings&) { ++fires_r2; })
        .build();

    engine.assert_fact(std::string("B1"), std::string("on"), std::string("B2"));
    engine.assert_fact(std::string("B1"), std::string("color"), std::string("red"));
    engine.run();

    REQUIRE(fires_r1 == 1);
    REQUIRE(fires_r2 == 1);
}

TEST_CASE("Retraction removes activations", "[rete]") {
    ReteEngine engine;
    int fires = 0;

    engine.add_rule("retract-test")
        .when(std::string("?x"), std::string("on"), std::string("?y"))
        .then([&](ReteEngine&, const Bindings&) { ++fires; })
        .build();

    auto w = engine.assert_fact(std::string("B1"), std::string("on"), std::string("B2"));
    REQUIRE(engine.activation_count() == 1);

    engine.retract_fact(w);
    REQUIRE(engine.activation_count() == 0);

    engine.run();
    REQUIRE(fires == 0);
}

TEST_CASE("Negated condition blocks when match exists", "[rete]") {
    ReteEngine engine;
    int fires = 0;

    engine.add_rule("neg-test")
        .when(std::string("?x"), std::string("on"), std::string("?y"))
        .when_not(std::string("?x"), std::string("color"), std::string("red"))
        .then([&](ReteEngine&, const Bindings&) { ++fires; })
        .build();

    engine.assert_fact(std::string("B1"), std::string("on"), std::string("B2"));
    engine.assert_fact(std::string("B1"), std::string("color"), std::string("red"));
    engine.run();

    REQUIRE(fires == 0);
}

TEST_CASE("Negated condition passes when no match", "[rete]") {
    ReteEngine engine;
    int fires = 0;

    engine.add_rule("neg-pass")
        .when(std::string("?x"), std::string("on"), std::string("?y"))
        .when_not(std::string("?x"), std::string("color"), std::string("red"))
        .then([&](ReteEngine&, const Bindings&) { ++fires; })
        .build();

    engine.assert_fact(std::string("B1"), std::string("on"), std::string("B2"));
    engine.assert_fact(std::string("B1"), std::string("color"), std::string("blue"));
    engine.run();

    REQUIRE(fires == 1);
}

TEST_CASE("Rule removal", "[rete]") {
    ReteEngine engine;
    int fires = 0;

    engine.add_rule("removable")
        .when(std::string("?x"), std::string("on"), std::string("?y"))
        .then([&](ReteEngine&, const Bindings&) { ++fires; })
        .build();

    engine.remove_rule("removable");
    engine.assert_fact(std::string("B1"), std::string("on"), std::string("B2"));
    engine.run();

    REQUIRE(fires == 0);
}

TEST_CASE("Query working memory", "[rete]") {
    ReteEngine engine;
    engine.assert_fact(std::string("B1"), std::string("on"), std::string("B2"));
    engine.assert_fact(std::string("B1"), std::string("color"), std::string("red"));
    engine.assert_fact(std::string("B2"), std::string("on"), std::string("table"));

    auto on_facts = engine.query(std::nullopt, std::string("on"), std::nullopt);
    REQUIRE(on_facts.size() == 2);

    auto b1_facts = engine.query(std::string("B1"), std::nullopt, std::nullopt);
    REQUIRE(b1_facts.size() == 2);

    auto specific = engine.query(std::string("B1"), std::string("on"), std::string("B2"));
    REQUIRE(specific.size() == 1);
}

TEST_CASE("WME count and rule count", "[rete]") {
    ReteEngine engine;
    engine.add_rule("r1")
        .when(std::string("?x"), std::string("on"), std::string("?y"))
        .then([](ReteEngine&, const Bindings&) {})
        .build();

    REQUIRE(engine.rule_count() == 1);
    REQUIRE(engine.wme_count() == 0);

    engine.assert_fact(std::string("A"), std::string("on"), std::string("B"));
    REQUIRE(engine.wme_count() == 1);
}

TEST_CASE("Facts asserted in rule actions become available", "[rete]") {
    ReteEngine engine;
    int derived_fires = 0;

    engine.add_rule("derive-bird")
        .when(std::string("?x"), std::string("has"), std::string("feathers"))
        .when(std::string("?x"), std::string("can"), std::string("fly"))
        .then([](ReteEngine& e, const Bindings& b) {
            e.assert_fact(b.at("?x"), std::string("is"), std::string("bird"));
        })
        .build();

    engine.add_rule("found-bird")
        .when(std::string("?x"), std::string("is"), std::string("bird"))
        .then([&](ReteEngine&, const Bindings& b) {
            REQUIRE(value_to_string(b.at("?x")) == "tweety");
            ++derived_fires;
        })
        .build();

    engine.assert_fact(std::string("tweety"), std::string("has"), std::string("feathers"));
    engine.assert_fact(std::string("tweety"), std::string("can"), std::string("fly"));
    engine.run();

    REQUIRE(derived_fires == 1);
    auto birds = engine.query(std::nullopt, std::string("is"), std::string("bird"));
    REQUIRE(birds.size() == 1);
}
