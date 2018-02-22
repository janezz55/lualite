/*
** This is free and unencumbered software released into the public domain.

** Anyone is free to copy, modify, publish, use, compile, sell, or
** distribute this software, either in source code form or as a compiled
** binary, for any purpose, commercial or non-commercial, and by any
** means.

** In jurisdictions that recognize copyright laws, the author or authors
** of this software dedicate any and all copyright interest in the
** software to the public domain. We make this dedication for the benefit
** of the public at large and to the detriment of our heirs and
** successors. We intend this dedication to be an overt act of
** relinquishment in perpetuity of all present and future rights to this
** software under copyright law.

** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
** EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
** MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
** IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
** OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
** ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
** OTHER DEALINGS IN THE SOFTWARE.

** For more information, please refer to <http://unlicense.org/>
*/

#ifndef LUALITE_HPP
# define LUALITE_HPP
# pragma once

#if __cplusplus < 201402L
# error "You need a C++14 compiler to use lualite"
#endif // __cplusplus

#include <cassert>

#include <cstring>

#include <type_traits>

#include <unordered_map>

#include <unordered_set>

#include <vector>

#ifndef LUALITE_NO_STD_CONTAINERS

#include <array>

#include <deque>

#include <forward_list>

#include <list>

#include <map>

#include <set>

#include <string>

#include <tuple>

#include <utility>

#endif // LUALITE_NO_STD_CONTAINERS

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

namespace lualite
{

#ifndef LLFUNC
# define LLFUNC(f) decltype(&f),&f
#endif // LLFUNC

struct any { };

class scope;

template <class C> class class_;

static constexpr auto const default_nrec = 10;

namespace
{

template <typename T>
using is_function_pointer =
  std::integral_constant<bool,
    std::is_pointer<T>{} &&
    std::is_function<std::remove_pointer_t<T>>{}
  >;

template <typename T>
using is_nc_reference =
  std::integral_constant<bool,
    std::is_reference<T>{} &&
    !std::is_const<std::remove_reference_t<T>>{}
  >;

struct swallow
{
  template <typename ...T>
  constexpr explicit swallow(T&& ...) noexcept
  {
  }
};

// key is at the top of the stack
inline void rawgetfield(lua_State* const L, int const index,
  char const* const key) noexcept
{
  auto const i(lua_absindex(L, index));
  lua_pushstring(L, key);
  lua_rawget(L, i);
}

// value is at the top of the stack, key is shifted below the top
inline void rawsetfield(lua_State* const L, int const index,
  char const* const key) noexcept
{
  auto const i(lua_absindex(L, index));
  lua_pushstring(L, key);
  lua_insert(L, -2);
  lua_rawset(L, i);
}

constexpr inline auto hash(char const* s, std::size_t h = {}) noexcept
{
  while (*s)
  {
    h ^= (h << 5) + (h >> 2) + static_cast<unsigned char>(*s++);
  }

  return h;
}

struct str_eq
{
  bool operator()(char const* const s1, char const* const s2) const noexcept
  {
    return !std::strcmp(s1, s2);
  }
};

struct str_hash
{
  constexpr std::size_t operator()(char const* s) const noexcept
  {
    return hash(s);
  }
};

template <typename T>
class scope_exit
{
  T const f_;

public:
  explicit scope_exit(T&& f) noexcept : f_(std::forward<T>(f))
  {
    static_assert(noexcept(f_()), "throwing functors are unsupported");
  }

  ~scope_exit() noexcept { f_(); }
};

template <typename T>
inline scope_exit<T> make_scope_exit(T&& f) noexcept
{
  return scope_exit<T>(std::forward<T>(f));
}

}

enum property_type : unsigned
{
  BOOLEAN,
  INTEGER,
  NUMBER,
  STRING,
  OTHER
};

struct constant_info_type
{
  enum property_type type;

  union
  {
    bool boolean;
    lua_Integer integer;
    lua_Number number;
    char const* string;
  } u;
};

using constants_type = std::vector<std::pair<char const* const,
  struct constant_info_type> >;

struct func_info_type
{
  char const* const name;

