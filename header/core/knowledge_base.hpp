#include<memory>

namespace rete_cpp
{

class working_memory;

class knowledge_base
{

    public:
        knowledge_base ():wm(new working_memory){}

    private:
        std::unique_ptr< working_memory> wm;

};


}