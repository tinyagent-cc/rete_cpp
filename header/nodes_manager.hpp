#include <vector>
#include <map>
#include <memory>
#include <boost/any.hpp>
#include <boost/graph/adjacency_list.hpp>


namespace rete_cpp
{

    class AlphaNode;
    class BetaNode;

    template <class Node>
    class NodeGraphManager
    {




      private:
      typedef boost::adjacency_list<boost:vecS, boost::vecS, boost::bidirectionalS> nodes_graph;
      typedef nodes_graph::vertex_iterator vertex_iterator;
      typedef nodes_graph::edge_iterator edge_iterator;

    }


}