  lua_CFunction const callback;
};

using map_member_info_type = lua_CFunction;

using member_info_type = func_info_type;

template <class C>
int getter(lua_State* const L)
{
  assert(2 == lua_gettop(L));
  auto const i(lualite::class_<C>::getters().find(lua_tostring(L, 2)));

  if (lualite::class_<C>::getters().end() == i)
  {
    return {};
  }
  else
  {
    auto const uvi(lua_upvalueindex(2));

    void* p(lua_touserdata(L, uvi));
    auto const q(p);

    for (auto const f: std::get<0>(i->second))
    {
      p = f(p);
    }

    lua_pushlightuserdata(L, p);
    lua_replace(L, uvi);

    auto const se(
      make_scope_exit(
        [&]() noexcept {
          lua_pushlightuserdata(L, q); lua_replace(L, uvi);
        }
      )
    );

    return std::get<1>(i->second)(L);
  }
}

template <class C>
int setter(lua_State* const L)
{
  assert(3 == lua_gettop(L));
  auto const i(lualite::class_<C>::setters().find(lua_tostring(L, 2)));

  if (lualite::class_<C>::setters().end() != i)
  {
    auto const uvi(lua_upvalueindex(2));

    void* p(lua_touserdata(L, uvi));
    auto const q(p);

    for (auto const f: std::get<0>(i->second))
    {
      p = f(p);
    }

    lua_pushlightuserdata(L, p);
    lua_replace(L, uvi);

    auto const se(make_scope_exit([&]() noexcept {
      lua_pushlightuserdata(L, q); lua_replace(L, uvi);})
    );

    std::get<1>(i->second)(L);
  }
  // else do nothing

  return {};
}

template <class C>
inline void create_wrapper_table(lua_State* const L, C* const instance)
{
  auto const uvi(lua_upvalueindex(1));

  lua_pushvalue(L, uvi);

  if (lua_isnil(L, -1))
  {
    lua_createtable(L, 0, default_nrec);

    for (auto& mi: lualite::class_<C>::defs())
    {
      assert(lua_istable(L, -1));

      lua_pushnil(L);

      void* p(instance);

      for (auto const f: mi.first)
      {
        p = f(p);
      }

      lua_pushlightuserdata(L, p);
      lua_pushcclosure(L, mi.second.callback, 2);

      rawsetfield(L, -2, mi.second.name);
    }

    // metatable
    assert(lua_istable(L, -1));
    lua_createtable(L, 0, 2);

    // getters
    assert(lua_istable(L, -1));

    lua_pushnil(L);
    lua_pushlightuserdata(L, instance);

    lua_pushcclosure(L, getter<C>, 2);

    rawsetfield(L, -2, "__index");

    // setters
    assert(lua_istable(L, -1));

    lua_pushnil(L);
    lua_pushlightuserdata(L, instance);

    lua_pushcclosure(L, setter<C>, 2);

    rawsetfield(L, -2, "__newindex");

    lua_setmetatable(L, -2);

    lua_copy(L, -1, uvi);
  }
  // else do nothing

  assert(lua_istable(L, uvi));
  assert(lua_istable(L, -1));
}

template <typename T>
inline std::enable_if_t<
  std::is_floating_point<std::decay_t<T>>{} &&
  !is_nc_reference<T>{},
  int
>
set(lua_State* const L, T&& v) noexcept
{
  lua_pushnumber(L, v);

  return 1;
}

template <typename T>
inline std::enable_if_t<
  std::is_integral<std::decay_t<T>>{} &&
  !std::is_same<std::decay_t<T>, bool>{} &&
  !is_nc_reference<T>{},
  int
>
set(lua_State* const L, T&& v) noexcept
{
  lua_pushinteger(L, v);

  return 1;
}

template <typename T>
inline std::enable_if_t<
  std::is_same<std::decay_t<T>, bool>{} &&
  !is_nc_reference<T>{},
  int
>
set(lua_State* const L, T&& v) noexcept
{
  lua_pushboolean(L, v);

  return 1;
}

template <typename T>
inline std::enable_if_t<
  std::is_same<std::decay_t<T>, char const*>{} &&
  !is_nc_reference<T>{},
  int
>
set(lua_State* const L, T&& v) noexcept
{
  lua_pushstring(L, v);

  return 1;
}

template <typename T>
inline std::enable_if_t<
  std::is_same<std::decay_t<T>, void const*>{} &&
  !is_nc_reference<T>{},
  int
>
set(lua_State* const L, T&& v) noexcept
{
  lua_pushlightuserdata(L, const_cast<void*>(v));

  return 1;
}

template <typename T>
inline std::enable_if_t<
  std::is_pointer<T>{} &&
  !std::is_const<std::remove_pointer_t<T>>{} &&
  !std::is_class<std::remove_pointer_t<T>>{},
  int
>
set(lua_State* const L, T&& v) noexcept
{
  lua_pushlightuserdata(L, v);

  return 1;
}

template <typename T>
inline std::enable_if_t<
  is_nc_reference<T>{} &&
  !std::is_class<std::decay_t<T>>{},
  int
>
set(lua_State* const L, T&& v) noexcept
{
  lua_pushlightuserdata(L, &v);

  return 1;
}

template <typename T>
inline std::enable_if_t<
  std::is_pointer<std::decay_t<T>>{} &&
  !std::is_const<std::remove_pointer_t<T>>{} &&
  std::is_class<std::remove_pointer_t<std::decay_t<T>>>{},
  int
>
set(lua_State* const L, T&& v) noexcept
{
  create_wrapper_table(L, v);

  return 1;
}

template <typename T>
inline std::enable_if_t<
  is_nc_reference<T>{} &&
  std::is_class<std::decay_t<T>>{},
  int
>
set(lua_State* const L, T&& v) noexcept
{
  create_wrapper_table(L, &v);

  return 1;
}

template <typename T>
inline std::enable_if_t<
  std::is_same<std::decay_t<T>, any>{} &&
  !is_nc_reference<T>{},
  int
>
set(lua_State* const, T&&) noexcept
{
  return 1;
}

template <int I, typename T>
inline std::enable_if_t<
  std::is_floating_point<std::decay_t<T>>{} &&
  !is_nc_reference<T>{},
  std::decay_t<T>
>
get(lua_State* const L) noexcept
{
  assert(lua_isnumber(L, I));
  return lua_tonumber(L, I);
}

template <int I, typename T>
inline std::enable_if_t<
  std::is_integral<std::decay_t<T>>{} &&
  !std::is_same<std::decay_t<T>, bool>{} &&
  !is_nc_reference<T>{},
  std::decay_t<T>
>
get(lua_State* const L) noexcept
{
  assert(lua_isnumber(L, I));
  return lua_tointeger(L, I);
}

template <int I, typename T>
inline std::enable_if_t<
  std::is_same<std::decay_t<T>, bool>{} &&
  !is_nc_reference<T>{},
  std::decay_t<T>
>
get(lua_State* const L) noexcept
{
  assert(lua_isboolean(L, I));
  return lua_toboolean(L, I);
}

template <int I, typename T>
inline std::enable_if_t<
  std::is_same<std::decay_t<T>, char const*>{} &&
  !is_nc_reference<T>{},
  std::decay_t<T>
>
get(lua_State* const L) noexcept
{
  assert(lua_isstring(L, I));
  return lua_tostring(L, I);
}

template <int I, typename T>
inline std::enable_if_t<
  std::is_pointer<T>{} &&
  !std::is_same<std::decay_t<T>, char const*>{},
  std::decay_t<T>
>
get(lua_State* const L) noexcept
{
  assert(lua_islightuserdata(L, I));
  return static_cast<T>(lua_touserdata(L, I));
}

template <int I, typename T>
inline std::enable_if_t<
  is_nc_reference<T>{},
  T
>
get(lua_State* const L) noexcept
{
  assert(lua_islightuserdata(L, I));
  return *static_cast<std::decay_t<T>*>(lua_touserdata(L, I));
}

template <int I, typename T>
inline std::enable_if_t<
  std::is_same<std::remove_const_t<T>, any>{},
  T
>
get(lua_State* const) noexcept
{
  return {};
}

#ifndef LUALITE_NO_STD_CONTAINERS

template <typename>
struct is_std_pair : std::false_type { };

template <class T1, class T2>
struct is_std_pair<std::pair<T1, T2> > : std::true_type { };

template <typename>
struct is_std_array : std::false_type { };

template <typename T, std::size_t N>
struct is_std_array<std::array<T, N> > : std::true_type { };

template <typename>
struct is_std_deque : std::false_type { };

template <typename T, class Alloc>
struct is_std_deque<std::deque<T, Alloc> > : std::true_type { };

template <typename>
struct is_std_forward_list : std::false_type { };

template <typename T, class Alloc>
struct is_std_forward_list<std::forward_list<T, Alloc> > : std::true_type { };

template <typename>
struct is_std_list : std::false_type { };

template <typename T, class Alloc>
struct is_std_list<std::list<T, Alloc> > : std::true_type { };

template <typename>
struct is_std_map : std::false_type { };

template <class Key, class T, class Compare, class Alloc>
struct is_std_map<std::map<Key, T, Compare, Alloc> > : std::true_type { };

template <typename>
struct is_std_set : std::false_type { };

template <class Key, class Compare, class Alloc>
struct is_std_set<std::set<Key, Compare, Alloc> > : std::true_type { };

template <typename>
struct is_std_unordered_map : std::false_type { };

template <class Key, class T, class Hash, class P, class Alloc>
struct is_std_unordered_map<std::unordered_map<Key, T, Hash, P, Alloc> > :
  std::true_type { };

template <typename>
struct is_std_unordered_set : std::false_type { };

template <class Key, class Hash, class Equal, class Alloc>
struct is_std_unordered_set<std::unordered_set<Key, Hash, Equal, Alloc> > :
  std::true_type { };

template <typename>
struct is_std_tuple : std::false_type { };

template <class ...Types>
struct is_std_tuple<std::tuple<Types...> > : std::true_type { };

template <typename>
struct is_std_vector : std::false_type { };

template <typename T, class Alloc>
struct is_std_vector<std::vector<T, Alloc> > : std::true_type { };

template <typename T>
inline std::enable_if_t<
  std::is_same<std::decay_t<T>, std::string>{} &&
  !is_nc_reference<T>{},
  int
>
set(lua_State* const L, T&& s) noexcept
{
  lua_pushlstring(L, s.c_str(), s.size());

  return 1;
}

template <typename C>
inline std::enable_if_t<
  is_std_pair<std::decay_t<C>>{} &&
  !is_nc_reference<C>{},
  int
>
set(lua_State* const L, C&& p) noexcept(
  noexcept(set(L, p.first), set(L, p.second))
)
{
  set(L, p.first);
  set(L, p.second);

  return 2;
}

template <typename ...Types, std::size_t ...I>
inline void set_tuple_result(lua_State* const L,
  std::tuple<Types...> const& t, std::index_sequence<I...> const) noexcept(
    noexcept(swallow{(set(L, std::get<I>(t)), 0)...})
  )
{
  swallow{(set(L, std::get<I>(t)), 0)...};
}

template <typename C>
inline std::enable_if_t<
  is_std_tuple<std::decay_t<C>>{} &&
  !is_nc_reference<C>{},
  int
>
set(lua_State* const L, C&& t) noexcept(
  noexcept(
    set_tuple_result(L, t,
      std::make_index_sequence<std::tuple_size<C>{}>()
    )
  )
)
{
  using result_type = std::decay_t<C>;

  set_tuple_result(L,
    t,
    std::make_index_sequence<std::tuple_size<C>{}>()
  );

  return std::tuple_size<result_type>{};
}

template <typename C>
inline std::enable_if_t<
  (is_std_array<std::decay_t<C>>{} ||
  is_std_deque<std::decay_t<C>>{} ||
  is_std_forward_list<std::decay_t<C>>{} ||
  is_std_list<std::decay_t<C>>{} ||
  is_std_vector<std::decay_t<C>>{} ||
  is_std_set<std::decay_t<C>>{} ||
  is_std_unordered_set<std::decay_t<C>>{}) &&
  !is_nc_reference<C>{},
  int
>
set(lua_State* const L, C&& c)
{
  lua_createtable(L, c.size(), 0);

  int j{};

  auto const cend(c.cend());

  for (auto i(c.cbegin()); i != cend; ++i)
  {
    set(L, *i);

    lua_rawseti(L, -2, ++j);
  }

  return 1;
}

template <typename C>
inline std::enable_if_t<
  (is_std_map<std::decay_t<C>>{} ||
  is_std_unordered_map<std::decay_t<C>>{}) &&
  !is_nc_reference<C>{},
  int
>
set(lua_State* const L, C&& m)
{
  lua_createtable(L, 0, m.size());

  auto const cend(m.cend());

  for (auto i(m.cbegin()); i != cend; ++i)
  {
    set(L, i->first);
    set(L, i->second);

    lua_rawset(L, -3);
  }

  return 1;
}

template <int I, class C>
inline std::enable_if_t<
  std::is_same<std::decay_t<C>, std::string>{} &&
  !is_nc_reference<C>{},
  std::decay_t<C>
>
get(lua_State* const L)
{
  assert(lua_isstring(L, I));

  std::size_t len;

  auto const s(lua_tolstring(L, I, &len));

  return {s, len};
}

template<int I, class C>
inline std::enable_if_t<
  is_std_pair<std::decay_t<C>>{} &&
  !is_nc_reference<C>{},
  std::decay_t<C>
>
get(lua_State* const L)
{
  assert(lua_istable(L, I));
 
  using result_type = std::decay_t<C>;

  lua_rawgeti(L, -1, 1);
  lua_rawgeti(L, -2, 2);

  result_type const result(
    get<-2, typename result_type::first_type>(L),
    get<-1, typename result_type::second_type>(L)
  );

  lua_pop(L, 2);

  return result;
}

template <std::size_t O, class C, std::size_t ...I>
inline C get_tuple_arg(lua_State* const L,
  std::index_sequence<I...> const) noexcept(
    noexcept(std::make_tuple(get<int(I - sizeof...(I)),
      std::tuple_element_t<I, C>>(L)...)
    )
  )
{
  swallow{(lua_rawgeti(L, O, I + 1), 0)...};

  C result(std::make_tuple(get<int(I - sizeof...(I)),
    std::tuple_element_t<I, C>>(L)...));

  lua_pop(L, int(sizeof...(I)));

  return result;
}

template <int I, class C>
inline std::enable_if_t<
  is_std_tuple<std::decay_t<C>>{} &&
  !is_nc_reference<C>{},
  std::decay_t<C>
>
get(lua_State* const L) noexcept(
  noexcept(get_tuple_arg<I,
    std::decay_t<C>>(L,
      std::make_index_sequence<
        std::tuple_size<std::decay_t<C>>{}
      >()
    )
  )
)
{
  assert(lua_istable(L, I));

  using result_type = std::decay_t<C>;

  return get_tuple_arg<I, result_type>(L,
    std::make_index_sequence<std::tuple_size<result_type>{}>()
  );
}

template<int I, class C>
inline std::enable_if_t<
  is_std_array<std::decay_t<C>>{} &&
  !is_nc_reference<C>{},
  std::decay_t<C>
>
get(lua_State* const L)
{
  assert(lua_istable(L, I));

  using result_type = std::decay_t<C>;
  result_type result;

  auto const len(std::min(lua_rawlen(L, I), lua_Unsigned(result.size())));

  for (decltype(lua_rawlen(L, I)) i{}; i != len; ++i)
  {
    lua_rawgeti(L, I, i + 1);

    result[i] = get<-1, typename result_type::value_type>(L);
  }

  lua_pop(L, len);

  return result;
}

template <int I, class C>
inline std::enable_if_t<
  (is_std_deque<std::decay_t<C>>{} ||
  is_std_forward_list<std::decay_t<C>>{} ||
  is_std_list<std::decay_t<C>>{}) &&
  !is_nc_reference<C>{},
  std::decay_t<C>
>
get(lua_State* const L)
{
  assert(lua_istable(L, I));

  using result_type = std::decay_t<C>;
  result_type result;

  auto const len(lua_rawlen(L, I));

  for (auto i(len); i; --i)
  {
    lua_rawgeti(L, I, i);

    result.emplace_front(get<-1, typename result_type::value_type>(L));
  }

  lua_pop(L, len);

  return result;
}

template <int I, class C>
inline std::enable_if_t<
  is_std_vector<std::decay_t<C>>{} &&
  !is_nc_reference<C>{},
  std::decay_t<C>
>
get(lua_State* const L)
{
  assert(lua_istable(L, I));

  using result_type = std::decay_t<C>;
  result_type result;

  auto const cend(lua_rawlen(L, I) + 1);

  result.reserve(cend - 1);

  for (decltype(lua_rawlen(L, I)) i(1); i != cend; ++i)
  {
    lua_rawgeti(L, I, i);

    result.emplace_back(get<-1, typename result_type::value_type>(L));
  }

  lua_pop(L, cend - 1);

  return result;
}

template <int I, class C>
inline std::enable_if_t<
  (is_std_map<std::decay_t<C>>{} ||
  is_std_unordered_map<std::decay_t<C>>{}) &&
  !is_nc_reference<C>{},
  std::decay_t<C>
>
get(lua_State* const L)
{
  assert(lua_istable(L, I));

  using result_type = std::decay_t<C>;
  result_type result;

  lua_pushnil(L);

  while (lua_next(L, I))
  {
    result.emplace(get<-2, typename result_type::key_type>(L),
      get<-1, typename result_type::mapped_type>(L)
    );

    lua_pop(L, 1);
  }

  return result;
}

template <int I, class C>
inline std::enable_if_t<
  (is_std_set<std::decay_t<C>>{} ||
  is_std_unordered_set<std::decay_t<C>>{}) &&
  !is_nc_reference<C>{},
  std::decay_t<C>
>
get(lua_State* const L)
{
  assert(lua_istable(L, I));

  using result_type = std::decay_t<C>;
  result_type result;

  auto const end(lua_rawlen(L, I) + 1);

  for (decltype(lua_rawlen(L, I)) i(1); i != end; ++i)
  {
    lua_rawgeti(L, I, i);

    result.emplace(get<-1, typename result_type::value_type>(L));
  }

  lua_pop(L, end - 1);

  return result;
}

#endif // LUALITE_NO_STD_CONTAINERS

template <class C>
int default_finalizer(lua_State* const L)
  noexcept(noexcept(std::declval<C>().~C()))
{
  delete static_cast<C*>(lua_touserdata(L, lua_upvalueindex(1)));

  return {};
}

template <std::size_t O, typename C, typename ...A, std::size_t ...I>
inline std::enable_if_t<bool(!sizeof...(A)), C*>
forward(lua_State* const, std::index_sequence<I...> const) noexcept(
  noexcept(C())
)
{
  return new C();
}

template <std::size_t O, typename C, typename ...A, std::size_t ...I>
inline std::enable_if_t<bool(sizeof...(A)), C*>
forward(lua_State* const L, std::index_sequence<I...> const) noexcept(
  noexcept(C(get<I + O, A>(L)...))
)
{
  return new C(get<I + O, A>(L)...);
}

template <std::size_t O, class C, class ...A>
int constructor_stub(lua_State* const L)
  noexcept(noexcept(
    forward<O, C, A...>(L, std::make_index_sequence<sizeof...(A)>()))
  )
{
  assert(sizeof...(A) == lua_gettop(L));

  auto const instance(forward<O, C, A...>(L,
    std::make_index_sequence<sizeof...(A)>())
  );

  // table
  lua_createtable(L, 0, default_nrec);

  for (auto& mi: lualite::class_<C>::defs())
  {
    assert(lua_istable(L, -1));

    void* p(instance);

    for (auto const f: mi.first)
    {
      p = f(p);
    }

    lua_pushnil(L);
    lua_pushlightuserdata(L, p);
    lua_pushcclosure(L, mi.second.callback, 2);

    rawsetfield(L, -2, mi.second.name);
  }

  // metatable
  assert(lua_istable(L, -1));
  lua_createtable(L, 0, 3);

  // gc
  assert(lua_istable(L, -1));
  lua_pushlightuserdata(L, instance);

  lua_pushcclosure(L, default_finalizer<C>, 1);

  rawsetfield(L, -2, "__gc");

  // getters
  assert(lua_istable(L, -1));

  lua_pushnil(L);
  lua_pushlightuserdata(L, instance);

  lua_pushcclosure(L, getter<C>, 2);

  rawsetfield(L, -2, "__index");

  // setters
  assert(lua_istable(L, -1));

  lua_pushnil(L);
  lua_pushlightuserdata(L, instance);

  lua_pushcclosure(L, setter<C>, 2);

  rawsetfield(L, -2, "__newindex");

  lua_setmetatable(L, -2);
  assert(lua_istable(L, -1));

  return 1;
}

template <std::size_t O, typename R, typename ...A, std::size_t ...I>
inline std::enable_if_t<bool(!sizeof...(A)), R>
forward(lua_State* const, R (* const f)(A...),
  std::index_sequence<I...> const) noexcept(noexcept((*f)()))
{
  return (*f)();
}

template <std::size_t O, typename R, typename ...A, std::size_t ...I>
inline std::enable_if_t<bool(sizeof...(A)), R>
forward(lua_State* const L, R (* const f)(A...),
  std::index_sequence<I...> const) noexcept(
  noexcept((*f)(get<I + O, A>(L)...))
)
{
  return (*f)(get<I + O, A>(L)...);
}

template <typename FP, FP fp, std::size_t O, class R, class ...A>
inline std::enable_if_t<std::is_void<R>{}, int>
func_stub(lua_State* const L) noexcept(
  noexcept(
    forward<O, R, A...>(L, fp, std::make_index_sequence<sizeof...(A)>())
  )
)
{
  assert(sizeof...(A) == lua_gettop(L));

  forward<O, R, A...>(L, fp, std::make_index_sequence<sizeof...(A)>());

  return {};
}

template <typename FP, FP fp, std::size_t O, class R, class ...A>
inline std::enable_if_t<!std::is_void<R>{}, int>
func_stub(lua_State* const L) noexcept(
  noexcept(
    set(L, forward<O, R, A...>(L, fp,
    std::make_index_sequence<sizeof...(A)>()))
  )
)
{
  return set(L, forward<O, R, A...>(L, fp,
    std::make_index_sequence<sizeof...(A)>()));
}

template <typename FP, FP fp, class R>
inline std::enable_if_t<!std::is_void<R>{}, int>
vararg_func_stub(lua_State* const L) noexcept(noexcept(set(fp(L))))
{
  return set(fp(L));
}

template <typename FP, FP fp, class R>
inline std::enable_if_t<std::is_void<R>{}, int>
vararg_func_stub(lua_State* const L) noexcept(noexcept(fp(L)))
{
  fp(L);

  return {};
}

template <std::size_t O, typename C, typename R, typename ...A,
  std::size_t ...I>
inline std::enable_if_t<bool(!sizeof...(A)), R>
forward(lua_State* const, C* const c,
  R (C::* const ptr_to_member)(A...) const,
  std::index_sequence<I...> const) noexcept(
  noexcept((c->*ptr_to_member)())
)
{
  return (c->*ptr_to_member)();
}

template <std::size_t O, typename C, typename R, typename ...A,
  std::size_t ...I>
inline std::enable_if_t<bool(!sizeof...(A)), R>
forward(lua_State* const, C* const c,
  R (C::* const ptr_to_member)(A...),
  std::index_sequence<I...> const) noexcept(
  noexcept((c->*ptr_to_member)())
)
{
  return (c->*ptr_to_member)();
}

template <std::size_t O, typename C, typename R, typename ...A,
  std::size_t ...I>
inline std::enable_if_t<bool(sizeof...(A)), R>
forward(lua_State* const L, C* const c,
  R (C::* const ptr_to_member)(A...) const,
  std::index_sequence<I...> const) noexcept(
  noexcept((c->*ptr_to_member)(get<I + O, A>(L)...))
)
{
  return (c->*ptr_to_member)(get<I + O, A>(L)...);
}

template <std::size_t O, typename C, typename R,
  typename ...A, std::size_t ...I>
inline std::enable_if_t<bool(sizeof...(A)), R>
forward(lua_State* const L, C* const c,
  R (C::* const ptr_to_member)(A...), std::index_sequence<I...> const)
  noexcept(noexcept((c->*ptr_to_member)(get<I + O, A>(L)...)))
{
  return (c->*ptr_to_member)(get<I + O, A>(L)...);
}

template <typename FP, FP fp, std::size_t O, class C, class R, class ...A>
inline std::enable_if_t<!std::is_void<R>{}, int>
member_stub(lua_State* const L) noexcept(
  noexcept(set(L,
    forward<O, C, R, A...>(L,
      static_cast<C*>(lua_touserdata(L, lua_upvalueindex(2))),
      fp,
      std::make_index_sequence<sizeof...(A)>()))
  )
)
{
//std::cout << lua_gettop(L) << " " << sizeof...(A) + O - 1 << std::endl;
  assert(sizeof...(A) + O - 1 == lua_gettop(L));

  return set(L,
    forward<O, C, R, A...>(L,
      static_cast<C*>(lua_touserdata(L, lua_upvalueindex(2))),
      fp,
      std::make_index_sequence<sizeof...(A)>()));
}

template <typename FP, FP fp, std::size_t O, class C, class R, class ...A>
inline std::enable_if_t<std::is_void<R>{}, int>
member_stub(lua_State* const L) noexcept(
  noexcept(forward<O, C, R, A...>(L,
    static_cast<C*>(lua_touserdata(L, lua_upvalueindex(2))),
    fp,
    std::make_index_sequence<sizeof...(A)>())
  )
)
{
  assert(sizeof...(A) + O - 1 == lua_gettop(L));

  forward<O, C, R, A...>(L,
    static_cast<C*>(lua_touserdata(L, lua_upvalueindex(2))),
    fp,
    std::make_index_sequence<sizeof...(A)>());

  return {};
}

template <typename FP, FP fp, class C, class R>
inline std::enable_if_t<!std::is_void<R>{}, int>
vararg_member_stub(lua_State* const L) noexcept(
  noexcept(set(L, (static_cast<C*>(
    lua_touserdata(L, lua_upvalueindex(2)))->*fp)(L))
  )
)
{
  return set(L,
    (static_cast<C*>(lua_touserdata(L, lua_upvalueindex(2)))->*fp)(L)
  );
}

template <typename FP, FP fp, class C, class R>
inline std::enable_if_t<std::is_void<R>{}, int>
vararg_member_stub(lua_State* const L) noexcept(
  noexcept(
    (static_cast<C*>(lua_touserdata(L, lua_upvalueindex(2)))->*fp)(L)
  )
)
{
  (static_cast<C*>(lua_touserdata(L, lua_upvalueindex(2)))->*fp)(L);

  return {};
}

template <typename FP, FP fp, std::size_t O, class R, class ...A>
constexpr inline lua_CFunction func_stub(R (*)(A...)) noexcept
{
  return &func_stub<FP, fp, O, R, A...>;
}

template <typename FP, FP fp, std::size_t O, class R, class C, class ...A>
constexpr inline lua_CFunction member_stub(R (C::*)(A...)) noexcept
{
  return &member_stub<FP, fp, O, C, R, A...>;
}

template <typename FP, FP fp, std::size_t O, class R, class C, class ...A>
constexpr inline lua_CFunction member_stub(R (C::*)(A...) const) noexcept
{
  return &member_stub<FP, fp, O, C, R, A...>;
}

template <typename FP, FP fp, class R, class C>
constexpr inline lua_CFunction vararg_member_stub(R (C::*)(lua_State*)) noexcept
{
  return &vararg_member_stub<FP, fp, C, R>;
}

template <typename FP, FP fp, class R, class C>
constexpr inline lua_CFunction vararg_member_stub(R (C::*)(lua_State*) const) noexcept
{
  return &vararg_member_stub<FP, fp, C, R>;
}

template <typename R>
constexpr inline enum property_type get_property_type() noexcept
{
  if (std::is_same<std::decay_t<R>, bool>{})
  {
    return BOOLEAN;
  }
  else if (std::is_integral<R>{})
  {
    return INTEGER;
  }
  else if (std::is_floating_point<R>{})
  {
    return NUMBER;
  }
  else if (std::is_same<R, std::string const&>{} ||
    std::is_same<std::decay_t<R>, char const*>{})
  {
    return STRING;
  }
  else
  {
    return OTHER;
  }
}

template <typename FP, FP fp, class R, class C, class ...A>
constexpr inline auto get_property_type( R (C::* const)(A...)) noexcept
{
  return get_property_type<R>();
}

template <typename FP, FP fp, class R, class C, class ...A>
constexpr inline auto get_property_type(R (C::* const)(A...) const) noexcept
{
  return get_property_type<R>();
}

template <typename ...A>
inline void call(lua_State* const L, int const nresults, A&& ...args)
  noexcept(noexcept(swallow{(set(L, std::forward<A>(args)))...}))
{
  int ac{};

  swallow{
    (ac += set(L, std::forward<A>(args)))...
  };
  assert(ac >= int(sizeof...(A)));

  lua_call(L, ac, nresults);
}

class scope
{
public:
  scope(char const* const name) : name_(name) { }

