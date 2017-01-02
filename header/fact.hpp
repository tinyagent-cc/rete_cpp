
namespace rete_cpp
{

    template< class fact_type >
    class fact : public fact_type
    {
        typedef fact_type* fact_ptr;
        typedef fact_type type;
    };


}