#include <string>

namespace rete_cpp
{

class agenda;
class agenda_filter;
class AlphaNode;
class BetaNode;

class working_memory
{

  public:
    void fireAllRules() {}
    void fireAllRules(agenda_filter const &filter) {}
    void halt() {}
    void getAgenda(std::string const &name, agenda &_agenda)
    {
        
    }
};
}