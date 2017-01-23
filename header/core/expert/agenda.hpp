
#include <string>
#include <queue>
namespace rete_cpp
{

  class rule;

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

class agenda
{

public:
  agenda(const std::string _name, const std::int32_t _salience) : name(_name), salience(_salience) {}
  agenda(const std::string _name) : name(_name), salience(0) {}
  void add(rule...rules){for (auto &r:rules) activatedRulesQueue.push_front(r);}
private:
  std::string name;


  // Using lambda to compare elements.
  auto comparator = [](rule& left, rule& right) { return left.salience < right.salience; };
  std::priority_queue<rule, std::vector<rule>, decltype(comparator)> activatedRulesQueue(comparator);
};
}