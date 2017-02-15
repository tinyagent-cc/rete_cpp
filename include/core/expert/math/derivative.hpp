
#include <limits>  //   std::numeric_limits<double>::infinity();
#include <cstdint> //std::uint8_t

template <std::uint8_t n, typename RealType, typename Fx, typename Enabled = void>
struct derivative
{
    RealType operator()(Fx &fx)
    {
        return derivative<n, RealType, >
    }
};

template <typename RealType, class Fx>
struct derivative<1, RealType, Fx, typename std::enable_if<
                                       std::is_floating_point<RealType>::value &&
                                       std::is_arithmetic<Fx>::value>::type>
{

    static RealType const value = 0;
};

template <class Fx>
struct derivative<0, decltype(Fx), Fx, typename std::enable_if<
                                           std::is_floating_point<decltype(Fx)>::value>::type>
{


    static decltype(Fx) const value = std::declval(Fx);
};

template <class Fx>
struct derivative<1, decltype(Fx), Fx, typename std::enable_if<
                                           std::is_floating_point<decltype(Fx)>::value && std::is_arithmetic<Fx>::value>::type>
{
    decltype(Fx) operator()(Fx &fx)
    {
        return 0;
    }
};