  template <typename ...A>
  scope(char const* const name, A&&... args) : name_(name)
  {
    swallow((args.set_parent_scope(this), 0)...);
  }

  scope(scope const&) = delete;

  scope& operator=(scope const&) = delete;

  template <typename T>
  std::enable_if_t<
    std::is_same<std::decay_t<T>, bool>{},
    scope&
  >
  constant(char const* const name, T const value)
  {
    struct constant_info_type const ci {
      BOOLEAN,
      {value}
    };

    constants_.emplace_back(name, ci);

    return *this;
  }

  template <typename T>
  std::enable_if_t<
    std::is_floating_point<std::decay_t<T>>{},
    scope&
  >
  constant(char const* const name, T const value)
  {
    struct constant_info_type ci;
    ci.type = NUMBER;
    ci.u.number = value;

    constants_.emplace_back(name, ci);

    return *this;
  }

  template <typename T>
  std::enable_if_t<
    std::is_integral<std::decay_t<T>>{} &&
    !std::is_same<std::decay_t<T>, bool>{},
    scope&
  >
  constant(char const* const name, T const value)
  {
    struct constant_info_type ci;
    ci.type = INTEGER;
    ci.u.integer = value;

    constants_.emplace_back(name, ci);

    return *this;
  }

  scope& constant(char const* const name, char const* const value)
  {
    struct constant_info_type ci;
    ci.type = STRING;
    ci.u.string = value;

    constants_.emplace_back(name, ci);

    return *this;
  }

