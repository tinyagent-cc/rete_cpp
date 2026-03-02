#include "catch.hpp"
#include <rete/rete.hpp>
#include <vector>

using namespace rete;

TEST_CASE("Refraction prevents re-firing on same facts", "[engine]") {
    ReteEngine engine;
    int fires = 0;

    engine.add_rule("refract-test")
        .when(std::string("?x"), std::string("on"), std::string("?y"))
        .then([&](ReteEngine&, const Bindings&) { ++fires; })
        .build();

    engine.assert_fact(std::string("A"), std::string("on"), std::string("B"));
    engine.run();
    REQUIRE(fires == 1);

    engine.run();
    REQUIRE(fires == 1);
}

TEST_CASE("max_cycles limits execution", "[engine]") {
    ReteEngine engine;
    int fires = 0;

    engine.add_rule("cycle-limit")
        .when(std::string("?x"), std::string("val"), std::string("?v"))
        .then([&](ReteEngine&, const Bindings&) { ++fires; })
        .build();

    for (int i = 0; i < 10; ++i)
        engine.assert_fact(std::string("obj" + std::to_string(i)),
                           std::string("val"),
                           std::string(std::to_string(i)));

    engine.run(3);
    REQUIRE(fires == 3);
}

TEST_CASE("halt() stops execution", "[engine]") {
    ReteEngine engine;
    int fires = 0;

    engine.add_rule("halt-test")
        .when(std::string("?x"), std::string("type"), std::string("?t"))
        .then([&](ReteEngine& e, const Bindings&) {
            ++fires;
            if (fires >= 2) e.halt();
        })
        .build();

    for (int i = 0; i < 10; ++i)
        engine.assert_fact(std::string("obj" + std::to_string(i)),
                           std::string("type"),
                           std::string("thing"));

    engine.run();
    REQUIRE(fires == 2);
}

TEST_CASE("Priority conflict strategy fires highest salience first", "[engine]") {
    ReteEngine engine;
    engine.set_conflict_strategy(ConflictStrategy::Priority);
    std::vector<std::string> order;

    engine.add_rule("low")
        .salience(1)
        .when(std::string("?x"), std::string("type"), std::string("item"))
        .then([&](ReteEngine&, const Bindings&) { order.push_back("low"); })
        .build();

    engine.add_rule("high")
        .salience(100)
        .when(std::string("?x"), std::string("type"), std::string("item"))
        .then([&](ReteEngine&, const Bindings&) { order.push_back("high"); })
        .build();

    engine.assert_fact(std::string("obj"), std::string("type"), std::string("item"));
    engine.run();

    REQUIRE(order.size() == 2);
    REQUIRE(order[0] == "high");
    REQUIRE(order[1] == "low");
}

TEST_CASE("Specificity strategy fires most conditions first", "[engine]") {
    ReteEngine engine;
    engine.set_conflict_strategy(ConflictStrategy::Specificity);
    std::vector<std::string> order;

    engine.add_rule("general")
        .when(std::string("?x"), std::string("type"), std::string("item"))
        .then([&](ReteEngine&, const Bindings&) { order.push_back("general"); })
        .build();

    engine.add_rule("specific")
        .when(std::string("?x"), std::string("type"), std::string("item"))
        .when(std::string("?x"), std::string("color"), std::string("?c"))
        .then([&](ReteEngine&, const Bindings&) { order.push_back("specific"); })
        .build();

    engine.assert_fact(std::string("obj"), std::string("type"), std::string("item"));
    engine.assert_fact(std::string("obj"), std::string("color"), std::string("red"));
    engine.run();

    REQUIRE(order.size() == 2);
    REQUIRE(order[0] == "specific");
    REQUIRE(order[1] == "general");
}

