#pragma once

#include <type_traits>
#include <iostream>
#include <cassert>
#include <stdexcept>
#include <string>
#include <cstdio>

#ifndef __host__
#  define __host__
#  define VARIANT_UNDEF_HOST
#endif

#ifndef __device__
#  define __device__
#  define VARIANT_UNDEF_DEVICE
#endif


namespace std
{
namespace experimental
{
namespace detail
{


template<size_t i, size_t... js>
struct constexpr_max
{
  static const size_t value = i < constexpr_max<js...>::value ? constexpr_max<js...>::value : i;
};


template<size_t i>
struct constexpr_max<i>
{
  static const size_t value = i;
};


} // end detail


template<typename... Types>
class variant;


namespace detail
{

template<size_t i, typename Variant> struct variant_element;


template<size_t i, typename T0, typename... Types>
struct variant_element<i, variant<T0, Types...>>
  : variant_element<i-1,variant<Types...>>
{};


template<typename T0, typename... Types>
struct variant_element<0, variant<T0, Types...>>
{
  typedef T0 type;
};


template<size_t i, typename... Types>
using variant_element_t = typename variant_element<i,Types...>::type;


} // end detail


static constexpr const size_t variant_not_found = static_cast<size_t>(-1);


namespace detail
{


template<typename T, typename U>
struct propagate_reference;


template<typename T, typename U>
struct propagate_reference<T&, U>
{
  typedef U& type;
};


template<typename T, typename U>
struct propagate_reference<const T&, U>
{
  typedef const U& type;
};


template<typename T, typename U>
struct propagate_reference<T&&, U>
{
  typedef U&& type;
};


template<size_t i, typename VariantReference>
struct variant_element_reference
  : propagate_reference<
      VariantReference,
      variant_element_t<
        i,
        typename std::decay<VariantReference>::type
      >
    >
{};


template<size_t i, typename VariantReference>
using variant_element_reference_t = typename variant_element_reference<i,VariantReference>::type;


} // end detail


template<typename Visitor, typename Variant>
__host__ __device__
auto visit(Visitor visitor, Variant&& var) ->
  typename std::result_of<
    Visitor(detail::variant_element_reference_t<0,decltype(var)>)
  >::type;


template<typename Visitor, typename Variant1, typename Variant2>
__host__ __device__
auto visit(Visitor visitor, Variant1&& var1, Variant2&& var2) ->
  typename std::result_of<
    Visitor(detail::variant_element_reference_t<0,decltype(var1)>,
            detail::variant_element_reference_t<0,decltype(var2)>)
  >::type;


namespace detail
{


template<size_t i, typename T, typename... Types>
struct find_type_impl;


// no match, keep going
template<size_t i, typename T, typename U, typename... Types>
struct find_type_impl<i,T,U,Types...>
  : find_type_impl<i+1,T,Types...>
{};


// found a match
template<size_t i, typename T, typename... Types>
struct find_type_impl<i,T,T,Types...>
{
  static constexpr const size_t value = i;
};


// no match
template<size_t i, typename T>
struct find_type_impl<i,T>
{
  static constexpr const size_t value = variant_not_found;
};


template<typename T, typename... Types>
using find_type = find_type_impl<0,T,Types...>;


template<typename T, typename Variant>
struct is_variant_alternative;

template<class T, class... Types>
struct is_variant_alternative<T,variant<Types...>>
  : std::integral_constant<
      bool,
      (find_type<T,Types...>::value != variant_not_found)
    >
{};


} // end detail


class bad_variant_access : public std::logic_error
{
  public:
    explicit bad_variant_access(const std::string& what_arg) : logic_error(what_arg) {}
    explicit bad_variant_access(const char* what_arg) : logic_error(what_arg) {}
};


namespace detail
{


__host__ __device__
inline void throw_bad_variant_access(const char* what_arg)
{
#ifdef __CUDA_ARCH__
  printf("bad_variant_access: %s\n", what_arg);
  assert(0);
#else
  throw bad_variant_access(what_arg);
#endif
}


template<class T, class...>
struct first_type
{
  using type = T;
};

template<class... Types>
using first_type_t = typename first_type<Types...>::type;


template<class... Types>
class variant_storage
{
  typedef typename std::aligned_storage<
    constexpr_max<sizeof(Types)...>::value
  >::type storage_type;
  
  storage_type storage_;
};

template<>
class variant_storage<> {};

} // end detail


template<typename... Types>
class variant : private detail::variant_storage<Types...>
{
  public:
    __host__ __device__
    variant()
      : variant(detail::first_type_t<Types...>{})
    {}

  private:
    struct binary_move_construct_visitor
    {
      template<class T>
      __host__ __device__
      void operator()(T& self, T&& other)
      {
        new (&self) T(std::move(other));
      }

      template<class... Args>
      __host__ __device__
      void operator()(Args&&...){}
    };

