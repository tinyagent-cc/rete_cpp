#include <string>
#include <memory>
#include <vector>

namespace rete_cpp
{

class agenda;

template <typename fact>
class working_memeory_element
{

  public:
    explicit working_memeory_element(std::unique_ptr<fact> &&_fact_ptr) : fact_ptr(_fact_ptr) {}
    explicit working_memeory_element(std::unique_ptr<fact> &_fact_ptr) : fact_ptr(std::move(_fact_ptr)) {}
    working_memeory_element() = delete;
    std::unique_ptr<fact> fact_ptr;
};

template <typename fact>
using wme = working_memeory_element<fact>;


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
  private:
  std::vector<
    
};
}