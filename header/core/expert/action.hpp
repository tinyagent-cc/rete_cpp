
#include <boost/tuple/tuple.hpp>
#include <functional>

namespace rete_cpp
{



template<typename T, typename enabled=void, typename...P>
struct action{
     template<P...args> struct apply{ };
};

template<typename FN,  typename...P>
struct action <FN,  typename std::enable_if< std::is_function<FN>::value >::type, P... >{
    template<P...args> struct apply{ 
            static const decltype(std::result_of<FN(P...)>::type) value = FN(...args);
    };
};

template<typename T, typename enabled=void>
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