  public:
    __host__ __device__
    variant(variant&& other)
      : index_(other.index())
    {
      std::experimental::visit(binary_move_construct_visitor(), *this, std::move(other));
    }

  private:
    struct binary_copy_construct_visitor
    {
      template<class T>
      __host__ __device__
      void operator()(T& self, const T& other)
      {
        new (&self) T(other);
      }

      template<class... Args>
      __host__ __device__
      void operator()(Args&&...){}
    };

  public:
    __host__ __device__
    variant(const variant& other)
      : index_(other.index())
    {
      std::experimental::visit(binary_copy_construct_visitor(), *this, other);
    }

  private:
    template<class T>
    struct unary_copy_construct_visitor
    {
      const T& other;

      __host__ __device__
      void operator()(T& self)
      {
        new (&self) T(other);
      }

      template<class U>
      __host__ __device__
      void operator()(U&&) {}
    };

  public:
    template<class T,
             class = typename std::enable_if<
               detail::is_variant_alternative<T,variant>::value
             >::type>
    __host__ __device__
    variant(const T& other)
      : index_(detail::find_type<T,Types...>::value)
    {
      std::experimental::visit(unary_copy_construct_visitor<T>{other}, *this);
    }

  private:
    template<class T>
    struct unary_move_construct_visitor
    {
      T& other;

      __host__ __device__
      void operator()(T& self)
      {
        new (&self) T(std::move(other));
      }

      template<class U>
      __host__ __device__
      void operator()(U&&){}
    };

  public:
    template<class T,
             class = typename std::enable_if<
               detail::is_variant_alternative<T,variant>::value
             >::type>
    __host__ __device__
    variant(T&& other)
      : index_(detail::find_type<T,Types...>::value)
    {
      std::experimental::visit(unary_move_construct_visitor<T>{other}, *this);
    }

  private:
    struct destruct_visitor
    {
      template<typename T>
      __host__ __device__
      typename std::enable_if<
        !std::is_trivially_destructible<T>::value
      >::type
        operator()(T& x)
      {
        x.~T();
      }
      
      template<typename T>
      __host__ __device__
      typename std::enable_if<
        std::is_trivially_destructible<T>::value
      >::type
        operator()(T& x)
      {
        // omit invocations of destructors for trivially destructible types
      }
    };

  public:
    __host__ __device__
    ~variant()
    {
      std::experimental::visit(destruct_visitor(), *this);
    }

  private:
    struct forward_assign_visitor
    {
      template<class A, class B>
      __host__ __device__
      void operator()(A& a, B&& b) const
      {
        a = std::forward<B>(b);
      }
    };

    struct destroy_and_copy_construct_visitor
    {
      template<class A, class B>
      __host__ __device__
      void operator()(A& a, const B& b) const
      {
        // copy b to a temporary
        B tmp = b;

        // destroy a
        a.~A();

        // placement move from tmp to a
        new (&a) B(std::move(tmp));
      }
    };

    struct destroy_and_move_construct_visitor
    {
      template<class A, class B>
      __host__ __device__
      void operator()(A& a, B&& b) const
      {
        // destroy a
        a.~A();

        using type = typename std::decay<B>::type;

        // placement move from b
        new (&a) type(std::move(b));
      }
    };

  public:
    __host__ __device__
    variant& operator=(const variant& other)
    {
      if(index() == other.index())
      {
        std::experimental::visit(forward_assign_visitor(), *this, other);
      }
      else
      {
        std::experimental::visit(destroy_and_copy_construct_visitor(), *this, other);
        index_ = other.index();
      }

      return *this;
    }

    __host__ __device__
    variant& operator=(variant&& other)
    {
      if(index() == other.index())
      {
        std::experimental::visit(forward_assign_visitor(), *this, std::move(other));
      }
      else
      {
        std::experimental::visit(destroy_and_move_construct_visitor(), *this, std::move(other));
        index_ = other.index();
      }

      return *this;
    }

    __host__ __device__
    size_t index() const
    {
      return index_;
    }

  private:
    struct swap_visitor
    {
      template<class A, class B>
      __host__ __device__
      void operator()(A& a, B& b)
      {
        // XXX can't call std::swap because __host__ __device__
        A tmp = std::move(a);
        a = std::move(b);
        b = std::move(tmp);
      }
    };

  public:
    __host__ __device__
    void swap(variant& other)
    {
      if(index() == other.index())
      {
        std::experimental::visit(swap_visitor(), *this, other);
      }
      else
      {
        variant tmp = *this;
        *this = other;
        other = std::move(tmp);
      }
    }

  private:
    struct equals
    {
      template<typename U1, typename U2>
      __host__ __device__
      bool operator()(const U1&, const U2&)
      {
        return false;
      }

