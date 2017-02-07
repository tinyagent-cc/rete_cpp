
namespace sbre_cpp
{

class condition;


template <
typename fact_type, 
typename contition_type,
typename... attribute_type_alias
>
struct fact : public fact_type
{
    // static const size_t numAttributes = sizeof...(attribute_type_alias);
    typedef fact_type type;
    bool assert_fact(const attribute_type_alias... values_alias,  const contition_type & cdt)
    {
        return true;
    }
};
}