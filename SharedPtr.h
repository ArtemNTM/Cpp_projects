#include <iostream>
#include <memory>
#include <vector>
#include <algorithm>
#include <cassert>

template <typename T>
class WeakPtr;

template <typename T>
class SharedPtr;

template <typename T>
struct EnableSharedFromThis {
  WeakPtr<T> weak;
public:
  SharedPtr<T> shared_from_this() const {
    auto ptr = weak.lock();
    if (!ptr) {
      throw std::bad_weak_ptr();
    }
    return ptr;
  }
};

struct BaseControlBlock {
  size_t counter;
  size_t weak_counter;

  BaseControlBlock() : counter(0), weak_counter(0) {}
  BaseControlBlock(size_t frst, size_t sec) : counter(frst), weak_counter(sec) {}
  virtual void* get_ptr_object() {
    return nullptr;
  };
  virtual void destroy_value() = 0;
  virtual void destroy_struct() = 0;
  virtual ~BaseControlBlock() = default;
};

template <typename U, typename Deleter = std::default_delete<U>, typename Alloc = std::allocator<U>>
struct ControlBlockStandard: BaseControlBlock {
  U* ptr;
  [[no_unique_address]] Deleter deleter;
  [[no_unique_address]] Alloc alloc;
public:
  using BlockAlloc = typename std::allocator_traits<Alloc>::template rebind_alloc<ControlBlockStandard>;
  using Traits = std::allocator_traits<BlockAlloc>;
  ControlBlockStandard(U* p) : ptr(p) {}
  ControlBlockStandard(U* p, Deleter del) : ptr(p), deleter(std::move(del)) {}
  ControlBlockStandard(U* p, Deleter del, Alloc al)
    : ptr(p), deleter(std::move(del)), alloc(al) {}
  
  void* get_ptr_object() override {
    return ptr;
  }

  void destroy_value() override {
    deleter(ptr);
  }

  void destroy_struct() override {
    this->~ControlBlockStandard();
    BlockAlloc ba(alloc);
    Traits::deallocate(ba, this, 1);
  }
};

template <typename U, typename Alloc = std::allocator<U>>
struct ControlBlockMakeShared: BaseControlBlock {
  alignas(U) std::byte value[sizeof(U)];
  [[no_unique_address]] Alloc alloc;

public:
  using Traits = std::allocator_traits<Alloc>;
  using BlockAlloc = typename std::allocator_traits<Alloc>
                     ::template rebind_alloc<ControlBlockMakeShared>;

  template <typename... Args>
  ControlBlockMakeShared(Alloc alloc, Args... args) : alloc(alloc) {
    U* ptr = reinterpret_cast<U*>(value);
    Traits::construct(alloc, ptr ,std::forward<Args>(args)...);
  }

  void* get_ptr_object() override {
    U* ptr = reinterpret_cast<U*>(value);
    return ptr;
  }

  void destroy_value() override {
    U* ptr = reinterpret_cast<U*>(value);
    Traits::destroy(alloc, ptr);
  }

  void destroy_struct() override {
    this->~ControlBlockMakeShared();
    BlockAlloc ba = alloc;
    std::allocator_traits<BlockAlloc>::deallocate(ba, this, 1);
  }
};

template <typename T>
class SharedPtr {
private:
  T* ptr;
  BaseControlBlock* cb;

  template <typename U, typename... Args>
  friend SharedPtr<U> makeShared(Args&&... args);

  template <typename U, typename Alloc, typename... Args>
  friend SharedPtr<U> allocateShared(const Alloc& alloc, Args&&... args);

  void Delete_check() {
    if (cb->counter == 0 && cb->weak_counter == 0) {
      cb->destroy_value();
      cb->destroy_struct();
      return;
    }
    if (cb->counter == 0) {
      cb->destroy_value();
    }
  }

  template<typename U>
  friend class WeakPtr;

  template<typename U>
  friend class SharedPtr;

  template <typename U>
  SharedPtr(U* pointer, BaseControlBlock* other_cb) : ptr(pointer), cb(other_cb) {
    if constexpr(std::is_base_of_v<EnableSharedFromThis<T>, U>) {
      pointer->weak = *this;
    }
    ++cb->counter;
  }

  SharedPtr(const WeakPtr<T>& other) : ptr(other.ptr), cb(other.cb) {
    ++cb->counter;
  }

public:
  SharedPtr() : ptr(nullptr), cb(nullptr) {}