      template<typename T>
      __host__ __device__
      bool operator()(const T& lhs, const T& rhs)
      {
        return lhs == rhs;
      }
    };


  public:
    __host__ __device__
    bool operator==(const variant& rhs) const
    {
      return index() == rhs.index() && std::experimental::visit(equals(), *this, rhs);
    }

    __host__ __device__
    bool operator!=(const variant& rhs) const
    {
      return !operator==(rhs);
    }

  private:
    struct less
    {
      template<typename U1, typename U2>
      __host__ __device__
      bool operator()(const U1&, const U2&)
      {
        return false;
      }

      template<typename T>
      __host__ __device__
      bool operator()(const T& lhs, const T& rhs)
      {
        return lhs < rhs;
      }
    };

  public:
    __host__ __device__
    bool operator<(const variant& rhs) const
    {
      if(index() != rhs.index()) return index() < rhs.index();

      return std::experimental::visit(less(), *this, rhs);
    }

    __host__ __device__
    bool operator<=(const variant& rhs) const
    {
      return !(rhs < *this);
    }

    __host__ __device__
    bool operator>(const variant& rhs) const
    {
      return rhs < *this;
    }

    __host__ __device__
    bool operator>=(const variant& rhs) const
    {
      return !(*this < rhs);
    }

  private:
    size_t index_;
};


namespace detail
{


struct ostream_output_visitor
{
  std::ostream &os;

  ostream_output_visitor(std::ostream& os) : os(os) {}

  template<typename T>
  std::ostream& operator()(const T& x)
  {
    return os << x;
  }
};


} // detail


template<typename... Types>
std::ostream &operator<<(std::ostream& os, const variant<Types...>& v)
{
  return std::experimental::visit(detail::ostream_output_visitor(os), v);
}


namespace detail
{


template<typename Visitor, typename Result, typename T, typename... Types>
struct apply_visitor_impl : apply_visitor_impl<Visitor,Result,Types...>
{
  typedef apply_visitor_impl<Visitor,Result,Types...> super_t;

  __host__ __device__
  static Result do_it(Visitor visitor, void* ptr, size_t index)
  {
    if(index == 0)
    {
      return visitor(*reinterpret_cast<T*>(ptr));
    }

    return super_t::do_it(visitor, ptr, --index);
  }


  __host__ __device__
  static Result do_it(Visitor visitor, const void* ptr, size_t index)
  {
    if(index == 0)
    {
      return visitor(*reinterpret_cast<const T*>(ptr));
    }

    return super_t::do_it(visitor, ptr, --index);
  }
};


template<typename Visitor, typename Result, typename T>
struct apply_visitor_impl<Visitor,Result,T>
{
  __host__ __device__
  static Result do_it(Visitor visitor, void* ptr, size_t)
  {
    return visitor(*reinterpret_cast<T*>(ptr));
  }

  __host__ __device__
  static Result do_it(Visitor visitor, const void* ptr, size_t)
  {
    return visitor(*reinterpret_cast<const T*>(ptr));
  }
};


template<typename Visitor, typename Result, typename Variant>
struct apply_visitor;


template<typename Visitor, typename Result, typename... Types>
struct apply_visitor<Visitor,Result,variant<Types...>>
  : apply_visitor_impl<Visitor,Result,Types...>
{};


} // end detail


template<typename Visitor, typename Variant>
__host__ __device__
auto visit(Visitor visitor, Variant&& var) ->
  typename std::result_of<
    Visitor(detail::variant_element_reference_t<0,decltype(var)>)
  >::type
{
  using result_type = typename std::result_of<
    Visitor(detail::variant_element_reference_t<0,decltype(var)>)
  >::type;
 
  using impl = detail::apply_visitor<Visitor,result_type,typename std::decay<Variant>::type>;

  return impl::do_it(visitor, &var, var.index());
}


namespace detail
{


template<typename Visitor, typename Result, typename ElementReference>
struct unary_visitor_binder
{
  Visitor visitor;
  ElementReference x;

  __host__ __device__
  unary_visitor_binder(Visitor visitor, ElementReference x) : visitor(visitor), x(x) {}

  template<typename T>
  __host__ __device__
  Result operator()(T&& y)
  {
    return visitor(x, std::forward<T>(y));
  }
};


template<class Reference>
struct rvalue_reference_to_lvalue_reference
{
  using type = Reference;
};

template<class T>
struct rvalue_reference_to_lvalue_reference<T&&>
{
  using type = T&;
};


template<typename Visitor, typename Result, typename VariantReference>
struct binary_visitor_binder
{
  Visitor visitor;
  // since rvalue references can't be members of classes, we transform any
  // VariantReference which is an rvalue reference to an lvalue reference
  // when we use y in operator(), we cast it back to the original reference type
  typename rvalue_reference_to_lvalue_reference<VariantReference>::type y;

