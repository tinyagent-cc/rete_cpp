#include <functional>

namespace rete_cpp
{

template <typename T>
struct evaluator
{
    bool eval(const T& left, const T& right)
    {
        return  ::eval<T>(left,right);
    }




};


template<typename T,  typename T2=void >
struct eval
{
   
}

template<typename T,  typename std::enable_if<!std::is_void<T>::value>::type>
struct eval
{
    bool operator(T t1, T t2)
    {
        return t1 == t2;
    }
}




}