  SharedPtr(T* ptr) : ptr(ptr) {
    using Alloctraits = std::allocator_traits<std::allocator<T>>;
    using BlockAlloc = typename Alloctraits::template rebind_alloc<ControlBlockStandard<T>>;
    BlockAlloc ba{};
    auto* raw_cb = std::allocator_traits<BlockAlloc>::allocate(ba, 1);
    std::allocator_traits<BlockAlloc>::construct(ba, raw_cb, ptr);
    cb = raw_cb;
    ++cb->counter;
    if constexpr(std::is_base_of_v<EnableSharedFromThis<T>, T>) {
      ptr->weak = *this;
    }
  }

  template <typename U, class = std::enable_if_t<std::is_convertible_v<U*, T*>>>
  SharedPtr(U* ptr) : ptr(ptr) {
    if constexpr(std::is_base_of_v<EnableSharedFromThis<T>, U>) {
      ptr->weak = *this;
    }
    using Alloctraits = std::allocator_traits<std::allocator<U>>;
    using BlockAlloc = typename Alloctraits::template rebind_alloc<ControlBlockStandard<U>>;
    BlockAlloc ba{};
    auto* raw_cb = std::allocator_traits<BlockAlloc>::allocate(ba, 1);
    std::allocator_traits<BlockAlloc>::construct(ba, raw_cb, ptr);
    cb = raw_cb;
    ++cb->counter;
  }

  SharedPtr(const SharedPtr& other) noexcept : ptr(other.ptr), cb(other.cb) {
    if (cb) {
      ++cb->counter;
    }
  }

  template <typename U, class = std::enable_if_t<std::is_convertible_v<U*, T*>>>
  SharedPtr(const SharedPtr<U>& other) : ptr(other.ptr), cb(other.cb) {
    if (cb) {
      ++cb->counter;
    }
  }

  SharedPtr(SharedPtr&& other) noexcept : ptr(other.ptr), cb(other.cb) 
  {
    other.ptr = nullptr;
    other.cb = nullptr;
  }

  template <typename U, class = std::enable_if_t<std::is_convertible_v<U*, T*>>>
  SharedPtr(SharedPtr<U>&& other) : ptr(other.ptr), cb(other.cb) 
  {
    other.ptr = nullptr;
    other.cb = nullptr;
  }

  template <typename U, typename Deleter, class = std::enable_if_t<std::is_convertible_v<U*, T*>>>
  SharedPtr(U* ptr, Deleter del) : ptr(ptr) {
    using Alloctraits = std::allocator_traits<std::allocator<U>>;
    using BlockAlloc = typename Alloctraits::template rebind_alloc<ControlBlockStandard<U, Deleter>>;
    BlockAlloc ba{std::allocator<U>()};
    auto* raw_cb = std::allocator_traits<BlockAlloc>::allocate(ba, 1);
    std::allocator_traits<BlockAlloc>::construct(ba, raw_cb, ptr, std::move(del));
    cb = raw_cb;
    if (cb) {
      ++cb->counter;
    }
  }

  template <typename U, typename Deleter, typename Alloc, 
            class = std::enable_if_t<std::is_convertible_v<U*, T*>>>
  SharedPtr(U* ptr, Deleter del, Alloc alloc) : ptr(ptr) {
    using Alloctraits = std::allocator_traits<Alloc>;
    using BlockAlloc = typename Alloctraits::template rebind_alloc<ControlBlockStandard<U, Deleter, Alloc>>;
    BlockAlloc ba(alloc);
    auto* raw_cb = std::allocator_traits<BlockAlloc>::allocate(ba, 1);
    try {
      new (raw_cb) ControlBlockStandard<U, Deleter, Alloc>(ptr, del, alloc);
    } catch (...) {
      std::allocator_traits<BlockAlloc>::deallocate(ba, raw_cb, 1);
      throw;
    }
    cb = raw_cb;
    if (cb) {
      ++cb->counter;
    }
  }

  template <typename U, typename V, std::enable_if_t<std::is_base_of_v<T, V>, std::true_type>>
  SharedPtr(const SharedPtr<U>& other, V* p) : ptr(p), cb(other.cb) {
    if (cb) {
      ++cb->counter;
    }
  }