  __host__ __device__
  binary_visitor_binder(Visitor visitor, VariantReference ref) : visitor(visitor), y(ref) {}

  template<typename T>
  __host__ __device__
  Result operator()(T&& x)
  {
    return std::experimental::visit(unary_visitor_binder<Visitor, Result, decltype(x)>(visitor, std::forward<T>(x)), std::forward<VariantReference>(y));
  }
};


} // end detail


template<typename Visitor, typename Variant1, typename Variant2>
__host__ __device__
auto visit(Visitor visitor, Variant1&& var1, Variant2&& var2) ->
  typename std::result_of<
    Visitor(detail::variant_element_reference_t<0,decltype(var1)>,
            detail::variant_element_reference_t<0,decltype(var2)>)
  >::type
{
  using result_type = typename std::result_of<
    Visitor(detail::variant_element_reference_t<0,decltype(var1)>,
            detail::variant_element_reference_t<0,decltype(var2)>)
  >::type;

  auto visitor_wrapper = detail::binary_visitor_binder<Visitor,result_type,decltype(var2)>(visitor, std::forward<Variant2>(var2));

  return std::experimental::visit(visitor_wrapper, std::forward<Variant1>(var1));
}


namespace detail
{


template<class T>
struct get_visitor
{
  __host__ __device__
  T* operator()(T& x) const
  {
    return &x;
  }

  __host__ __device__
  const T* operator()(const T& x) const
  {
    return &x;
  }

  template<class U>
  __host__ __device__
  T* operator()(U&&) const
  {
    return nullptr;
  }
};

} // end detail


template<size_t i, class... Types>
__host__ __device__
detail::variant_element_reference_t<i, variant<Types...>&>
  get(variant<Types...>& v)
{
  if(i != v.index())
  {
    detail::throw_bad_variant_access("i does not equal index()");
  }

  using type = typename std::decay<
    detail::variant_element_t<i,variant<Types...>>
  >::type;

  return *std::experimental::visit(detail::get_visitor<type>(), v);
}


template<size_t i, class... Types>
__host__ __device__
detail::variant_element_reference_t<i, variant<Types...>&&>
  get(variant<Types...>&& v)
{
  if(i != v.index())
  {
    detail::throw_bad_variant_access("i does not equal index()");
  }

  using type = typename std::decay<
    detail::variant_element_t<i,variant<Types...>>
  >::type;

  return std::move(*std::experimental::visit(detail::get_visitor<type>(), v));
}


template<size_t i, class... Types>
__host__ __device__
detail::variant_element_reference_t<i, const variant<Types...>&>
  get(const variant<Types...>& v)
{
  if(i != v.index())
  {
    detail::throw_bad_variant_access("i does not equal index()");
  }

  using type = typename std::decay<
    detail::variant_element_t<i,variant<Types...>>
  >::type;

  return *std::experimental::visit(detail::get_visitor<type>(), v);
}


namespace detail
{


template<size_t i, class... Types>
struct find_exactly_one_impl;

template<size_t i, class T, class U, class... Types>
struct find_exactly_one_impl<i,T,U,Types...> : find_exactly_one_impl<i+1,T,Types...> {};

template<size_t i, class T, class... Types>
struct find_exactly_one_impl<i,T,T,Types...> : std::integral_constant<size_t, i>
{
  static_assert(find_exactly_one_impl<i,T,Types...>::value == variant_not_found, "type can only occur once in type list");
};


template<size_t i, class T>
struct find_exactly_one_impl<i,T> : std::integral_constant<size_t, variant_not_found> {};

template<class T, class... Types>
struct find_exactly_one : find_exactly_one_impl<0,T,Types...>
{
  static_assert(find_exactly_one::value != variant_not_found, "type not found in type list");
};


} // end detail


template<class T, class... Types>
__host__ __device__
bool holds_alternative(const variant<Types...>& v)
{
  constexpr size_t i = detail::find_exactly_one<T,Types...>::value;
  return i == v.index();
}


template<class T, class... Types>
__host__ __device__
typename std::remove_reference<T>::type&
  get(variant<Types...>& v)
{
  return get<detail::find_type<T,Types...>::value>(v);
}


template<class T, class... Types>
__host__ __device__
const typename std::remove_reference<T>::type&
  get(const variant<Types...>& v)
{
  return get<detail::find_type<T,Types...>::value>(v);
}


template<class T, class... Types>
__host__ __device__
typename std::remove_reference<T>::type&&
  get(variant<Types...>&& v)
{
  return std::move(get<detail::find_type<T,Types...>::value>(v));
}


} // end experimental
} // end std

#ifdef VARIANT_UNDEF_HOST
#  undef __host__
#  undef VARIANT_UNDEF_HOST
#endif

#ifdef VARIANT_UNDEF_DEVICE
#  undef __device__
#  undef VARIANT_UNDEF_DEVICE
#endif

