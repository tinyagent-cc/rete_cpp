#include <vector>
#include <map>
#include <memory>
#include <boost/any.hpp>
#include <boost/graph/adjacency_list.hpp>
namespace rete_cpp
{

template<class T>
    class alpha_node
    {




        private:
        typedef T nodeType;
        typedef T* nodePtr;
        std::shared_ptr<nodeType> node;
        std::shared_ptr<nodePtr> node_ptr;
        std::shared_ptr<boost::any> nextNode;
        std::shared_ptr<boost::any> previousNode;


    }








}

