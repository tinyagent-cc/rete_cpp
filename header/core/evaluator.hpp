#include <functional>
#include <type_traits>
#include <boost/fusion/functional/invocation/invoke.hpp>

namespace rete_cpp
{

template <typename T>
struct evaluator
{
    bool evaluate(const T& left, const T& right)
    {
        return   eval(left,right);
    }




};


template<typename T,  typename Enable=void >
struct eval
{
   
};


template<typename T,  typename std::enable_if<
    !(std::is_void<T>::value
    || std::is_compound<T>::value) || std::is_class<T>::value
    >::type >
struct eval
{
    bool operator(T t1, T t2)
    {
        return t1 == t2;
    }
};

template<typename T,  typename std::enable_if<
    !std::is_void<T>::value 
    && std::is_member_pointer<T>::value) 
    >::type >
struct eval
{
    bool operator(T t1, T t2)
    {
        return t1 == t2;
    }
};



template< typename T>
std::enable_if<
    !std::is_void<t>::value
>::type
struct valueOf
{
    auto operator()(T t)
    {
        return t;
    }
};
template< typename T>
std::enable_if<
    std::is_member_object_pointer<T>::value
>::type
struct valueOf
{
  //  auto std::mem_fn
   // decltype(std::result_of<>)
    auto operator()(T t)
    {
        
        return t;
    }
};
template< typename T, typename ...Args>
std::enable_if<
    std::is_member_function_pointer<T>::value
    ||  std::is_function<T>::value
>::type
struct valueOf
{
  //  auto std::mem_fn
   // decltype(std::result_of<>)
    auto operator()(T t, Args...args)
    {
        return invoke(t,args);
    }
};
 
}