  template <typename U, typename V, std::enable_if_t<std::is_base_of_v<T, V>, std::true_type>>
  SharedPtr(SharedPtr<U>&& other, V* p) : ptr(p), cb(other.cb) {
    other.ptr = nullptr;
    other.cb = nullptr;
  }

  SharedPtr& operator=(const SharedPtr& other) noexcept {
    if (this == &other) {
      return *this;
    }
    if (cb) {
      --cb->counter;
      Delete_check();
    }
    ptr = other.ptr;
    cb = other.cb;
    if (cb) {
      ++cb->counter;
    }
    return *this;
  }

  SharedPtr& operator=(SharedPtr&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    if (cb) {
      --cb->counter;
      Delete_check();
    }
    ptr = other.ptr;
    cb = other.cb;
    other.ptr = nullptr;
    other.cb = nullptr;
    return *this;
  }

  template <typename U, class = std::enable_if_t<std::is_convertible_v<U*, T*>>>
  SharedPtr& operator=(const SharedPtr<U>& other) {
    if (cb) {
      --cb->counter;
      Delete_check();
    }
    ptr = other.ptr;
    cb = other.cb;
    if (cb) {
      ++cb->counter;
    }
    return *this;
  }

  template <typename U, class = std::enable_if_t<std::is_convertible_v<U*, T*>>>
  SharedPtr& operator=(SharedPtr<U>&& other) {
    if (cb) {
      --cb->counter;
      Delete_check();
    }
    ptr = other.ptr;
    cb = other.cb;
    other.ptr = nullptr;
    other.cb = nullptr;
    return *this;
  }

  size_t use_count() const {
    return cb->counter;
  }

  template <typename U>
  void reset(U* other) {
    ptr = other;
    if (cb) {
      --cb->counter;
      Delete_check();
    }
    cb = new ControlBlockStandard(other);
    ++cb->counter;
  }

  void reset() noexcept {
    if (cb) {
      --cb->counter;
      Delete_check();
    }
    cb = nullptr;
    ptr = nullptr;
  }

  auto* get() const noexcept {
    return ptr;
  }

  T& operator*() const noexcept {
    return *ptr;
  }

  T* operator->() const noexcept {
    return ptr;
  }

  operator bool() const noexcept { return ptr != nullptr; }

  void swap(SharedPtr& other) {
    std::swap(ptr, other.ptr);
    std::swap(cb, other.cb);
  }

  ~SharedPtr() {
    if (cb) {
      --cb->counter;
      Delete_check();
    }
  }
};

template <typename T>
class WeakPtr {
private:
  T* ptr;
  BaseControlBlock* cb;

  friend SharedPtr<T>;

  template<typename U>
  friend class WeakPtr;

  template <typename U, typename... Args>
  friend WeakPtr<U> make_shared(Args&&... args);

  void Delete_check() {
    if (cb->weak_counter == 0 && cb->counter == 0) {
      cb->destroy_struct();
      return;
    }
  }

  template <typename U>
  WeakPtr(U* pointer, BaseControlBlock* other_cb) 
    : ptr(pointer), cb(other_cb) {}

public:

// Конструкторы от указателей
  WeakPtr() : ptr(nullptr), cb(nullptr) {}

  WeakPtr(T* ptr) : ptr(ptr) {
    using Alloctraits = std::allocator_traits<std::allocator<T>>;
    using BlockAlloc = typename Alloctraits::template rebind_alloc<ControlBlockStandard<T>>;
    BlockAlloc ba{};
    auto* raw_cb = std::allocator_traits<BlockAlloc>::allocate(ba, 1);
    std::allocator_traits<BlockAlloc>::construct(ba, raw_cb, ptr);
    cb = raw_cb;
    ++cb->weak_counter;
  }

  template <typename U, class = std::enable_if_t<std::is_convertible_v<U*, T*>>>
  WeakPtr(U* ptr) : ptr(ptr) {
    using Alloctraits = std::allocator_traits<std::allocator<U>>;
    using BlockAlloc = typename Alloctraits::template rebind_alloc<ControlBlockStandard<U>>;
    BlockAlloc ba{};
    auto* raw_cb = std::allocator_traits<BlockAlloc>::allocate(ba, 1);
    std::allocator_traits<BlockAlloc>::construct(ba, raw_cb, ptr);
    cb = raw_cb;
    ++cb->weak_counter;
  }

