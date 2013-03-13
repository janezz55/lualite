#pragma once
#ifndef LUALITE_STDCONTAINERS_HPP
# define LUALITE_STDCONTAINERS_HPP

#include <cassert>

#include <array>

#include <deque>

#include <forward_list>

#include <list>

#include <map>

#include <unordered_map>

#include <string>

#include <vector>

namespace lualite
{

namespace detail
{

inline void set_result(lua_State* const  L,
  std::string const& value)
{
  lua_pushlstring(L, value.c_str(), value.size());
}

template <typename T, std::size_t N>
inline void set_result(lua_State* const L,
  std::array<T, N> && a)
{
  lua_createtable(L, N, 0);

  auto const end(a.cend());

  for (auto i(a.cbegin()); i != end; ++i)
  {
    lua_pushunsigned(L, i - a.cbegin() + 1);
    set_result(L, *i);

    lua_rawset(L, -3);
  }
}

template <typename T, class Alloc>
inline void set_result(lua_State* const L,
  std::deque<T, Alloc> && d)
{
  lua_createtable(L, d.size(), 0);

  auto const end(d.cend());

  for (auto i(d.cbegin()); i != end; ++i)
  {
    lua_pushunsigned(L, i - d.cbegin() + 1);
    set_result(L, *i);

    lua_rawset(L, -3);
  }
}

template <typename T, class Alloc>
inline void set_result(lua_State* const L,
  std::forward_list<T, Alloc> && l)
{
  lua_createtable(L, l.size(), 0);

  std::size_t j(1);

  auto const end(l.cend());

  for (auto i(l.cbegin()); i != end; ++i, ++j)
  {
    lua_pushunsigned(L, j);
    set_result(L, *i);

    lua_rawset(L, -3);
  }
}

template <typename T, class Alloc>
inline void set_result(lua_State* const L,
  std::list<T, Alloc> && l)
{
  lua_createtable(L, l.size(), 0);

  std::size_t j(1);

  auto const end(l.cend());

  for (auto i(l.cbegin()); i != end; ++i, ++j)
  {
    lua_pushunsigned(L, j);
    set_result(L, *i);

    lua_rawset(L, -3);
  }
}


template <class Key, class T, class Compare, class Alloc>
inline void set_result(lua_State* const L,
  std::map<Key, T, Compare, Alloc> && m)
{
  lua_createtable(L, 0, m.size());

  auto const end(m.cend());

  for (auto i(m.cbegin()); i != end; ++i)
  {
    set_result(L, i->first);
    set_result(L, i->second);

    lua_rawset(L, -3);
  }
}

template <class Key, class T, class Hash, class Pred, class Alloc>
inline void set_result(lua_State* const L,
  std::unordered_map<Key, T, Hash, Pred, Alloc> && m)
{
  lua_createtable(L, 0, m.size());

  auto const end(m.cend());

  for (auto i(m.cbegin()); i != end; ++i)
  {
    set_result(L, i->first);
    set_result(L, i->second);

    lua_rawset(L, -3);
  }
}

template <typename T, class Alloc>
inline void set_result(lua_State* const L,
  std::vector<T, Alloc> && v)
{
  lua_createtable(L, v.size(), 0);

  auto const end(v.cend());

  for (auto i(v.cbegin()); i != end; ++i)
  {
    lua_pushunsigned(L, i - v.cbegin() + 1);
    set_result(L, *i);

    lua_rawset(L, -3);
  }
}

template <int I>
inline std::string get_arg(lua_State* const L,
  std::string const)
{
  assert(lua_isstring(L, I));

  std::size_t len;
  char const* const val(lua_tolstring(L, I, &len));

  return std::string(val, len);
}

template<int I, class T, std::size_t N>
inline std::array<T, N> get_arg(lua_State* const L,
  std::array<T, N> const)
{
  assert(lua_istable(L, I));

  std::array<T, N> result;

  auto const end(lua_rawlen(L, I) + 1);

  for (decltype(lua_rawlen(L, I)) i(1); i != end; ++i)
  {
    lua_rawgeti(L, I, i);

    result[i - 1] = get_arg<I + 1>(L, T());

    lua_pop(L, 1);
  }

  return result;
}

template <int I, typename T, class Alloc>
inline std::deque<T, Alloc> get_arg(lua_State* const L,
  std::deque<T, Alloc> const)
{
  assert(lua_istable(L, I));

  std::deque<T, Alloc> result;

  auto const end(lua_rawlen(L, I) + 1);

  for (decltype(lua_rawlen(L, I)) i(1); i != end; ++i)
  {
    lua_rawgeti(L, I, i);

    result.emplace_back(get_arg<I + 1>(L, T()));

    lua_pop(L, 1);
  }

  return result;
}

template <int I, typename T, class Alloc>
inline std::forward_list<T, Alloc> get_arg(lua_State* const L,
  std::forward_list<T, Alloc> const)
{
  assert(lua_istable(L, I));

  std::list<T, Alloc> result;

  static decltype(lua_rawlen(L, I)) const end(0);

  for (decltype(lua_rawlen(L, I)) i(lua_rawlen(L, I)); i != end; --i)
  {
    lua_rawgeti(L, I, i);

    result.emplace_front(get_arg<I + 1>(L, T()));

    lua_pop(L, 1);
  }

  return result;
}

template <int I, typename T, class Alloc>
inline std::list<T, Alloc> get_arg(lua_State* const L,
  std::list<T, Alloc> const)
{
  assert(lua_istable(L, I));

  std::list<T, Alloc> result;

  auto const end(lua_rawlen(L, I) + 1);

  for (decltype(lua_rawlen(L, I)) i(1); i != end; ++i)
  {
    lua_rawgeti(L, I, i);

    result.emplace_back(get_arg<I + 1>(L, T()));

    lua_pop(L, 1);
  }

  return result;
}

template <int I, typename T, class Alloc>
inline std::vector<T, Alloc> get_arg(lua_State* const L,
  std::vector<T, Alloc> const)
{
  assert(lua_istable(L, I));

  std::vector<T, Alloc> result;

  auto const end(lua_rawlen(L, I) + 1);

  for (decltype(lua_rawlen(L, I)) i(1); i != end; ++i)
  {
    lua_rawgeti(L, I, i);

    result.emplace_back(get_arg<I + 1>(L, T()));

    lua_pop(L, 1);
  }

  return result;
}

template <int I, class Key, class T, class Compare, class Alloc>
inline std::map<Key, T, Compare, Alloc> get_arg(lua_State* const L,
  std::map<Key, T, Compare, Alloc> const)
{
  assert(lua_istable(L, I));

  std::map<Key, T, Compare, Alloc> result;

  lua_pushnil(L);

  while (lua_next(L, I))
  {
    result.emplace(get_arg<I + 1>(L, Key()), get_arg<I + 2>(L, T()));

    lua_pop(L, 1);
  }

  return result;
}

template <int I, class Key, class T, class Hash, class Pred, class Alloc>
inline std::unordered_map<Key, T, Hash, Pred, Alloc> get_arg(
  lua_State* const L, std::unordered_map<Key, T, Hash, Pred, Alloc> const)
{
  assert(lua_istable(L, I));

  std::unordered_map<Key, T, Hash, Pred, Alloc> result;

  lua_pushnil(L);

  while (lua_next(L, I))
  {
    result.emplace(get_arg<I + 1>(L, Key()), get_arg<I + 2>(L, T()));

    lua_pop(L, 1);
  }

  return result;
}

} // detail

} // lualite

#endif // LUALITE_STDCONTAINERS_HPP