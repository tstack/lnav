#ifndef VARIANT_CAST_HPP
#define VARIANT_CAST_HPP

#include <type_traits>

namespace mapbox {
namespace util {

namespace detail {

template <class T>
class static_caster
{
public:
    template <class V>
    T& operator()(V& v) const
    {
        return static_cast<T&>(v);
    }
};

template <class T>
class dynamic_caster
{
public:
    using result_type = T&;
    template <class V>
    T& operator()(V& v, typename std::enable_if<!std::is_polymorphic<V>::value>::type* = nullptr) const
    {
        throw std::bad_cast();
    }
    template <class V>
    T& operator()(V& v, typename std::enable_if<std::is_polymorphic<V>::value>::type* = nullptr) const
    {
        return dynamic_cast<T&>(v);
    }
};

template <class T>
class dynamic_caster<T*>
{
public:
    using result_type = T*;
    template <class V>
    T* operator()(V& v, typename std::enable_if<!std::is_polymorphic<V>::value>::type* = nullptr) const
    {
        return nullptr;
    }
    template <class V>
    T* operator()(V& v, typename std::enable_if<std::is_polymorphic<V>::value>::type* = nullptr) const
    {
        return dynamic_cast<T*>(&v);
    }
};
}

template <class T, class V>
typename detail::dynamic_caster<T>::result_type
dynamic_variant_cast(V& v)
{
    return mapbox::util::apply_visitor(detail::dynamic_caster<T>(), v);
}

template <class T, class V>
typename detail::dynamic_caster<const T>::result_type
dynamic_variant_cast(const V& v)
{
    return mapbox::util::apply_visitor(detail::dynamic_caster<const T>(), v);
}

template <class T, class V>
T& static_variant_cast(V& v)
{
    return mapbox::util::apply_visitor(detail::static_caster<T>(), v);
}

template <class T, class V>
const T& static_variant_cast(const V& v)
{
    return mapbox::util::apply_visitor(detail::static_caster<const T>(), v);
}
}
}

#endif // VARIANT_CAST_HPP
