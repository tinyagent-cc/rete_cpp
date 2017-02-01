#include <functional>
#include <type_traits>

// invoke implementation copied/modified from std++ 17 to be cxx11 complient

namespace detail
{

template <class T>
struct is_reference_wrapper : std::false_type
{
};
template <class U>
struct is_reference_wrapper<std::reference_wrapper<U>> : std::true_type
{
};

template <class Base, class T, class Derived, class... Args>
auto INVOKE(T Base::*pmf, Derived &&ref, Args &&... args) noexcept(noexcept((std::forward<Derived>(ref).*pmf)(std::forward<Args>(args)...)))
    -> typename std::enable_if<std::is_function<T>::value &&
                                   std::is_base_of<Base, typename std::decay<Derived>::type>::value::type,
                               decltype((std::forward<Derived>(ref).*pmf)(std::forward<Args>(args)...))>
{
    return (std::forward<Derived>(ref).*pmf)(std::forward<Args>(args)...);
}

template <class Base, class T, class RefWrap, class... Args>
auto INVOKE(T Base::*pmf, RefWrap &&ref, Args &&... args) noexcept(noexcept((ref.get().*pmf)(std::forward<Args>(args)...)))
    -> typename std::enable_if<std::is_function<T>::value &&
                                   is_reference_wrapper<typename std::decay<RefWrap>::type>::value,
                               decltype((ref.get().*pmf)(std::forward<Args>(args)...))>::type

{
    return (ref.get().*pmf)(std::forward<Args>(args)...);
}

template <class Base, class T, class Pointer, class... Args>
auto INVOKE(T Base::*pmf, Pointer &&ptr, Args &&... args) noexcept(noexcept(((*std::forward<Pointer>(ptr)).*pmf)(std::forward<Args>(args)...)))
    -> typename std::enable_if<std::is_function<T>::value &&
                                   !is_reference_wrapper<typename std::decay<Pointer>::type>::value &&
                                   !std::is_base_of<Base, typename std::decay<Pointer>::type>::value,
                               decltype(((*std::forward<Pointer>(ptr)).*pmf)(std::forward<Args>(args)...))>::type
{
    return ((*std::forward<Pointer>(ptr)).*pmf)(std::forward<Args>(args)...);
}

template <class Base, class T, class Derived>
auto INVOKE(T Base::*pmd, Derived &&ref) noexcept(noexcept(std::forward<Derived>(ref).*pmd))
    -> typename std::enable_if<!std::is_function<T>::value &&
                                   std::is_base_of<Base, typename std::decay<Derived>::type>::value,
                               decltype(std::forward<Derived>(ref).*pmd)>::type
{
    return std::forward<Derived>(ref).*pmd;
}

template <class Base, class T, class RefWrap>
auto INVOKE(T Base::*pmd, RefWrap &&ref) noexcept(noexcept(ref.get().*pmd))
    -> typename std::enable_if<!std::is_function<T>::value &&
                                   is_reference_wrapper<typename std::decay<RefWrap>::type>::value,
                               decltype(ref.get().*pmd)>::type
{
    return ref.get().*pmd;
}

template <class Base, class T, class Pointer>
auto INVOKE(T Base::*pmd, Pointer &&ptr) noexcept(noexcept((*std::forward<Pointer>(ptr)).*pmd))
    -> typename std::enable_if<!std::is_function<T>::value &&
                                   !is_reference_wrapper<typename std::decay<Pointer>::type>::value &&
                                   !std::is_base_of<Base, typename std::decay<Pointer>::type>::value,
                               decltype((*std::forward<Pointer>(ptr)).*pmd)>::type
{
    return (*std::forward<Pointer>(ptr)).*pmd;
}

template <class F, class... Args>
auto INVOKE(F &&f, Args &&... args) noexcept(noexcept(std::forward<F>(f)(std::forward<Args>(args)...)))
    -> typename std::enable_if<!std::is_member_pointer<typename std::decay<F>::type>::value,
                               decltype(std::forward<F>(f)(std::forward<Args>(args)...))>::type
{
    return std::forward<F>(f)(std::forward<Args>(args)...);
}
} // namespace detail

namespace sbre_cpp
{

template <class F, class... ArgTypes>
auto invoke(F &&f, ArgTypes &&... args)
    // exception specification for QoI
    noexcept(noexcept(detail::INVOKE(std::forward<F>(f), std::forward<ArgTypes>(args)...)))
        -> decltype(detail::INVOKE(std::forward<F>(f), std::forward<ArgTypes>(args)...))
{
    return detail::INVOKE(std::forward<F>(f), std::forward<ArgTypes>(args)...);
}
}