  template <typename FP, FP fp>
  scope& def(char const* const name)
  {
    push_function<FP, fp>(name, fp);

    return *this;
  }

  scope& enum_(char const* const name, lua_Integer const value)
  {
    constant(name, value);

    return *this;
  }

  template <typename FP, FP fp>
  scope& vararg_def(char const* const name)
  {
    push_vararg_function<FP, fp>(name, fp);

    return *this;
  }

protected:
  virtual void apply(lua_State* const L)
  {
    struct S
    {
      static void push_constant(lua_State* const L,
        decltype(constants_)::const_reference i)
      {
        switch (i.second.type)
        {
          default:
            assert(0);

          case BOOLEAN:
            lua_pushboolean(L, i.second.u.boolean);

            break;

          case INTEGER:
            lua_pushinteger(L, i.second.u.integer);

            break;

          case NUMBER:
            lua_pushnumber(L, i.second.u.number);

            break;

          case STRING:
            lua_pushstring(L, i.second.u.string);
        }
      }
    };

    if (parent_scope_)
    {
      scope::get_scope(L);
      assert(lua_istable(L, -1));

      for (auto& i: constants_)
      {
        assert(lua_istable(L, -1));
        S::push_constant(L, i);

        rawsetfield(L, -2, i.first);
      }

      for (auto& i: functions_)
      {
        assert(lua_istable(L, -1));

        lua_pushnil(L);

        lua_pushcclosure(L, i.callback, 1);

        rawsetfield(L, -2, i.name);
      }

      lua_pop(L, 1);
    }
    else
    {
      for (auto& i: constants_)
      {
        assert(lua_istable(L, -1));
        S::push_constant(L, i);

        lua_setglobal(L, i.first);
      }

      for (auto& i: functions_)
      {
        lua_pushnil(L);
        lua_pushcclosure(L, i.callback, 1);

        lua_setglobal(L, i.name);
      }

      auto next(next_);

      while (next)
      {
        next->apply(L);

        next = next->next_;
      }
    }

    assert(!lua_gettop(L));
  }

