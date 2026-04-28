#include <array>
#include<typeinfo>
#include <iostream>
#include <limits>
#include <ranges>
#include <cassert>
#include <vector>
#include <algorithm>
constexpr std::size_t DYNAMIC_CAPACITY = std::numeric_limits<std::size_t>::max();

template <typename T, size_t Capacity>
class Container {
  alignas(alignof(T)) std::array<std::byte, Capacity * sizeof(T)> arr;
  inline constexpr static size_t cap = Capacity;
public:
  Container() {}

  Container(size_t cap) {
    if (cap != Capacity) {
      throw std::invalid_argument("Value cap != value capacity");
    }
  }

  T& operator[](size_t pos) {
    return *reinterpret_cast<T*>(&arr[pos * sizeof(T)]);
  }

  const T& operator[](size_t pos) const {
    return *reinterpret_cast<const T*>(&arr[pos * sizeof(T)]);
  }

  void construct(size_t pos, const T& value) {
    std::construct_at(
      reinterpret_cast<T*>(arr.data() + pos * sizeof(T)),
      value
    );
  }

  void destroy(size_t pos) {
    std::destroy_at(reinterpret_cast<T*>(&arr[pos * sizeof(T)]));
  }

  size_t get_cap() const noexcept {
    return cap;
  }
};

template <typename T>
class Container<T, DYNAMIC_CAPACITY> {
  T* arr;
  size_t cap;
public:
  Container(size_t Cap) : cap(Cap) {
    arr = reinterpret_cast<T*>(new std::byte[sizeof(T) * Cap]);
  }

  T& operator[](size_t pos) {
    return *(arr + pos);
  }

  const T& operator[](size_t pos) const {
    return *(arr + pos);
  }

  void construct(size_t pos, const T& value) {
    auto ptr = reinterpret_cast<T*>(arr + pos);
    std::construct_at(ptr, value);
  }

  void destroy(size_t pos) {
    std::destroy_at(arr + pos);
  }

  size_t get_cap() const noexcept {
    return cap;
  }

  ~Container() {
    delete[] reinterpret_cast<char*>(arr);
  }
};

template<typename T, bool IsConst, size_t Capacity>
struct IteratorFields {
  using pointer_type = std::conditional_t<IsConst, const T*, T*>;
  inline static constexpr size_t cap = Capacity;
  pointer_type ptr = nullptr;
  size_t real_pos = 0;
  size_t pos_head = 0;
  IteratorFields(pointer_type pointer, size_t ps_h, size_t r_p, size_t cp = 0) {
    ptr = pointer;
    pos_head = ps_h;
    real_pos = r_p;
    (void) cp;
  }
};

template<typename T, bool IsConst>
struct IteratorFields<T, IsConst, DYNAMIC_CAPACITY> {
  using pointer_type = std::conditional_t<IsConst, const T*, T*>;
  size_t cap = 0;
  pointer_type ptr = nullptr;
  size_t real_pos = 0;
  size_t pos_head = 0;
  IteratorFields(pointer_type pointer, size_t ps_h, size_t r_p, size_t cp) {
    ptr = pointer;
    pos_head = ps_h;
    real_pos = r_p;
    cap = cp;
  }
};

template<typename T, size_t Capacity = DYNAMIC_CAPACITY>
class CircularBuffer {
  template<bool IsConst, size_t Cap = DYNAMIC_CAPACITY>
  class base_iterator {
   public:
    using pointer_type = std::conditional_t<IsConst, const T*, T*>;
    using reference_type = std::conditional_t<IsConst, const T&, T&>;
    using value_type = T;
    using iterator_category = std::random_access_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using pointer = pointer_type;
    using reference = reference_type;
    IteratorFields<value_type, IsConst, Cap> f;
    friend CircularBuffer;

    base_iterator(pointer_type ptr, size_t h, size_t r_p, size_t cap = 0): f{ptr, h, r_p, cap} {}

    template <bool B = IsConst, typename = std::enable_if_t<B>>
    base_iterator(const base_iterator<false, Capacity>& other) : f(other.f) {} 

    base_iterator& operator++() {
      if (f.pos_head + f.real_pos == f.cap - 1) {
        f.ptr -= f.cap;
      }
      ++f.ptr;
      ++f.real_pos;
      return *this;
    }