TEST_CASE("Forward chaining inference chain", "[engine]") {
    ReteEngine engine;

    engine.add_rule("has-feathers-and-flies->bird")
        .salience(10)
        .when(std::string("?x"), std::string("has"), std::string("feathers"))
        .when(std::string("?x"), std::string("can"), std::string("fly"))
        .then([](ReteEngine& e, const Bindings& b) {
            e.assert_fact(b.at("?x"), std::string("is"), std::string("bird"));
        })
        .build();

    engine.add_rule("bird-and-talks->parrot")
        .salience(5)
        .when(std::string("?x"), std::string("is"), std::string("bird"))
        .when(std::string("?x"), std::string("can"), std::string("talk"))
        .then([](ReteEngine& e, const Bindings& b) {
            e.assert_fact(b.at("?x"), std::string("is"), std::string("parrot"));
        })
        .build();

    engine.assert_fact(std::string("polly"), std::string("has"), std::string("feathers"));
    engine.assert_fact(std::string("polly"), std::string("can"), std::string("fly"));
    engine.assert_fact(std::string("polly"), std::string("can"), std::string("talk"));
    engine.run();

    auto parrots = engine.query(std::nullopt, std::string("is"), std::string("parrot"));
    REQUIRE(parrots.size() == 1);
    REQUIRE(parrots[0]->identifier == Value{std::string("polly")});
}

TEST_CASE("Retraction during execution removes derived facts", "[engine]") {
    ReteEngine engine;

    engine.add_rule("derive")
        .when(std::string("?x"), std::string("temp"), std::string("hot"))
        .then([](ReteEngine& e, const Bindings& b) {
            e.assert_fact(b.at("?x"), std::string("state"), std::string("alarm"));
        })
        .build();

    auto w = engine.assert_fact(std::string("sensor1"), std::string("temp"), std::string("hot"));
    engine.run();

    auto alarms = engine.query(std::nullopt, std::string("state"), std::string("alarm"));
    REQUIRE(alarms.size() == 1);

    engine.retract_fact(w);
    REQUIRE(engine.query(std::nullopt, std::string("temp"), std::string("hot")).empty());
}

TEST_CASE("Multiple WMEs fire rule multiple times", "[engine]") {
    ReteEngine engine;
    int fires = 0;

    engine.add_rule("count-items")
        .when(std::string("?x"), std::string("type"), std::string("item"))
        .then([&](ReteEngine&, const Bindings&) { ++fires; })
        .build();

    engine.assert_fact(std::string("a"), std::string("type"), std::string("item"));
    engine.assert_fact(std::string("b"), std::string("type"), std::string("item"));
    engine.assert_fact(std::string("c"), std::string("type"), std::string("item"));
    engine.run();

    REQUIRE(fires == 3);
}

TEST_CASE("clear_refraction allows re-firing", "[engine]") {
    ReteEngine engine;
    int fires = 0;

    engine.add_rule("re-fire")
        .when(std::string("?x"), std::string("on"), std::string("?y"))
        .then([&](ReteEngine&, const Bindings&) { ++fires; })
        .build();

    engine.assert_fact(std::string("A"), std::string("on"), std::string("B"));
    engine.run();
    REQUIRE(fires == 1);

    engine.clear_refraction();
    engine.run();
    REQUIRE(fires == 2);
}

TEST_CASE("modify_fact triggers new matching", "[engine]") {
    ReteEngine engine;
    std::vector<std::string> seen_colors;

    engine.add_rule("track-color")
        .when(std::string("?x"), std::string("color"), std::string("?c"))
        .then([&](ReteEngine&, const Bindings& b) {
            seen_colors.push_back(value_to_string(b.at("?c")));
        })
        .build();

    auto w = engine.assert_fact(std::string("obj"), std::string("color"), std::string("red"));
    engine.run();
    REQUIRE(seen_colors.size() == 1);
    REQUIRE(seen_colors[0] == "red");

    engine.modify_fact(w, std::string("obj"), std::string("color"), std::string("blue"));
    engine.run();
    REQUIRE(seen_colors.size() == 2);
    REQUIRE(seen_colors[1] == "blue");
}