  void append_child_scope(scope* const instance)
  {
    if (next_)
    {
      auto next(next_);

      while (next->next_)
      {
        next = next->next_;
      }

      next->next_ = instance;
    }
    else
    {
      next_ = instance;
    }
  }

  void set_parent_scope(scope* const parent_scope)
  {
    parent_scope->append_child_scope(this);

    parent_scope_ = parent_scope;
  }

  void get_scope(lua_State* const L)
  {
    if (parent_scope_)
    {
      assert(name_);
      parent_scope_->get_scope(L);

      if (scope_create_)
      {
        scope_create_ = false;

        if (lua_gettop(L))
        {
          assert(lua_istable(L, -1));
          lua_createtable(L, 0, default_nrec);
          rawsetfield(L, -2, name_);
        }
        else
        {
          lua_createtable(L, 0, default_nrec);
          lua_setglobal(L, name_);
        }
      }
      // else do nothing

      if (lua_gettop(L) && lua_istable(L, -1))
      {
        luaL_getsubtable(L, -1, name_);
        lua_remove(L, -2);
      }
      else
      {
        lua_getglobal(L, name_);
      }
    }
    else if (name_)
    {
      if (scope_create_)
      {
        scope_create_ = false;

        lua_createtable(L, 0, default_nrec);
        lua_setglobal(L, name_);
      }
      // else do nothing

      lua_getglobal(L, name_);
    }
    // else do nothing
  }

protected:
  char const* const name_;