    base_iterator operator++(int) {
      base_iterator copy = *this;
      if (f.pos_head + f.real_pos == f.cap - 1) {
        f.ptr -= f.cap;
      }
      ++f.ptr;
      ++f.real_pos;
      return copy;
    }

    base_iterator& operator--() {
      if (f.pos_head + f.real_pos == 0) {
        f.ptr += f.cap;
      }
      --f.ptr;
      --f.real_pos;
      return *this;
    }
 
    base_iterator operator--(int) {
      base_iterator copy = *this;
      if (f.pos_head + f.real_pos == 0) {
        f.ptr += f.cap;
      }
      --f.ptr;
      --f.real_pos;
      return copy;
    }

    base_iterator& operator+=(difference_type value) {
      f.ptr += ((f.pos_head + f.real_pos + value) % f.cap - (f.real_pos + f.pos_head) % f.cap);
      f.real_pos += value;
      return *this;
    }

    friend base_iterator operator+(const base_iterator& iter, difference_type value) {
      base_iterator copy = iter;
      copy += value;
      return copy;
    }

    friend base_iterator operator+(difference_type value, const base_iterator& iter) {
      return iter + value;
    }

    base_iterator& operator-=(difference_type  value) {
      f.ptr -= ((f.pos_head + f.real_pos + value) % f.cap - (f.real_pos + f.pos_head) % f.cap);
      f.real_pos -= value;
      return *this;
    }

    friend base_iterator operator-(const base_iterator& iter, difference_type  value) {
      base_iterator copy = iter;
      copy -= value;
      return copy;
    }

    friend base_iterator operator-(difference_type value, const base_iterator& iter) {
      return iter - value;
    }

    reference_type operator*() const noexcept{
      return  *f.ptr;
    }

    pointer_type operator->() const noexcept {
      return f.ptr;
    }

    bool operator==(const base_iterator& other) {
      return f.real_pos == other.f.real_pos;
    }

    bool operator<(const base_iterator& other) {
      return f.real_pos < other.f.real_pos;
    }

    bool operator<=(const base_iterator& other) {
      return f.real_pos <= other.f.real_pos;
    }

    bool operator>=(const base_iterator& other) {
      return f.real_pos >= other.f.real_pos;
    }

    bool operator!=(const base_iterator& other) {
      return f.real_pos != other.f.real_pos;
    }

    bool operator>(const base_iterator& other) {
      return f.real_pos > other.f.real_pos;
    }

    difference_type operator-(const base_iterator& other) {
      return f.real_pos - other.f.real_pos;
    }
  };


public:
  using iterator = base_iterator<false, Capacity>;
  using const_iterator = base_iterator<true, Capacity>;
  using reverse_iterator = std::reverse_iterator<base_iterator<false, Capacity>>;
  using const_reverse_iterator = std::reverse_iterator<base_iterator<true, Capacity>>;

  CircularBuffer() {}
  CircularBuffer(size_t Cap) : cont(Cap) {}

  CircularBuffer(const CircularBuffer& buf): cont(buf.cont.get_cap()) {
    for (size_t i = 0; i < buf.sz; ++i) {
      cont.construct(i, buf.cont[i]);
    }
    sz = buf.sz;
    head = buf.head;
    tail = buf.tail;
  }

  CircularBuffer& operator=(const CircularBuffer& buf)  {
    if (this == &buf) {
      return *this;
    }
    for (size_t i = 0; i < sz; ++i) {
      cont.destroy(i);
    }
    for (size_t i = 0; i < buf.sz; ++i) {
      cont.construct(i, buf.cont[i]);
    }
    sz = buf.sz;
    head = buf.head;
    tail = buf.tail;
    return *this;
  }

  void push_back(const T& value) {
    if (sz == 0) {
      cont.construct(0, value);
      ++sz;
      return;
    }
    if (sz < cont.get_cap()) {
      cont.construct((tail + 1) % cont.get_cap(), value);
      ++tail;
      tail %= cont.get_cap();
      ++sz;
      return;
    }
    ++tail;
    tail %= cont.get_cap();
    ++head;
    head %= cont.get_cap();
    cont[tail] = value;
  }

