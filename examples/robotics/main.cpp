// Robotics behaviour driven by a rule file
//
// The interesting part of this example is what it does not contain. There is
// no motor driver, no GPIO, no sensor library and no robotics framework, and
// the engine has no idea it is steering anything. The application publishes
// facts, rete_cpp matches them, an action id comes out, the ActionRegistry
// dispatches it, and the application decides what to do with the result.
// Everything that knows this is a robot lives in main() and in the rules file.
//
// RETE is the behavioural and reflex layer, not the motion loop. It answers
// "what should this thing be doing" at tick rate. Holding a wheel at a setpoint
// is a different job with different deadlines and belongs below this.
//
// Run it:
//   ./build/examples/robotics_behaviour
//   ./build/examples/robotics_behaviour examples/robotics/rules.yaml
//
// Then change a salience or a threshold in the rules file and run the same
// binary again.

#include <rete/rete.hpp>
#include <rete/rules/action_registry.hpp>
#include <rete/rules/loader.hpp>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#ifndef RETE_ROBOTICS_RULES_DIR
#define RETE_ROBOTICS_RULES_DIR "examples/robotics"
#endif

namespace {

using rete::Bindings;
using rete::ReteEngine;
using rete::Value;
using rete::rules::ActionHandler;
using rete::rules::ActionParams;
using rete::rules::ActionRegistry;

// ---------------------------------------------------------------------------
// The facts this robot publishes
//
// Six of them, and the engine sees nothing else. Each is a plain triple:
// identifier, attribute, value. `obstacle.front` in the rules file is the
// identifier "obstacle" and the attribute "front".
// ---------------------------------------------------------------------------

enum FactId {
    ObstacleFront = 0,
    ObstacleLeft,
    ObstacleRight,
    RobotMoving,
    PersonVisible,
    BatteryLevel,
    FactCount
};

struct Slot {
    const char*  fact;
    const char*  attribute;
    Value        value;
    rete::WmePtr wme;
};

// One sensor frame. A real robot fills this from hardware; here a script does.
struct Frame {
    const char* note;
    bool        obstacle_front;
    bool        obstacle_left;
    bool        obstacle_right;
    bool        robot_moving;
    bool        person_visible;
    int64_t     battery_level;
};

class World {
public:
    World() {
        slots_[ObstacleFront] = Slot{"obstacle", "front",   Value{false}, nullptr};
        slots_[ObstacleLeft]  = Slot{"obstacle", "left",    Value{false}, nullptr};
        slots_[ObstacleRight] = Slot{"obstacle", "right",   Value{false}, nullptr};
        slots_[RobotMoving]   = Slot{"robot",    "moving",  Value{false}, nullptr};
        slots_[PersonVisible] = Slot{"person",   "visible", Value{false}, nullptr};
        slots_[BatteryLevel]  = Slot{"battery",  "level",   Value{static_cast<int64_t>(100)}, nullptr};
    }

    // Republishes every fact, whether or not it moved. Each modify_fact
    // retracts the old WME and asserts a new one, so every tick starts from a
    // clean match set and nothing carries over from the last frame.
    void publish(ReteEngine& engine, const Frame& f) {
        slots_[ObstacleFront].value = Value{f.obstacle_front};
        slots_[ObstacleLeft].value  = Value{f.obstacle_left};
        slots_[ObstacleRight].value = Value{f.obstacle_right};
        slots_[RobotMoving].value   = Value{f.robot_moving};
        slots_[PersonVisible].value = Value{f.person_visible};
        slots_[BatteryLevel].value  = Value{f.battery_level};

        for (auto& s : slots_) {
            const Value id{std::string(s.fact)};
            const Value attr{std::string(s.attribute)};
            if (s.wme)
                engine.modify_fact(s.wme, id, attr, s.value);
            else
                s.wme = engine.assert_fact(id, attr, s.value);
        }
    }

