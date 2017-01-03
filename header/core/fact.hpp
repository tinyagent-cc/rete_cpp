
namespace rete_cpp
{

    template< typename fact_type, typename... attribute_type_alias  >
    struct  fact : public fact_type
    {
        static const size_t numAttributes = sizeof...(attribute_type_alias);
        typedef fact_type* fact_ptr;
        typedef fact_type type;
    };




}