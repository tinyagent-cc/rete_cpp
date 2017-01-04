#include <string>

namespace rete_cpp
{

class agenda;

class working_memory
{

public:
  void fireAllRules() {}
  template<typename agenda_filter>
  void fireAllRules(agenda_filter const &filter) {}
  void getAgenda(std::string const &name, agenda &_agenda)
  {
  }

  void focusOn(){}

  // actions
  template <typename data_type_ref>
  void insert(data_type_ref....data) {}
  template <typename data_type_ref>
  void update(data_type_ref....data) {}
  template <typename data_type_ref>
  void retract(data_type_ref....data) {}

  //halt
  void halt() {}
};
}