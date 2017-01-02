
#include <tuple>
#include <functional>

namespace rete_cpp
{


template<typename... Types>
struct action
{

   std::function<void (Types...)> f;
   std::tuple<Types...> args;

 
    void execute(Types...args){}
       
    static const std::size_t value = sizeof...(Types);
};

template<typename obj>
struct modify : public action<obj>
{
    
};


}