  scope* parent_scope_{};

  std::vector<func_info_type> functions_;

private:
  template <typename FP, FP fp, typename R, typename ...A>
  void push_function(char const* const name, R (* const)(A...))
  {
    functions_.push_back({name, func_stub<FP, fp, 1, R, A...>});
  }

  template <typename FP, FP fp, typename R>
  void push_vararg_function(char const* const name, R (* const)(lua_State*))
  {
    functions_.push_back({name, vararg_func_stub<FP, fp, R>});
  }

private:
  friend class module;

  constants_type constants_;

  scope* next_{};

  bool scope_create_{true};
};

class module : public scope
{
  lua_State* const L_;

public:
  template <typename ...A>
  module(lua_State* const L, A&&... args) :
    scope(nullptr),
    L_(L)
  {
    swallow((args.set_parent_scope(this), 0)...);

    scope::apply(L);
  }

  template <typename ...A>
  module(lua_State* const L, char const* const name, A&&... args) :
    scope(name),
    L_(L)
  {
    swallow((args.set_parent_scope(this), 0)...);

    scope::apply(L);
  }

  template <typename T>
  std::enable_if_t<
    std::is_same<std::decay_t<T>, bool>{},
    module&
  >
  constant(char const* const name, T const value)
  {
    if (name_)
    {
      scope::get_scope(L_);
      assert(lua_istable(L_, -1));

      lua_pushboolean(L_, value);
      rawsetfield(L_, -2, name);

      lua_pop(L_, 1);
    }
    else
    {
      lua_pushboolean(L_, value);

      lua_setglobal(L_, name);
    }

    return *this;
  }

