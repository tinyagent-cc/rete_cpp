// Blocks World Expert System
//
// Classic AI planning domain. Given an initial configuration of blocks
// on a table, the system applies rules to rearrange them toward a goal.
// Demonstrates negated conditions and working-memory modification.
//
// Initial state:       Goal state:
//   C                    A
//   B  A                 B
//  ---table---           C
//                      ---table---

#include <rete/rete.hpp>
#include <iostream>
#include <string>

using namespace rete;

int main() {
    ReteEngine engine;
    engine.set_conflict_strategy(ConflictStrategy::Priority);

    // A block is "clear" if nothing is on top of it.
    // The rule derives (X clear yes) when X is a block and no other block is on X.
    engine.add_rule("derive-clear")
        .salience(100)
        .when(std::string("?x"), std::string("isa"), std::string("block"))
        .when_not(std::string("?any"), std::string("on"), std::string("?x"))
        .then([](ReteEngine& e, const Bindings& b) {
            e.assert_fact(b.at("?x"), std::string("clear"), std::string("yes"));
        })
        .build();

    // Move: if we want X on Y, X is clear, Y is clear, and X is currently on Z
    engine.add_rule("move-block")
        .salience(50)
        .when(std::string("?x"), std::string("goal-on"), std::string("?y"))
        .when(std::string("?x"), std::string("clear"), std::string("yes"))
        .when(std::string("?y"), std::string("clear"), std::string("yes"))
        .when(std::string("?x"), std::string("on"), std::string("?z"))
        .then([](ReteEngine& e, const Bindings& b) {
            auto x = b.at("?x");
            auto y = b.at("?y");
            auto z = b.at("?z");
            std::cout << "  MOVE " << value_to_string(x)
                      << " from " << value_to_string(z)
                      << " to "   << value_to_string(y) << "\n";

            // Retract old position and clear status, assert new position
            for (auto& w : e.query(x, std::string("on"), std::nullopt))
                e.retract_fact(w);
            for (auto& w : e.query(x, std::string("clear"), std::nullopt))
                e.retract_fact(w);
            for (auto& w : e.query(y, std::string("clear"), std::nullopt))
                e.retract_fact(w);
            for (auto& w : e.query(x, std::string("goal-on"), std::nullopt))
                e.retract_fact(w);
            // Retract derived clear for Z (will be re-derived if still valid)
            for (auto& w : e.query(z, std::string("clear"), std::nullopt))
                e.retract_fact(w);

            e.assert_fact(x, std::string("on"), y);
        })
        .build();

    engine.add_rule("goal-achieved")
        .salience(10)
        .when(std::string("?x"), std::string("on"), std::string("?y"))
        .when(std::string("?x"), std::string("goal-on"), std::string("?y"))
        .then([](ReteEngine& e, const Bindings& b) {
            std::cout << "  GOAL: " << value_to_string(b.at("?x"))
                      << " is on " << value_to_string(b.at("?y")) << " (achieved)\n";
            for (auto& w : e.query(b.at("?x"), std::string("goal-on"), std::nullopt))
                e.retract_fact(w);
        })
        .build();

    // ---- Initial state -----------------------------------------------
    std::cout << "=== Blocks World Expert System ===\n\n";
    std::cout << "Initial state: C on B, B on table, A on table\n";
    std::cout << "Goal: A on B, B on C, C on table\n\n";

    engine.assert_fact(std::string("A"), std::string("isa"), std::string("block"));
    engine.assert_fact(std::string("B"), std::string("isa"), std::string("block"));
    engine.assert_fact(std::string("C"), std::string("isa"), std::string("block"));

    engine.assert_fact(std::string("C"), std::string("on"), std::string("B"));
    engine.assert_fact(std::string("B"), std::string("on"), std::string("table"));
    engine.assert_fact(std::string("A"), std::string("on"), std::string("table"));
    engine.assert_fact(std::string("table"), std::string("clear"), std::string("yes"));

    // Goals
    engine.assert_fact(std::string("A"), std::string("goal-on"), std::string("B"));
    engine.assert_fact(std::string("B"), std::string("goal-on"), std::string("C"));
    engine.assert_fact(std::string("C"), std::string("goal-on"), std::string("table"));

    std::cout << "Running planner...\n\n";
    engine.run(50);

    std::cout << "\n--- Final state ---\n";
    for (auto& w : engine.query(std::nullopt, std::string("on"), std::nullopt))
        std::cout << "  " << w->to_string() << "\n";

    return 0;
}