  template <typename U, typename Deleter, class = std::enable_if_t<std::is_convertible_v<U*, T*>>>
  WeakPtr(U* ptr, Deleter del) : ptr(ptr) {
    using Alloctraits = std::allocator_traits<std::allocator<U>>;
    using BlockAlloc = typename Alloctraits::template rebind_alloc<ControlBlockStandard<U, Deleter>>;
    BlockAlloc ba{std::allocator<U>()};
    auto* raw_cb = std::allocator_traits<BlockAlloc>::allocate(ba, 1);
    std::allocator_traits<BlockAlloc>::construct(ba, raw_cb, ptr, std::move(del));
    cb = raw_cb;
    if (cb) {
      ++cb->weak_counter;
    }
  }

  template <typename U, typename Deleter, typename Alloc, 
            class = std::enable_if_t<std::is_convertible_v<U*, T*>>>
  WeakPtr(U* ptr, Deleter del, Alloc alloc) : ptr(ptr) {
    using Alloctraits = std::allocator_traits<Alloc>;
    using BlockAlloc = typename Alloctraits::template rebind_alloc<ControlBlockStandard<U, Deleter, Alloc>>;
    BlockAlloc ba(alloc);
    auto* raw_cb = std::allocator_traits<BlockAlloc>::allocate(ba, 1);
    std::allocator_traits<BlockAlloc>::construct(ba, raw_cb, ptr, std::move(del), alloc);
    cb = raw_cb;
    if (cb) {
      ++cb->counter;
    }
  }

// Конструктор от WeakPtr
  WeakPtr(const WeakPtr& other) noexcept : ptr(other.ptr), cb(other.cb) {
    if (cb) {
      ++cb->weak_counter;
    }
  }

  WeakPtr(WeakPtr&& other) noexcept : ptr(other.ptr), cb(other.cb) {
    other.ptr = nullptr;
    other.cb = nullptr;
  }

  template <typename U, class = std::enable_if_t<std::is_convertible_v<U*, T*>>>
  WeakPtr(const WeakPtr<U>& other) : ptr(other.ptr), cb(other.cb) {
    if (cb) {
      ++cb->weak_counter;
    }
  }

  template <typename U, class = std::enable_if_t<std::is_convertible_v<U*, T*>>>
  WeakPtr(WeakPtr<U>&& other) : ptr(std::move(other.ptr)), cb(std::move(other.cb)) 
  {
    other.ptr = nullptr;
    other.cb = nullptr;
  }

  template <typename U, typename V, std::enable_if_t<std::is_base_of_v<T, V>, std::true_type>>
  WeakPtr(const WeakPtr<U>& other, V* p) : ptr(p), cb(other.cb) {
    if (cb) {
      ++cb->weak_counter;
    }
  }

  template <typename U, typename V, std::enable_if_t<std::is_base_of_v<T, V>, std::true_type>>
  WeakPtr(WeakPtr<U>&& other, V* p) : ptr(p), cb(std::move(other.cb)) {
    other.ptr = nullptr;
    other.cb = nullptr;
  }

// Конструкторы от SharedPtr<T>
  WeakPtr(const SharedPtr<T>& other) noexcept : ptr(other.ptr), cb(other.cb) {
    if (cb) {
      ++cb->weak_counter;
    }
  }

  WeakPtr(SharedPtr<T>&& other) noexcept : ptr(other.ptr), cb(other.cb) {
    other.ptr = nullptr;
    other.cb = nullptr;
  }

  template <typename U, class = std::enable_if_t<std::is_convertible_v<U*, T*>>>
  WeakPtr(const SharedPtr<U>& other) : ptr(other.ptr), cb(other.cb) {
    if (cb) {
      ++cb->weak_counter;
    }
  }

  template <typename U, class = std::enable_if_t<std::is_convertible_v<U*, T*>>>
  WeakPtr(SharedPtr<U>&& other) : ptr(std::move(other.ptr)), cb(std::move(other.cb)) {
    other.ptr = nullptr;
    other.cb = nullptr;
  }

// Операторы присваивания
  WeakPtr& operator=(const SharedPtr<T>& other) noexcept {
    if (cb) {
      --cb->weak_counter;
      Delete_check();
    }
    ptr = other.ptr;
    cb = other.cb;
    if (cb) {
      ++cb->weak_counter;
    }
    return *this;
  }