  template <typename T>
  std::enable_if_t<
    std::is_floating_point<std::decay_t<T>>{},
    module&
  >
  constant(char const* const name, T const value)
  {
    if (name_)
    {
      scope::get_scope(L_);
      assert(lua_istable(L_, -1));

      lua_pushnumber(L_, value);
      rawsetfield(L_, -2, name);

      lua_pop(L_, 1);
    }
    else
    {
      lua_pushnumber(L_, value);

      lua_setglobal(L_, name);
    }

    return *this;
  }

  template <typename T>
  std::enable_if_t<
    std::is_integral<std::decay_t<T>>{} &&
    !std::is_same<std::decay_t<T>, bool>{},
    module&
  >
  constant(char const* const name, T const value)
  {
    if (name_)
    {
      scope::get_scope(L_);
      assert(lua_istable(L_, -1));

      lua_pushinteger(L_, value);
      rawsetfield(L_, -2, name);

      lua_pop(L_, 1);
    }
    else
    {
      lua_pushinteger(L_, value);

      lua_setglobal(L_, name);
    }

    return *this;
  }

  module& constant(char const* const name, char const* const value)
  {
    if (name_)
    {
      scope::get_scope(L_);
      assert(lua_istable(L_, -1));

      lua_pushstring(L_, value);
      rawsetfield(L_, -2, name);

      lua_pop(L_, 1);
    }
    else
    {
      lua_pushstring(L_, value);

      lua_setglobal(L_, name);
    }

    return *this;
  }

  template <typename FP, FP fp>
  module& def(char const* const name)
  {
    if (name_)
    {
      scope::get_scope(L_);
      assert(lua_istable(L_, -1));

      push_function<FP, fp>(fp);

      rawsetfield(L_, -2, name);

      lua_pop(L_, 1);
    }
    else
    {
      push_function<FP, fp>(fp);

      lua_setglobal(L_, name);
    }

    return *this;
  }

  module& enum_(char const* const name, int const value)
  {
    return constant(name, lua_Number(value));
  }

  template <typename FP, FP fp>
  module& vararg_def(char const* const name)
  {
    if (name_)
    {
      scope::get_scope(L_);
      assert(lua_istable(L_, -1));

      push_vararg_function<FP, fp>(fp);

      rawsetfield(L_, -2, name);

      lua_pop(L_, 1);
    }
    else
    {
      push_vararg_function<FP, fp>(fp);

      lua_setglobal(L_, name);
    }

    return *this;
  }

private:
  template <typename FP, FP fp, typename R, typename ...A>
  void push_function(R (* const)(A...))
  {
    lua_pushnil(L_);
    lua_pushcclosure(L_, func_stub<FP, fp, 1, R, A...>, 1);
  }

  template <typename FP, FP fp, typename R>
  void push_vararg_function(R (* const)(lua_State*))
  {
    lua_pushnil(L_);
    lua_pushcclosure(L_, vararg_func_stub<FP, fp, R>, 1);
  }
};

using accessor_type = std::tuple<
  std::vector<void* (*)(void*) noexcept>,
  map_member_info_type,
  enum property_type
>;

using accessors_type = std::unordered_map<char const*,
  accessor_type,
  str_hash,
  str_eq
>;

using accessors_info_type = std::unordered_map<char const*,
  unsigned,
  str_hash,
  str_eq
>;

using defs_type = std::vector<
  std::pair<
    std::vector<void* (*)(void*) noexcept>,
    member_info_type
  >
>;

template <class C>
class class_ : public scope
{
  static char const* class_name_;

  static std::vector<bool(*)(char const*) noexcept> inherits_;

  static std::vector<func_info_type> constructors_;

  static defs_type defs_;

