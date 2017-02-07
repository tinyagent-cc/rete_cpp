#include <vector>
#include <map>
#include <memory>
#include <boost/any.hpp>
#include <boost/graph/adjacency_list.hpp>
namespace sbre_cpp
{

template<typename...>
struct condition;
template<>
struct condition<> : std::true_type{};

template<bool T>
struct condition :  std::integral_constant<bool, T>
{
}; 


template <typename Condtion>
struct alpha_node
{

  private:
    typedef T nodeType;
    typedef T *nodePtr;
    std::shared_ptr<nodeType> node;
    std::shared_ptr<nodePtr> node_ptr;
    std::shared_ptr<boost::any> nextNode;
    std::shared_ptr<boost::any> previousNode;
};



struct alpha_network
{

   bool select(working_memeory_element& const wme, fact& const fact){
       return std::is_same<wme.fact_ptr.get()::type, fact::type >()::value;
   }

   typedef boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS> nodes_graph; 

};


// Using lambda to compare elements.
  template <class wme>
  auto comparator = [](T &left, T &right) bool->{ return left.salience < right.salience; };


/* template<typename... facts>
 struct Token
 {
    typedef std::shared_ptr<working_memeory_element<facts...>> type;
 };*/

//cpp11
template <typename... facts>
using Token = std::shared_ptr<working_memeory_element<facts...>>;
}
