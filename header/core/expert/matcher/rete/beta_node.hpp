

namespace sbre_cpp
{

template <typename lfs, typename rhs>
struct beta_node
{
};
template <typename lfs, typename rhs>
struct not_node : beta_node<lfs, rhs>
{
    template <lfs lfs_v, rhs rhs_v>
    struct eval
    {
    };
};
template <typename lfs, typename rhs>
struct join_node : beta_node<lfs, rhs>
{
    template <lfs lfs_v, rhs rhs_v>
    struct eval
    {
    };
};


}