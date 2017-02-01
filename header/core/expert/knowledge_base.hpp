#include <memory>
#include <array>
#include <string>
namespace sbre_cpp
{


template <class rule_type>
struct knowledge_base : private RulesCompiler
{

    typedef std::array<std::unique_ptr<rule_type>> rules_container;

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