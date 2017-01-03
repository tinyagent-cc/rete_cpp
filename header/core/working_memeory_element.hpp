#include <memory>

namespace rete_cpp
{

template <typename fact>
class working_memeory_element
{

  public:
    explicit working_memeory_element(std::unique_ptr<fact> &&_fact_ptr) : fact_ptr(_fact_ptr) {}
    explicit working_memeory_element(std::unique_ptr<fact> &_fact_ptr) : fact_ptr(std::move(_fact_ptr)) {}
    working_memeory_element() = delete;
    std::unique_ptr<fact> fact_ptr;
};
}