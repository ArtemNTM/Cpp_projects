#include <iostream>
#include <type_traits>
#include <vector>

template <typename T, typename... Types>
struct count_same;

template <typename T, typename... Types>
constexpr bool count_same_v = (count_same<T, Types...>::value == 1);

template <typename... Types>
class Tuple;

// Concepts
template <typename T>
concept cli = requires() { T{}; };

template <typename Head, typename... Tail>
concept base_construct = (std::is_default_constructible_v<Head> &&
                      (std::is_default_constructible_v<Tail> && ...) &&
                      cli<Head> && (cli<Tail> && ...));

template <typename Head, typename... Tail>
concept head_tail_construct = std::is_copy_constructible_v<Head> && (std::is_copy_constructible_v<Tail> && ...);

template<typename Head,  typename UHead,
         typename Other, typename... UTail>
concept head_constructible = ((sizeof...(UTail) != 0) 
                            || (!std::is_constructible_v<Head, Other> && 
                            !std::is_convertible_v<Other, Head> && 
                            !std::is_same_v<Head, UHead>));

template<typename Head,  typename UHead, typename... UTail>
concept head_convertible = std::is_convertible_v<decltype(get<0>(std::forward<Tuple<UHead, UTail...>>
                           (std::declval<Tuple<UHead, UTail...>>()))), Head>;


template <typename Head, typename... Tail>
class Tuple<Head, Tail...> {
public:
  Head head;
  [[no_unique_address]] Tuple<Tail...> tail;

  static constexpr bool all_convert_const_ref = std::is_convertible_v<const Head&, Head> &&
  (std::is_convertible_v<const Tail&, Tail> && ...);
  static constexpr bool all_copy_constructible = std::is_copy_constructible<Head>::value &&
  (std::is_copy_constructible<Tail>::value && ...);
  static constexpr bool all_move_constructible = std::is_move_constructible<Head>::value && 
  (std::is_move_constructible<Tail>::value && ...);

  static constexpr int sz = sizeof...(Tail);

  template <typename...>
  friend class Tuple;

  constexpr Tuple() requires base_construct<Head, Tail...> : head(), tail() {}

  explicit (!all_convert_const_ref)
  constexpr Tuple(const Head& h, const Tail&... t) requires head_tail_construct<Head, Tail...> : head(h), tail(t...)
  {}

  template <typename UHead, typename... UTail>
  requires (sz == sizeof...(UTail) &&
            std::is_constructible_v<Head, UHead> && (std::is_constructible_v<Tail, UTail> && ...)
           )
  explicit(!(std::is_convertible_v<Head, UHead> && (std::is_convertible_v<Tail, UTail> && ...)))
  Tuple(UHead&& h, UTail&&... t)
    : head(std::forward<UHead>(h)), tail(std::forward<UTail>(t)...)
  {}

  template <typename UHead, typename... UTail>
    explicit(!(head_convertible<Head, UHead, UTail...> && 
    (std::is_convertible_v<decltype(get<sz + 1 - sizeof...(UTail)>(std::forward<Tuple<UHead, UTail...>>(std::declval<Tuple<UHead, UTail...>>()))), Tail> && ...)))
  Tuple(const Tuple<UHead, UTail...>& other) requires
    (sz == sizeof...(UTail)) && 
    (std::is_constructible_v<Head, decltype(get<0>(std::forward<decltype(other)>(other)))>) &&
    (std::is_constructible_v<Tail, decltype(get<sz + 1 - sizeof...(UTail)>(std::forward<decltype(other)>(other)))> && ...) &&
    (head_constructible<Head, UHead, decltype(other), UTail...>)
    : head(other.head), tail(other.tail) {}
  
  template <typename UHead, typename... UTail>
    explicit(!(head_convertible<Head, UHead, UTail...> && 
    (std::is_convertible_v<decltype(get<sz + 1 - sizeof...(UTail)>(std::forward<Tuple<UHead, UTail...>>(std::declval<Tuple<UHead, UTail...>>()))), Tail> && ...)))
  Tuple(Tuple<UHead, UTail...>&& other) requires
    (sz == sizeof...(UTail)) && 
    std::is_constructible_v<Head, decltype(get<0>(std::forward<decltype(other)>(other)))> &&
    (std::is_constructible_v<Tail, decltype(get<sz + 1 - sizeof...(UTail)>(std::forward<decltype(other)>(other)))> && ...) &&
    (head_constructible<Head, UHead, decltype(other), UTail...>)
    : head(get<0>(std::forward<decltype(other)>(other))), tail(get<sz + 1 - sizeof...(UTail)>(std::forward<decltype(other)>(other))) {}
  
  template <typename Type1, typename Type2>
  requires (sz == 1) && std::is_constructible_v<Head, const Type1&> &&
            std::is_constructible_v<Tail..., const Type2&>
  Tuple(const std::pair<Type1, Type2>& pair) : head(pair.first), tail((pair.second)) {}

