
namespace rete_cpp
{


template<typename... Types>
struct action
{
    static const std::size_t value = sizeof...(Types);
    void execute(Types...args){}
};

template<typename obj>
struct modify : public action<obj>
{
    
};


}