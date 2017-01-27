#include <memory>
#include <array>
#include <string>
namespace rete_cpp
{

class rule;

template <class FactType>
struct knowledge_base : private RulesCompiler
{

    typedef std::array<std::unique_ptr<rule>> rules_container;

    explicit knowledge_base(std::string &_name) : name(_name) {}
    knowledge_base() = delete;
    void init()
    {
        std::vector<std::exception> errors;
        RulesCompiler::compile(errors);
        if (!errors.empty())
        {
            std::string message = "Compiler errors :";
            for (auto &&ex : errors)
            {
                message += ex.what();
                message += "\n\r";
            }
            throw std::exception(message);
        }
        rules_a_uptr = RulesCompiler::build();
    }

    std::string name;
    rules_container rules_a_uptr;
};
}