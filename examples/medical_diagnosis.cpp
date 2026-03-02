// Medical Diagnosis Expert System
//
// Demonstrates salience-based conflict resolution, negated conditions,
// and multi-step forward chaining. The system takes patient symptoms
// and derives possible conditions.
//
// Rule priorities ensure that more specific diagnoses are preferred
// and that contradictory conditions are handled gracefully.

#include <rete/rete.hpp>
#include <iostream>
#include <string>

using namespace rete;

int main() {
    ReteEngine engine;
    engine.set_conflict_strategy(ConflictStrategy::Priority);

    // ---- Intermediate inference rules (high priority) -----------------

    engine.add_rule("fever+cough->respiratory-infection")
        .salience(50)
        .when(std::string("?p"), std::string("has-symptom"), std::string("fever"))
        .when(std::string("?p"), std::string("has-symptom"), std::string("cough"))
        .then([](ReteEngine& e, const Bindings& b) {
            e.assert_fact(b.at("?p"), std::string("may-have"), std::string("respiratory-infection"));
        })
        .build();

    engine.add_rule("fever+headache+stiff-neck->meningitis")
        .salience(60)
        .when(std::string("?p"), std::string("has-symptom"), std::string("fever"))
        .when(std::string("?p"), std::string("has-symptom"), std::string("headache"))
        .when(std::string("?p"), std::string("has-symptom"), std::string("stiff-neck"))
        .then([](ReteEngine& e, const Bindings& b) {
            e.assert_fact(b.at("?p"), std::string("may-have"), std::string("meningitis"));
        })
        .build();

    engine.add_rule("cough+short-breath->pneumonia")
        .salience(55)
        .when(std::string("?p"), std::string("has-symptom"), std::string("cough"))
        .when(std::string("?p"), std::string("has-symptom"), std::string("shortness-of-breath"))
        .then([](ReteEngine& e, const Bindings& b) {
            e.assert_fact(b.at("?p"), std::string("may-have"), std::string("pneumonia"));
        })
        .build();

    engine.add_rule("headache+no-fever->tension-headache")
        .salience(40)
        .when(std::string("?p"), std::string("has-symptom"), std::string("headache"))
        .when_not(std::string("?p"), std::string("has-symptom"), std::string("fever"))
        .then([](ReteEngine& e, const Bindings& b) {
            e.assert_fact(b.at("?p"), std::string("may-have"), std::string("tension-headache"));
        })
        .build();

    engine.add_rule("sneeze+runny-nose->common-cold")
        .salience(30)
        .when(std::string("?p"), std::string("has-symptom"), std::string("sneezing"))
        .when(std::string("?p"), std::string("has-symptom"), std::string("runny-nose"))
        .then([](ReteEngine& e, const Bindings& b) {
            e.assert_fact(b.at("?p"), std::string("may-have"), std::string("common-cold"));
        })
        .build();

    engine.add_rule("rash+fever->measles")
        .salience(45)
        .when(std::string("?p"), std::string("has-symptom"), std::string("rash"))
        .when(std::string("?p"), std::string("has-symptom"), std::string("fever"))
        .then([](ReteEngine& e, const Bindings& b) {
            e.assert_fact(b.at("?p"), std::string("may-have"), std::string("measles"));
        })
        .build();

    // ---- Severity rules (lower priority, run after diagnosis) ---------

    engine.add_rule("meningitis->critical")
        .salience(20)
        .when(std::string("?p"), std::string("may-have"), std::string("meningitis"))
        .then([](ReteEngine& e, const Bindings& b) {
            e.assert_fact(b.at("?p"), std::string("severity"), std::string("critical"));
        })
        .build();

    engine.add_rule("pneumonia->serious")
        .salience(20)
        .when(std::string("?p"), std::string("may-have"), std::string("pneumonia"))
        .then([](ReteEngine& e, const Bindings& b) {
            e.assert_fact(b.at("?p"), std::string("severity"), std::string("serious"));
        })
        .build();

    // ---- Reporting rule (lowest priority) ------------------------------

    engine.add_rule("report-diagnosis")
        .salience(1)
        .when(std::string("?p"), std::string("may-have"), std::string("?diagnosis"))
        .then([](ReteEngine&, const Bindings& b) {
            std::cout << "  Patient " << value_to_string(b.at("?p"))
                      << " may have: " << value_to_string(b.at("?diagnosis")) << "\n";
        })
        .build();

    engine.add_rule("report-severity")
        .salience(1)
        .when(std::string("?p"), std::string("severity"), std::string("?s"))
        .then([](ReteEngine&, const Bindings& b) {
            std::cout << "  ** " << value_to_string(b.at("?p"))
                      << " severity: " << value_to_string(b.at("?s")) << "\n";
        })
        .build();

    // ---- Patient 1: Alice (fever, cough, shortness of breath) ---------
    std::cout << "=== Medical Diagnosis Expert System ===\n\n";

    std::cout << "Patient Alice: fever, cough, shortness of breath\n";
    engine.assert_fact(std::string("alice"), std::string("has-symptom"), std::string("fever"));
    engine.assert_fact(std::string("alice"), std::string("has-symptom"), std::string("cough"));
    engine.assert_fact(std::string("alice"), std::string("has-symptom"), std::string("shortness-of-breath"));

    // ---- Patient 2: Bob (fever, headache, stiff neck) -----------------
    std::cout << "Patient Bob:   fever, headache, stiff neck\n";
    engine.assert_fact(std::string("bob"), std::string("has-symptom"), std::string("fever"));
    engine.assert_fact(std::string("bob"), std::string("has-symptom"), std::string("headache"));
    engine.assert_fact(std::string("bob"), std::string("has-symptom"), std::string("stiff-neck"));

    // ---- Patient 3: Carol (headache only -- no fever) -----------------
    std::cout << "Patient Carol: headache (no fever)\n";
    engine.assert_fact(std::string("carol"), std::string("has-symptom"), std::string("headache"));

    std::cout << "\nRunning diagnosis...\n\n";
    engine.run();

    std::cout << "\n--- Summary: " << engine.wme_count() << " total facts ---\n";
    for (auto& w : engine.query(std::nullopt, std::string("may-have"), std::nullopt))
        std::cout << "  " << w->to_string() << "\n";

    return 0;
}
