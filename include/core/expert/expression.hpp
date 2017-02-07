// Include all of Proto
#include <boost/proto/proto.hpp>
//#include <boost/phoenix/

// Create some namespace aliases
namespace mpl = boost::mpl;
namespace fusion = boost::fusion;
namespace proto = boost::proto;
namespace phoenix = boost::phoenix;

// Allow unqualified use of Proto's wildcard pattern
using proto::_;


namespace sbre_cpp
{

    struct Expresssion{};


    template<>struct BoolExpr : std::true_type{};

    template <bool result>
    struct BoolExpr : std::integral_constant<bool, result>{};

     template <typename lfs,typename rhs, typename boolOp>
    struct BoolExpr : BoolExpr<boolOp(lfs, rhs)>{};


    namespace details
    {
        struct _expr : std::integral
        struct make_minus
        


    }




}