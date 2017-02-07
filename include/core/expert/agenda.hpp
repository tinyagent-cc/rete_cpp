#include <boost/variant.hpp>
#include <string>
#include <queue>
namespace sbre_cpp
{

// Inference engine prepares a priotorized list of rules called agenda.
// The rules in the list must be satisfied by the facts in the working memory.
// When the inference engine notices a fact that satisfies the pattern in the condition part of the rule then it adds the rule to the agenda.
// If a rule has mulitple patterns then all of its patterns must be satisfied to get place in the agend.
// In a condition a>b and b>c, for example, both a>b and b>c must be satisfied.
// A rule whose patterns are satisfied is said to be activated or instantiated.
// When there is more than one activated rule in the agenda then the inference engine has to select one rule, based on priority or on other factors, for firing.
// Rule based expert systems are built using refraction to prevent trivial loops.
// That is a rule which is fired on a fact will not be fired again and again on the same fact.
// To implement this feature OPS5 uses a unique identifier called timetag.
// Then-part of the rule contains actions to be executed when the rule fires.
// Some of the actions are addition of facts to the working memory, removal of facts from the working memory and printing results.
// Agenda conflict occurs when different activations have the same priority.
// Agenda conflict is tackled by strategies such as first come first execute, assigning default priority, and so on.

template <class RuleType, class AgendaType>
struct agenda_impl
{
  typedef boost::variant<RuleType, AgendaType> agenda_inner_type;
  typedef std::priority_queue<agenda_inner_type> agenda_priority_queue;

  explicit agenda(const std::string _name, const int _salience) : name(_name), salience(_salience) {}
  explicit agenda(const std::string _name) : name(_name), salience(0) {}
  agenda() = delete;

namesapce details{
  template <class T>
  void add(T... rules)
  {
    for (auto &r : T)
      activatedRulesQueue.push_front(r);
  }
  template <typename T, unsigned int index>
  T get() { return boost::get<T>(activatedRulesQueue[index]); }
  
}
  template<unsigned int index>
  using get_agenda=details::get<AgendaType, index>;
 
  template<unsigned int index>
  using get_rule=details::get<RuleType, index>;
  

  using add_rule=details::add<RuleType>;
  using add_agenda=details::add<AgendaType>;
  
  std::string name;
  int salience;

  // Using lambda to compare elements.
  template <class T>
  auto comparator = [](T &left, T &right) { return left.salience < right.salience; };

  agenda_priority_queue<rule, std::vector<boost::variant<RuleType, AgendaType>>, decltype(comparator)> activatedRulesQueue(comparator);
};
}