  static accessors_type getters_;
  static accessors_type setters_;

public:
  class_(char const* const name) : scope(name)
  {
    class_name_ = name;
  }

  template <typename T>
  class_& constant(char const* const name, T&& value)
  {
    scope::constant(name, std::forward<T>(value));

    return *this;
  }

  class_& constructor(char const* const name = "new")
  {
    return constructor<>(name);
  }

  template <class ...A>
  class_& constructor(char const* const name = "new")
  {
    constructors_.push_back({name, constructor_stub<1, C, A...>});

    return *this;
  }

  template <class ...A>
  class_& inherits()
  {
    swallow{
      (S<A>::copy_accessors(class_<A>::getters(), getters_), 0)...
    };

    swallow{
      (S<A>::copy_accessors(class_<A>::setters(), setters_), 0)...
    };

    swallow{
      (S<A>::copy_defs(class_<A>::defs(), defs_), 0)...
    };

    assert(inherits_.empty());
    inherits_.reserve(sizeof...(A));
    swallow{
      (inherits_.push_back(class_<A>::inherits), 0)...
    };

    return *this;
  }

  static auto class_name() noexcept { return class_name_; }

  static auto const& defs() noexcept { return defs_; }

  static auto const& getters() noexcept { return getters_; }

  static auto const& setters() noexcept { return setters_; }

  auto getters_info() const
  {
    accessors_info_type r;

    for (auto& g: getters_)
    {
      r.emplace(g.first, std::get<2>(g.second));
    }

    return r;
  }

  auto setters_info() const
  {
    accessors_info_type r;

    for (auto& s: setters_)
    {
      r.emplace(s.first, std::get<2>(s.second));
    }

    return r;
  }

  static bool inherits(char const* const name) noexcept
  {
    assert(class_name_ && name);
    if (std::strcmp(name, class_name_))
    {
      for (auto const f: inherits_)
      {
        if (f(name))
        {
          return true;
        }
        // else do nothing
      }

      return false;
    }
    else
    {
      return true;
    }
  }

  template <typename FP, FP fp>
  std::enable_if_t<
    is_function_pointer<FP>{},
    class_&
  >
  def(char const* const name)
  {
    scope::def<FP, fp>(name);

    return *this;
  }

  template <typename FP, FP fp>
  std::enable_if_t<
    !is_function_pointer<FP>{},
    class_&
  >
  def(char const* const name)
  {
    defs_.push_back(
      {
        {},
        member_info_type {
          name,
          member_stub<FP, fp, 2>(fp)
        }
      }
    );

    return *this;
  }

  template <typename FP, FP fp>
  std::enable_if_t<
    !is_function_pointer<FP>{},
    class_&
  >
  def_func(char const* const name)
  {
    defs_.push_back(
      {
        {},
        member_info_type {
          name,
          member_stub<FP, fp, 1>(fp)
        }
      }
    );

    return *this;
  }

  template <typename FP, FP fp>
  std::enable_if_t<
    is_function_pointer<FP>{},
    class_&
  >
  def_func(char const* const name)
  {
    defs_.push_back(
      {
        {},
        member_info_type {
          name,
          func_stub<FP, fp, 1>(fp)
        }
      }
    );

    return *this;
  }

  auto& enum_(char const* const name, lua_Integer const value)
  {
    scope::constant(name, value);

    return *this;
  }

  template <class FP, FP fp>
  auto& property(char const* const name)
  {
    getters_.emplace(name,
      accessors_type::mapped_type {
        {},
        member_stub<FP, fp, 3>(fp),
        get_property_type<FP, fp>(fp)
      }
    );

    assert(std::get<1>(getters_[name]));

    return *this;
  }

  template <typename FPA, FPA fpa, typename FPB, FPB fpb>
  auto& property(char const* const name)
  {
    getters_.emplace(name,
      accessors_type::mapped_type {
        {},
        member_stub<FPA, fpa, 3>(fpa),
        get_property_type<FPA, fpa>(fpa)
      }
    );

    assert(std::get<1>(getters_[name]));

    setters_.emplace(name,
      accessors_type::mapped_type {
        {},
        member_stub<FPB, fpb, 3>(fpb),
        get_property_type<FPA, fpa>(fpa)
      }
    );

    assert(std::get<1>(setters_[name]));

    return *this;
  }

  template <typename FP, FP fp>
  std::enable_if_t<
    !is_function_pointer<FP>{},
    class_&
  >
  vararg_def(char const* const name)
  {
    defs_.push_back(
      {
        {},
        member_info_type {
          name,
          vararg_member_stub<FP, fp>(fp)
        }
      }
    );

    return *this;
  }

private:
  template <class A>
  struct S
  {
    static void copy_accessors(accessors_type const& src,
      accessors_type& dst)
    {
      for (auto& a: src)
      {
        auto& n(dst[a.first] = a.second);

        std::get<0>(n).emplace_back(convert<A>);
        std::get<0>(n).shrink_to_fit();
      }
    }

    static void copy_defs(defs_type const& src, defs_type& dst)
    {
      dst.reserve(dst.size() + src.size());

      for (auto& a: src)
      {
        dst.push_back(a);

        dst.back().first.emplace_back(convert<A>);
        dst.back().first.shrink_to_fit();
      }
    }
  };

  void apply(lua_State* const L)
  {
    assert(parent_scope_);
    scope::apply(L);

    scope::get_scope(L);
    assert(lua_istable(L, -1));

    for (auto& i: constructors_)
    {
      assert(lua_istable(L, -1));
      lua_pushcfunction(L, i.callback);

      rawsetfield(L, -2, i.name);
    }

    assert(inherits_.capacity() == inherits_.size());
    constructors_.shrink_to_fit();
    defs_.shrink_to_fit();

    lua_pop(L, 1);

    assert(!lua_gettop(L));
  }

  template <class A>
  static void* convert(void* const a) noexcept
  {
    return static_cast<A*>(static_cast<C*>(a));
  }
};

template <class C>
char const* class_<C>::class_name_;

template <class C>
std::vector<bool(*)(char const*) noexcept> class_<C>::inherits_;

template <class C>
std::vector<func_info_type> class_<C>::constructors_;

template <class C>
defs_type class_<C>::defs_;

template <class C>
accessors_type class_<C>::getters_;

template <class C>
accessors_type class_<C>::setters_;

} // lualite

#endif // LUALITE_HPP