  WeakPtr& operator=(SharedPtr<T>&& other) noexcept {
    
    if (cb) {
      --cb->weak_counter;
      Delete_check();
    }
    ptr = other.ptr;
    cb = other.cb;
    other.ptr = nullptr;
    other.cb = nullptr;
    return *this;
  }

  WeakPtr& operator=(const WeakPtr& other) noexcept {
    if (this == &other) {
      return *this;
    }
    if (cb) {
      --cb->weak_counter;
      Delete_check();
    }
    ptr = other.ptr;
    cb = other.cb;
    if (cb) {
      ++cb->weak_counter;
    }
    return *this;
  }

  WeakPtr& operator=(WeakPtr&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    if (cb) {
      --cb->weak_counter;
      Delete_check();
    }
    ptr = other.ptr;
    cb = other.cb;
    other.ptr = nullptr;
    other.cb = nullptr;
    return *this;
  }

  template <typename U, class = std::enable_if_t<std::is_convertible_v<U*, T*>>>
  WeakPtr& operator=(const WeakPtr<U>& other) {
    if (cb) {
      --cb->weak_counter;
      Delete_check();
    }
    ptr = other.ptr;
    cb = other.cb;
    ++cb->weak_counter;
    return *this;
  }

  template <typename U, class = std::enable_if_t<std::is_convertible_v<U*, T*>>>
  WeakPtr& operator=(WeakPtr<U>&& other) {
    if (cb) {
      --cb->weak_counter;
      Delete_check();
    }
    ptr = other.ptr;
    cb = other.cb;
    other.ptr = nullptr;
    other.cb = nullptr;
    return *this;
  }

  size_t use_count() const {
    if (!cb) {
      return 0;
    }
    return cb->counter;
  }

  bool expired() const {
    if (cb) {
      return (cb->counter == 0);
    }
    return true;
  }

  SharedPtr<T> lock() const {
    return expired() ? SharedPtr<T>() : SharedPtr<T>(*this);
  }

  template <typename U>
  void reset(U* other) {
    ptr = other;
    if (cb) {
      --cb->weak_counter;
      Delete_check();
    }
    cb = new ControlBlockStandard(other);
    ++cb->weak_counter;
  }

  void reset() noexcept {
    if (cb) {
      --cb->weak_counter;
      Delete_check();
    }
    cb = nullptr;
    ptr = nullptr;
  }

  void swap(WeakPtr& other) {
    std::swap(ptr, other.ptr);
    std::swap(cb, other.cb);
  }

  ~WeakPtr() {
    if (cb) {
      --cb->weak_counter;
      Delete_check();
      return;
    }
  }
};

template <typename U, typename... Args>
SharedPtr<U> makeShared(Args&&... args) {
  using Alloctraits = std::allocator_traits<std::allocator<U>>;
  using BlockAlloc = typename Alloctraits::template rebind_alloc<ControlBlockMakeShared<U, std::allocator<U>>>;
  BlockAlloc ba{std::allocator<U>()};
  auto* pointer = std::allocator_traits<BlockAlloc>::allocate(ba, 1);
  std::allocator<U> alloc;
  try {
    new (pointer) ControlBlockMakeShared<U>(alloc, std::forward<Args>(args)...);
    return SharedPtr<U>(reinterpret_cast<U*>(pointer->get_ptr_object()), static_cast<BaseControlBlock*>(pointer));
  } catch (...) {
    std::allocator_traits<BlockAlloc>::deallocate(ba, pointer, 1);
    throw;
  }
  return SharedPtr<U>(reinterpret_cast<U*>(pointer->get_ptr_object()), static_cast<BaseControlBlock*>(pointer));
}

template <typename U, typename Alloc, typename... Args>
SharedPtr<U> allocateShared(const Alloc& alloc, Args&&... args) {
  using Traits = std::allocator_traits<Alloc>;
  using BlockAlloc = typename Traits::template rebind_alloc<ControlBlockMakeShared<U, Alloc>>;

  BlockAlloc ba = alloc;
  auto* pointer = std::allocator_traits<BlockAlloc>::allocate(ba, 1);
  try {
    new (pointer) ControlBlockMakeShared<U, Alloc>(alloc, std::forward<Args>(args)...);
    return SharedPtr<U>(reinterpret_cast<U*>(pointer->get_ptr_object()), static_cast<BaseControlBlock*>(pointer));
  } catch (...) {
    std::allocator_traits<BlockAlloc>::deallocate(ba, pointer, 1);
    throw;
  }
}