  template <typename Type1, typename Type2>
  requires (sz == 1) && std::is_constructible_v<Head, Type1&&> &&
            std::is_constructible_v<Tail..., Type2&&>
  Tuple(std::pair<Type1, Type2>&& pair) : head(std::forward<decltype(pair.first)>(pair.first)),
  tail(std::forward<decltype(pair.second)>(pair.second)) {}
  
  explicit(!all_copy_constructible)
  Tuple(const Tuple& other) = default;

  explicit(!all_move_constructible)
  Tuple(Tuple&& other) = default;

  decltype(auto) operator=(const Tuple& other) 
    requires (std::is_copy_assignable<Head>::value) && (std::is_copy_assignable<Tail>::value && ...)
  {
    head = other.head;
    tail = other.tail;
    return *this;
  }

  decltype(auto) operator=(Tuple&& other) 
    requires (std::is_move_assignable<Head>::value) && (std::is_move_assignable<Tail>::value && ...)
  {
    head = std::forward<decltype(other.head)>(other.head);
    tail = std::forward<decltype(other.tail)>(other.tail);
    return *this;
  }

  template <typename UHead, typename... UTail>
  decltype(auto) operator=(const Tuple<UHead, UTail...>& other) 
    requires (sz == sizeof...(UTail)) && (std::is_assignable_v<Head&, UHead&>) && (std::is_assignable_v<Tail&, UTail&> && ...)
  {
    head = other.head;
    tail = other.tail;
    return *this;
  }

  template <typename UHead, typename... UTail>
  decltype(auto) operator=(Tuple<UHead, UTail...>&& other) 
    requires (sz == sizeof...(UTail)) && (std::is_assignable<Head&, UHead>::value) && (std::is_assignable<Tail&, UTail>::value && ...)
  {
    head = std::forward<decltype(other.head)>(other.head);
    tail = std::forward<decltype(other.tail)>(other.tail);
    return *this;
  }

  template <typename Type1, typename Type2>
  decltype(auto) operator=(const std::pair<Type1, Type2>& pair) {
    head = pair.first;
    tail = pair.second;
    return *this;
  }

  template <typename Type1, typename Type2>
  decltype(auto) operator=(std::pair<Type1, Type2>&& pair) {
    head = std::forward<Type1>(pair.first);
    tail = std::forward<Type2>(pair.second);
    return *this;
  }
};

template <>
class Tuple<> {};

template <typename Type1, typename Type2>
Tuple(std::pair<Type1, Type2>&) -> Tuple<Type1, Type2>;

template <typename Type1, typename Type2>
Tuple(const std::pair<Type1, Type2>&) -> Tuple<Type1, Type2>;

template <typename Type1, typename Type2>
Tuple(std::pair<Type1, Type2>&&) -> Tuple<Type1, Type2>;

template<std::size_t I, class... Ts>
requires (I < sizeof...(Ts))
constexpr decltype(auto) get(Tuple<Ts...>& t)
{
  if constexpr (I == 0) {
    return static_cast<decltype(t.head)&>(t.head);
  } else {
    return get<I - 1>(t.tail);
  }
}

template <std::size_t I, class... Ts>
requires (I < sizeof...(Ts))
constexpr decltype(auto) get(const Tuple<Ts...>& t)
{
  if constexpr (I == 0) {
    return static_cast<const decltype(t.head)&>(t.head);
  } else {
    return get<I - 1>(t.tail);
  }
}

template<std::size_t I, class... Ts>
requires (I < sizeof...(Ts))
constexpr decltype(auto) get(Tuple<Ts...>&& t)
{
  if constexpr (I == 0) {
    return std::forward<decltype(t.head)>(t.head);
  } else {
    return get<I - 1>(std::forward<decltype(t.tail)>(t.tail));
  }
}

template <typename... Types>
auto makeTuple(Types&&... args) -> Tuple<std::decay_t<Types>...> {
  return Tuple<std::decay_t<Types>...>(std::forward<Types>(args)...);
}

template <typename... Types>
Tuple<Types&...> tie(Types&... args) noexcept {
  return {args...};
}

template <typename... Types>
Tuple<Types&&...> forward_as_tuple(Types&&... args) noexcept {
  return {std::forward<Types>(args)...};
}

template <typename T>
struct count_same<T> : std::integral_constant<int, 0> {};

template <typename T, typename Head, typename... Tail>
struct count_same<T, Head, Tail...> 
  : std::integral_constant<int, (std::is_same_v<T, Head> ? 1 : 0) + 
         count_same<T, Tail...>::value
    >
  {};

template <typename U, typename... Types>
U& get(Tuple<Types...>&);

template <typename U, typename... Types>
U&& get(Tuple<Types...>&&);

template <typename U, typename... Types>
const U& get(const Tuple<Types...>&);

template <typename U>
U& get(Tuple<>&);

