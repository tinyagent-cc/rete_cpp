
#include <boost/tuple/tuple.hpp>
#include <iostream>
#include <string>
#include <type_traits>
#include <functional>
#include <boost/fusion/functional/invocation/invoke.hpp>

namespace rete_cpp
{


namespace details{
    
template<bool activate, typename T, typename... args>
struct apply_impl;

template< typename T,typename... args>
struct apply_impl<false,T, args...>{};

template< typename FN,typename... P>
struct apply_impl<true,FN, P... args>{
       static const  decltype( std::result_of<FN(P...)>::type ) value = FN( std::forward<P>(args)...);
    };

}
template<typename T, bool enabled=false, typename ...P>
struct action
{
    template<P... args>
    using apply=details::apply_impl<enabled,T, args...> ;
}; 


template<typename FN, typename ...P>
struct action<FN, typename std::enable_if<std::is_function<FN>::value, bool>{} ,P...>{
    template<P... args>
    using apply=details::apply_impl<FN, args...> ;
};

/*
template<class T, class...P>
using action = details::action_helper<T,P...>;
*/
int test(int x)
{
    return x;
}


int main()
{
 auto al= action<decltype(test), true,int>::apply<5>::value;
  std::cout << "result :" << al <<std::endl;
  return 0;
}

}