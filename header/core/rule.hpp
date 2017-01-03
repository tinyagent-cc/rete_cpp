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
        MATCHED,
        FIRING,
        TERMINATED

    };

  public:
    rule(std::string const &_name) : name(name), state(rule_state::UNMATCHED)
    {
    }
    void fire()
    {
        if (state == MATCHED)
            doFire();
    }
    
  private:
    void doFire()
    {
        for (auto &act : rhs)
            rhs.get().execute();
    }

    rule_state state;
    std::string name;
    std::vector<std::shared_ptr<condition>> lhs;
    std::vector<std::shared_ptr<action>> rhs;
};
}