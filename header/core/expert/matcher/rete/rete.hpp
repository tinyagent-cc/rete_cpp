#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/filtered_graph.hpp>
#include <boost/graph/graph_traits.hpp>
#include <map>
#include <memory>
#include <vector>

namespace boost
  {
    enum vertex_compile_cost_t { vertex_compile_cost = 111; };
    BOOST_INSTALL_PROPERTY(vertex, compile_cost);

  }

namespace sbre_cpp {

class rete {

  template <class Graph, class EdgePredicate, class VertexPredicate>
  using lfs_rules =
      boost::filtered_graph<Graph, EdgePredicate, VertexPredicate>;

  typedef boost::adjacency_list<
  boost::listS, 
  boost::vecS, 
  boost::directedS, 
  boost::property<boost::vertex_name_t, std::string , boost::property<boost::vertex_compile_cost_t, float>  >,
    //edge
  boost::property<boost::edge_weight2_t, float>
  > graph_t;
     
  typedef boost::graph_traits<graph_t>::vertex_descriptor vertex_t; 
  typedef boost::graph_traits<graph_t>::edge_descriptor edge_t; 

  

  graph_t Graph;

  void build() {




  }
};

class Node {};

class RootNode : Node {};

class TypeNode : Node {};






}