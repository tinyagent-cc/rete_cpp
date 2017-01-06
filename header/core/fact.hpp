
namespace rete_cpp
{

class condition;
template <typename fact_type, typename... attribute_type_alias>
struct fact : public fact_type
{
    // static const size_t numAttributes = sizeof...(attribute_type_alias);
    typedef fact_type type;
    bool assert_fact(attribute_type_alias... values_alias,  const condition & cdt)
    {
        return true;
    }
};
}