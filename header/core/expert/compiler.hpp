#include<array>
#include<memory>

namespace rete_cpp
{

// need to be specialized
template <typename compiler>
struct compiler_traits
{
  typedef typename compiler::input_source input_source;
  typedef typename compiler::category category ;
}



template< typename in_stream_type, class compiler_tag=clips_compiler_tag>
class compiler
{

 typedef std::array<std::unique_ptr<rule>> rules_container;
  public:
    explicit compiler(in_stream_type&& if_stream):input_source(if_stream){}
    explicit compiler(in_stream_type& if_stream):input_source(if_stream){}
    compiler()=delete;

     void compile(std::vector<std::exception>& errors)  {

        typename compiler_traits<compiler>::category compiler_tag;
        typename compiler_traits<compiler>::input_source input_source;
        compile<input_source>(input_source, errors,compiler_tag);

     }

    std::array<std::unique_ptr<rule> build()
    {
      rules_container rules;
      if(!has_compile_errors)
      {

      }
      return std::move(rules);
    }

    bool has_compile_errors;


    private 
     in_stream_type input_source;
     compiler_tag
};





struct default_compiler_tag{};
struct clips_compiler_tag{};

  namespace detail {
    template<typename src_type>
    void compile(src_type source, std::vector<std::exception> errors, default_compiler_tag)
    {

    }
    template<typename src_type>
    void compile(src_type source, std::vector<std::exception> errors, clips_compiler_tag)
    {
      
    }
    
    
  }


}