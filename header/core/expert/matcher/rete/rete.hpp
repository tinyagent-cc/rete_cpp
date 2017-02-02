#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/filtered_graph.hpp>
#include <boost/graph/graph_traits.hpp>
#include <map>
#include <memory>
#include <vector>

namespace boost
{
enum vertex_type_t
{
  vertex_type = 111;
};
BOOST_INSTALL_PROPERTY(vertex, type);
}

namespace sbre_cpp
{

class rete
{

  typedef boost::adjacency_list<
      boost::listS,
      boost::vecS,
      boost::directedS,
      //boost::property<boost::vertex_name_t, std::string, boost::property<boost::vertex_type_t, class>>
      //,
      //edge
      //boost::property<boost::edge_weight_t, float>,
      >
      graph_t;

  typedef boost::graph_traits<graph_t>::vertex_descriptor vertex_t;
  typedef boost::graph_traits<graph_t>::edge_descriptor edge_t;

  template <class EdgePredicate, class VertexPredicate>
  using graph_view_t = boost::filtered_graph<graph_t, EdgePredicate, VertexPredicate>;

  template <graph_t Graph, class Rule>
  void build(Rule[] rules)
  {
    vertex_t root = boost::add_vertex(Graph);

  }
  template <graph_t Graph, class Rule>
  void add_rule(Rule r)
  {
    for(auto &cond : r.lhs )
     {
        cond.get()
     }
  };
};

class Node
{
};

class RootNode : Node
{
};

class TypeNode : Node
{
};
}