    std::string describe() const {
        std::string out;
        for (const auto& s : slots_) {
            if (!out.empty()) out += "  ";
            out += s.fact;
            out += '.';
            out += s.attribute;
            out += '=';
            out += rete::value_to_string(s.value);
        }
        return out;
    }

private:
    Slot slots_[FactCount];
};

// ---------------------------------------------------------------------------
// Actions
//
// The rules file names these; it never carries them. An id nobody registered
// fails validation and the rule is not loaded, which is why registration
// happens before the load and not after.
// ---------------------------------------------------------------------------

// Every proposal a rule made this tick, in the order the agenda fired them,
// which under the Priority strategy is highest salience first.
std::vector<std::string> proposals;

// value_to_string renders a double through std::to_string, which pads 0.6 out
// to 0.600000. Trim it here rather than in the library: how a value prints is
// the application's business.
std::string show(const Value& v) {
    std::string s = rete::value_to_string(v);
    if (!std::holds_alternative<double>(v)) return s;
    if (s.find('.') == std::string::npos) return s;
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

std::string format_params(const ActionParams& params) {
    if (params.empty()) return {};
    std::string out = " (";
    bool first = true;
    for (const auto& kv : params.all()) {
        if (!first) out += ", ";
        first = false;
        out += kv.first + "=" + show(kv.second);
    }
    return out + ")";
}

// Only the variables the rule file named. The loader generates its own for
// guards and wildcards under a "?$" prefix, and those are plumbing.
std::string format_bindings(const Bindings& bindings) {
    std::vector<std::pair<std::string, std::string>> named;
    for (const auto& kv : bindings) {
        if (rete::rules::is_generated_variable(kv.first)) continue;
        named.emplace_back(kv.first, show(kv.second));
    }
    if (named.empty()) return {};
    std::sort(named.begin(), named.end());

    std::string out = " [";
    for (std::size_t i = 0; i < named.size(); ++i) {
        if (i) out += ", ";
        out += named[i].first + "=" + named[i].second;
    }
    return out + "]";
}

std::string pad(std::string s, std::size_t width) {
    while (s.size() < width) s += ' ';
    return s;
}

// Every handler does the same thing: say what it would do, and record that it
// was offered. Driving a wheel from in here would put hardware inside the rule
// engine's call stack, which is the coupling this example exists to avoid.
ActionHandler make_action(std::string id, std::string sentence) {
    return [id = std::move(id), sentence = std::move(sentence)](
               ReteEngine&, const Bindings& bindings, const ActionParams& params) {
        std::cout << "  proposes  " << pad(id, 16) << sentence
                  << format_params(params) << format_bindings(bindings) << "\n";
        proposals.push_back(id);
    };
}

void register_actions(ActionRegistry& registry) {
    registry.register_action("stop",           make_action("stop", "cut drive power and hold"));
    registry.register_action("move_forward",   make_action("move_forward", "drive straight ahead"));
    registry.register_action("turn_left",      make_action("turn_left", "steer left"));
    registry.register_action("turn_right",     make_action("turn_right", "steer right"));
    registry.register_action("avoid_obstacle", make_action("avoid_obstacle", "back off and look for a gap"));
    registry.register_action("return_to_base", make_action("return_to_base", "break off and head for the dock"));
    registry.register_action("follow",         make_action("follow", "track the person and keep station"));
}

// The scenario. Nine frames chosen so each behaviour gets its turn and two of
// them collide, because a collision is the only way to see salience work.
const Frame kScenario[] = {
    // note                          front   left   right  moving person battery
    {"open floor, nobody about",     false,  false, false, true,  false, 95},
    {"person steps into view",       false,  false, false, true,  true,  88},
    {"wall closes in on the left",   false,  true,  false, true,  true,  80},
    {"wall closes in on the right",  false,  false, true,  true,  true,  72},
    {"corridor narrows both sides",  false,  true,  true,  true,  false, 64},
    {"obstacle dead ahead",          true,   true,  false, true,  false, 55},
    {"battery 22, still following",  false,  false, false, true,  true,  22},
    {"battery 18, still following",  false,  false, false, true,  true,  18},
    {"parked with nothing to do",    false,  false, false, false, false, 18},
};

const char* format_name(rete::rules::Format f) {
    switch (f) {
    case rete::rules::Format::Json: return "JSON";
    case rete::rules::Format::Yaml: return "YAML";
    case rete::rules::Format::Auto: return "auto";
    }
    return "?";
}

} // namespace

int main(int argc, char** argv) {
    const std::string path =
        (argc > 1) ? argv[1] : std::string(RETE_ROBOTICS_RULES_DIR) + "/rules.json";

    ReteEngine engine;
    engine.set_conflict_strategy(rete::ConflictStrategy::Priority);

    ActionRegistry registry;
    register_actions(registry);

    rete::rules::RuleLoader loader(engine, registry);
    const rete::rules::LoadResult result = loader.load_from_file(path);

    std::cout << "rules file  " << path << "\n"
              << "format      " << format_name(rete::rules::format_from_path(path))
              << "  (this build reads "
              << (rete::BuildFeatures::json ? "JSON" : "")
              << (rete::BuildFeatures::json && rete::BuildFeatures::yaml ? " and " : "")
              << (rete::BuildFeatures::yaml ? "YAML" : "")
              << ")\n";

    // A robot that boots with zero rules loaded and says nothing about it is
    // worse than one that refuses to boot, so both failures end the run.
    if (!result.ok()) {
        std::cerr << "\nthe rules did not load\n" << result.summary() << "\n";
        return 1;
    }
    if (result.loaded == 0) {
        std::cerr << "\n" << path << " parsed but defined no rules to run\n";
        return 1;
    }

    std::cout << "loaded      " << result.loaded << " rules:";
    for (const auto& n : result.rule_names) std::cout << " " << n;
    std::cout << "\n";
    if (!loader.disabled_rules().empty()) {
        std::cout << "disabled    ";
        for (const auto& n : loader.disabled_rules()) std::cout << n << " ";
        std::cout << "\n";
    }
    std::cout << "actions     " << registry.size() << " registered\n\n";

    World world;
    int tick = 0;

    for (const Frame& frame : kScenario) {
        ++tick;
        proposals.clear();

        world.publish(engine, frame);

        // Refraction remembers which match already fired, keyed by the WMEs
        // involved. publish() gives every fact a new WME each tick, so the set
        // only ever grows; clearing it keeps a long run bounded.
        engine.clear_refraction();

        std::cout << "tick " << tick << "  " << frame.note << "\n"
                  << "  facts     " << world.describe() << "\n";

        // Every matching rule fires, in salience order. The engine arbitrates
        // by ordering; deciding what to do with the ordering is the caller's.
        engine.run();

        // First proposal wins because the agenda handed it over first. That is
        // the whole of the priority scheme: no rule knows about any other.
        if (proposals.empty())
            std::cout << "  command   none, nothing matched\n";
        else
            std::cout << "  command   " << proposals.front() << "\n";
        std::cout << "\n";
    }

    return 0;
}
