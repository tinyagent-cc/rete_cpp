
#include <string>
namespace rete_cpp
{

class agenda
{

  public:

  agenda(const std::string _name, const std::int32_t _salience):name(_name), salience(_salience){}
  agenda(const std::string _name):name(_name), salience(0){}
  private:
    std::string name;
    std::int32_t salience;
};


}