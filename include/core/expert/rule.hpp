#include <vector>
#include <string>
#include <memory>

namespace sbre_cpp
{

enum rule_state
{
    UNMATCHED = 0,
    ACTIVATED,
    FIRING,
    TERMINATED

};

template<class condition_type, class action_type>
struct rule
{

    rule_state state;
    std::string name;
    int salience;
    std::vector<std::shared_ptr<condition_type>> lhs;
    std::vector<std::shared_ptr<action_type>> rhs;

    rule(std::string const &&_name, int _salience, rule_state &&_rule_state) : name(name), salience(_salience), state(_rule_state)
    {
    }
    rule(std::string const &_name, int _salience, rule_state &_rule_state) : name(name), salience(_salience), state(_rule_state)
    {
    }
    rule(std::string const &_name) : name(name), salience(0), state(rule_state::UNMATCHED)
    {
    }
    rule() = delete;

   inline void fire()
    {
        if (state == ACTIVATED)
        {
            state = FIRING;
             for (auto &act : rhs)
                rhs.get().execute();
            state = TERMINATED;
        }
    }

};
}