  void push_front(const T& value) {
    if (sz < cont.get_cap()) {
      cont.construct((head - 1) % cont.get_cap(), value);
      --head;
      head %= cont.get_cap();
      ++sz;
      tail = (head + sz - 1) % cont.get_cap();
      return;
    }
    --head;
    head %= cont.get_cap();
    tail = (head + sz - 1) % cont.get_cap();
    cont[head] = value;
    return;
  }

  void pop_back() {
    cont.destroy(tail);
    --sz;
    --tail;
    tail %= cont.get_cap();
  }

  void pop_front() {
    cont.destroy(head);
    --sz;
    ++head;
    head %= cont.get_cap();
  }

  T& operator[](size_t pos) {
    return cont[(head + pos) % cont.get_cap()];
  }

  const T& operator[](size_t pos) const {
    return cont[(head + pos) % cont.get_cap()];
  }

  T& at(size_t pos) {
    if (sz <= pos) {
      throw std::out_of_range("The position is out of range");
    }
    return cont[(head + pos) % cont.get_cap()];
  }

  const T& at(size_t pos) const {
    if (sz <= pos) {
      throw std::out_of_range("The position is out of range");
    }
    return cont[(head + pos) % cont.get_cap()];
  }

  size_t size() const noexcept {
    return sz;
  }

  size_t capacity() const noexcept {
    return cont.get_cap();
  }

  bool empty() const noexcept {
    return sz == 0;
  }

  bool full() const noexcept {
    return sz == cont.get_cap();
  }

  iterator begin() {
    T* ptr = &cont[head];
    return iterator{ptr, head, 0, cont.get_cap()};
  }

  iterator end() {
    T* ptr = &cont[head];
    return iterator{ptr, head, sz, cont.get_cap()};
  }

  const_iterator begin() const {
    const T* ptr = &cont[head];
    return const_iterator{ptr, head, 0, cont.get_cap()};
  }

  const_iterator end() const {
    const T* ptr = &cont[head];
    return const_iterator{ptr, head, sz, cont.get_cap()};
  }

  const_iterator cbegin() const {
    const T* ptr = &cont[head];
    return const_iterator{ptr, head, 0, cont.get_cap()};
  }

  const_iterator cend() const {
    T* ptr = &cont[tail];
    return const_iterator{ptr, head, sz, cont.get_cap()};
  }

  reverse_iterator rbegin() {
    return std::make_reverse_iterator(end());
  }

  reverse_iterator rend() {
    return std::make_reverse_iterator(begin());
  }

  const_reverse_iterator rbegin() const {
    return std::make_reverse_iterator(end());
  }

  const_reverse_iterator rend() const {
    return std::make_reverse_iterator(begin());
  }

  const_reverse_iterator crbegin() const {
    return  std::make_reverse_iterator(cend());
  }

  const_reverse_iterator crend() const {
    return std::make_reverse_iterator(cbegin());
  }

  void insert(iterator it, const T& value) {
    if (sz == 0) {
      cont.construct(0, value);
      ++sz;
      return;
    }
    if (it == begin() && sz == cont.get_cap()) {
      return;
    }
    if (sz == cont.get_cap()) {
      for (size_t i = 0; i < it.f.real_pos - 1; ++i) {
        cont[(head + i) % cont.get_cap()] = cont[(head + i + 1) % cont.get_cap()];
      }
      cont[(head + it.f.real_pos - 1) % cont.get_cap()] = value;
      return;
    }
    cont.construct(tail + 1, value);
    ++tail;
    ++sz;
    for (size_t i = sz - 1; i > it.f.real_pos; --i) {
      cont[(head + i) % cont.get_cap()] = cont[(head + i - 1) % cont.get_cap()];
    }
    cont[(head + it.f.real_pos) % cont.get_cap()] = value;
    return;
  }

  void erase(iterator it) {
    for (size_t i = it.f.real_pos; i < sz - 1; ++i) {
      cont[(head + i) % cont.get_cap()] = cont[(head + i + 1) % cont.get_cap()];
    }
    cont.destroy(tail);
    --sz;
    --tail;
    tail %= cont.get_cap();
  }

  ~CircularBuffer() {
    for (size_t i = 0; i < sz; ++i) {
      size_t index = (head + i) % cont.get_cap();
      cont.destroy(index);
    }
  }

 private:
  Container<T, Capacity> cont;
  size_t sz = 0;
  size_t head = 0;
  size_t tail = 0;
};