template <typename U>
const U& get(const Tuple<>&);

template <typename U>
U&& get(Tuple<>&&);

template <typename T, typename Head, typename... Tail>
requires (count_same_v<T, Head, Tail...>)
T& get(Tuple<Head, Tail...>& tuple) noexcept {
  if constexpr(std::is_same_v<T, Head>) {
    return tuple.head;
  }
  return get<T>(tuple.tail);
}

template <typename T, typename Head, typename... Tail>
requires (count_same_v<T, Head, Tail...>)
T&& get(Tuple<Head, Tail...>&& tuple) noexcept {
  if constexpr(std::is_same_v<T, Head>) {
    return tuple.head;
  }
  return get<T>(tuple.tail);
}

template <typename T, typename Head, typename... Tail>
requires (count_same_v<T, Head, Tail...>)
const T& get(const Tuple<Head, Tail...>& tuple) noexcept {
  if constexpr(std::is_same_v<T, Head>) {
    return tuple.head;
  }
  return get<T>(tuple.tail);
}

template <typename T, typename Head, typename... Tail>
requires (count_same_v<T, Head, Tail...>)
const T&& get(const Tuple<Head, Tail...>&& tuple) noexcept {
  if constexpr(std::is_same_v<T, Head>) {
    return tuple.head;
  }
  return get<T>(tuple.tail);
}

// Размер Tuple
template <typename T>
struct tuple_size;

template <typename Head, typename... Tail>
struct tuple_size<Tuple<Head, Tail...>> {
  inline static constexpr size_t size = sizeof...(Tail) + 1;
};

template <typename T>
constexpr size_t tuple_size_v = tuple_size<T>::size;

// TupleCat
template<typename... Tuples>
inline constexpr std::size_t total_elems_v =
  (tuple_size_v<std::remove_reference_t<Tuples>> + ... + 0);

template<std::size_t Pos, typename First, typename... Rest>
struct pick_element {
  static constexpr decltype(auto) value(First&& first, Rest&&... rest) {
    constexpr std::size_t first_count =
      tuple_size_v<std::remove_reference_t<First>>;
    if constexpr (Pos < first_count) {
      return get<Pos>(std::forward<First>(first));
    } else {
      return pick_element<
                Pos - first_count,
                Rest...>::value(std::forward<Rest>(rest)...);
    }
  }
};

template<std::size_t Pos, typename Last>
struct pick_element<Pos, Last> {
  static constexpr decltype(auto) value(Last&& last) {
    return get<Pos>(std::forward<Last>(last));
  }
};

template<typename Seq, typename... Tuples>
struct cat_builder;

template<std::size_t... Idx, typename... Tuples>
struct cat_builder<std::index_sequence<Idx...>, Tuples...> {
  static constexpr auto build(Tuples&&... ts) {
    return makeTuple(
      pick_element<Idx, Tuples...>::value(
      std::forward<Tuples>(ts)...)...);
  }
};

template<typename... Tuples>
constexpr auto tupleCat(Tuples&&... ts) {
  constexpr std::size_t total = total_elems_v<std::remove_reference_t<Tuples>...>;
  return cat_builder<
            std::make_index_sequence<total>,
            Tuples&&...>::build(std::forward<Tuples>(ts)...);
}

// Операторы сравнения
template<typename A, typename B, std::size_t... I>
constexpr bool equal_impl(const A& a, const B& b, std::index_sequence<I...>)
{
  return ((get<I>(a) == get<I>(b)) && ...);
}

template<std::size_t I, typename A, typename B>
constexpr bool less_impl(const A& a, const B& b)
{
  if constexpr (I == tuple_size_v<std::remove_reference_t<A>>)
    return false;
  else {
    if (get<I>(a) < get<I>(b))  return true;
    if (get<I>(b) < get<I>(a))  return false;
    return less_impl<I + 1>(a, b);
  }
}

template<typename... Ts, typename... Us>
requires (sizeof...(Ts) == sizeof...(Us))
constexpr bool operator==(const Tuple<Ts...>& a, const Tuple<Us...>& b)
{
  return equal_impl(a, b, std::make_index_sequence<sizeof...(Ts)>{});
}

template<typename... Ts, typename... Us>
requires (sizeof...(Ts) == sizeof...(Us))
constexpr bool operator!=(const Tuple<Ts...>& a, const Tuple<Us...>& b)
{
  return !(a == b);
}

template<typename... Ts, typename... Us>
requires (sizeof...(Ts) == sizeof...(Us))
constexpr bool operator<(const Tuple<Ts...>& a, const Tuple<Us...>& b)
{
  return less_impl<0>(a, b);
}

template<typename... Ts, typename... Us>
requires (sizeof...(Ts) == sizeof...(Us))
constexpr bool operator>(const Tuple<Ts...>& a, const Tuple<Us...>& b)
{
  return b < a;
}
