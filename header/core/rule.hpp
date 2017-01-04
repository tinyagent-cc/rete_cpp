#include <vector>
#include <string>
#include <memory>

namespace rete_cpp
{

class fact;
class condition;
class action;

class rule
{
    enum rule_state
    {
        UNMATCHED = 0,
        ACTIVATED,
        FIRING,
        TERMINATED

    };

  public:
    rule(std::string const &&_name, int  _salience, rule_state &&_rule_state) : name(name), salience(_salience), state(_rule_state)
    {
    }
    rule(std::string const &_name, int _salience, rule_state &_rule_state) : name(name), salience(_salience), state(_rule_state)
    {
    }
    rule(std::string const &_name) : name(name), salience(0), state(rule_state::UNMATCHED)
    {
    }
    void fire()
    {
        if (state == ACTIVATED)
        {
            state = FIRING;
            doFire();
            state = TERMINATED;
        }
    }

  private:
    void doFire()
    {
        for (auto &act : rhs)
            rhs.get().execute();
    }

    rule_state state;
    std::string name;
    int salience;
    std::vector<std::shared_ptr<condition>> lhs;
    std::vector<std::shared_ptr<action>> rhs;
};
}