// Animal Classification Expert System
//
// Demonstrates forward-chaining inference: raw attributes about an animal
// are asserted, and the RETE engine derives increasingly specific
// classifications through a chain of rules.
//
// Knowledge base (simplified):
//   has feathers + can fly          -> bird
//   has feathers + swims            -> penguin (overrides bird)
//   bird + can talk                 -> parrot
//   has fur + eats meat             -> carnivore
//   carnivore + has stripes         -> tiger
//   carnivore + has spots           -> leopard
//   has fur + eats plants           -> herbivore
//   herbivore + has long neck       -> giraffe
//   herbivore + has stripes         -> zebra

#include <rete/rete.hpp>
#include <iostream>
#include <string>

using namespace rete;

int main() {
    ReteEngine engine;
    engine.set_conflict_strategy(ConflictStrategy::Priority);

    // ---- Classification rules (higher salience = fires first) --------

    engine.add_rule("feathers+fly->bird")
        .salience(20)
        .when(std::string("?x"), std::string("has"), std::string("feathers"))
        .when(std::string("?x"), std::string("can"), std::string("fly"))
        .then([](ReteEngine& e, const Bindings& b) {
            e.assert_fact(b.at("?x"), std::string("is"), std::string("bird"));
        })
        .build();

    engine.add_rule("feathers+swims->penguin")
        .salience(25)
        .when(std::string("?x"), std::string("has"), std::string("feathers"))
        .when(std::string("?x"), std::string("can"), std::string("swim"))
        .then([](ReteEngine& e, const Bindings& b) {
            e.assert_fact(b.at("?x"), std::string("is"), std::string("penguin"));
        })
        .build();

    engine.add_rule("bird+talks->parrot")
        .salience(15)
        .when(std::string("?x"), std::string("is"), std::string("bird"))
        .when(std::string("?x"), std::string("can"), std::string("talk"))
        .then([](ReteEngine& e, const Bindings& b) {
            e.assert_fact(b.at("?x"), std::string("is"), std::string("parrot"));
        })
        .build();

    engine.add_rule("fur+meat->carnivore")
        .salience(20)
        .when(std::string("?x"), std::string("has"), std::string("fur"))
        .when(std::string("?x"), std::string("eats"), std::string("meat"))
        .then([](ReteEngine& e, const Bindings& b) {
            e.assert_fact(b.at("?x"), std::string("is"), std::string("carnivore"));
        })
        .build();

    engine.add_rule("carnivore+stripes->tiger")
        .salience(10)
        .when(std::string("?x"), std::string("is"), std::string("carnivore"))
        .when(std::string("?x"), std::string("has"), std::string("stripes"))
        .then([](ReteEngine& e, const Bindings& b) {
            e.assert_fact(b.at("?x"), std::string("is"), std::string("tiger"));
        })
        .build();

    engine.add_rule("carnivore+spots->leopard")
        .salience(10)
        .when(std::string("?x"), std::string("is"), std::string("carnivore"))
        .when(std::string("?x"), std::string("has"), std::string("spots"))
        .then([](ReteEngine& e, const Bindings& b) {
            e.assert_fact(b.at("?x"), std::string("is"), std::string("leopard"));
        })
        .build();

    engine.add_rule("fur+plants->herbivore")
        .salience(20)
        .when(std::string("?x"), std::string("has"), std::string("fur"))
        .when(std::string("?x"), std::string("eats"), std::string("plants"))
        .then([](ReteEngine& e, const Bindings& b) {
            e.assert_fact(b.at("?x"), std::string("is"), std::string("herbivore"));
        })
        .build();

    engine.add_rule("herbivore+long-neck->giraffe")
        .salience(10)
        .when(std::string("?x"), std::string("is"), std::string("herbivore"))
        .when(std::string("?x"), std::string("has"), std::string("long-neck"))
        .then([](ReteEngine& e, const Bindings& b) {
            e.assert_fact(b.at("?x"), std::string("is"), std::string("giraffe"));
        })
        .build();

    engine.add_rule("herbivore+stripes->zebra")
        .salience(10)
        .when(std::string("?x"), std::string("is"), std::string("herbivore"))
        .when(std::string("?x"), std::string("has"), std::string("stripes"))
        .then([](ReteEngine& e, const Bindings& b) {
            e.assert_fact(b.at("?x"), std::string("is"), std::string("zebra"));
        })
        .build();

    engine.add_rule("print-classification")
        .salience(1)
        .when(std::string("?x"), std::string("is"), std::string("?class"))
        .then([](ReteEngine&, const Bindings& b) {
            std::cout << "  " << value_to_string(b.at("?x"))
                      << " is a " << value_to_string(b.at("?class")) << "\n";
        })
        .build();

    // ---- Assert facts about animals -----------------------------------

    std::cout << "=== Animal Classification Expert System ===\n\n";

    std::cout << "Facts about Polly: has feathers, can fly, can talk\n";
    engine.assert_fact(std::string("polly"), std::string("has"), std::string("feathers"));
    engine.assert_fact(std::string("polly"), std::string("can"), std::string("fly"));
    engine.assert_fact(std::string("polly"), std::string("can"), std::string("talk"));

    std::cout << "Facts about Shere-Khan: has fur, eats meat, has stripes\n";
    engine.assert_fact(std::string("shere-khan"), std::string("has"), std::string("fur"));
    engine.assert_fact(std::string("shere-khan"), std::string("eats"), std::string("meat"));
    engine.assert_fact(std::string("shere-khan"), std::string("has"), std::string("stripes"));

    std::cout << "Facts about Tux: has feathers, can swim\n";
    engine.assert_fact(std::string("tux"), std::string("has"), std::string("feathers"));
    engine.assert_fact(std::string("tux"), std::string("can"), std::string("swim"));

    std::cout << "Facts about Stretch: has fur, eats plants, has long-neck\n";
    engine.assert_fact(std::string("stretch"), std::string("has"), std::string("fur"));
    engine.assert_fact(std::string("stretch"), std::string("eats"), std::string("plants"));
    engine.assert_fact(std::string("stretch"), std::string("has"), std::string("long-neck"));

    std::cout << "\nRunning inference engine...\n\n";
    std::cout << "Derived classifications:\n";
    engine.run();

    std::cout << "\n--- Final working memory: " << engine.wme_count() << " facts ---\n";
    for (auto& w : engine.facts())
        std::cout << "  " << w->to_string() << "\n";

    return 0;
}
