#include <iostream>
#include <functional>
#include <typeinfo>
#include <type_traits>
#include <concepts>
#include <cassert>
#include <numeric>
#include <vector>
#include <memory>


template <bool MoveOnly, typename>
class BaseFunction;

template <bool MoveOnly, typename Ret, typename... Args>
class BaseFunction<MoveOnly, Ret(Args...)> {
private:
  using invoke_ptr_t = Ret(*)(void*, Args...);
  using destroy_ptr_t = void(*)(void*);
  using construct_t = void*(*)(void*, char*);
  using get_target_type_t = const std::type_info&(*)(void*);

// ControlBlock Implementation
  struct ControlBlockBase {
    construct_t construct_ptr;
    destroy_ptr_t destroy_ptr;
    get_target_type_t get_target_type_ptr;
  };

  template <typename F>
  struct ControlBlock : ControlBlockBase {
    ControlBlock() {
      this->destroy_ptr = reinterpret_cast<destroy_ptr_t>(&destroyer<F>);
      this->construct_ptr = reinterpret_cast<construct_t>(&construct_by_ptr<F>);
      this->get_target_type_ptr = reinterpret_cast<get_target_type_t>(&get_target_type<F>);
    }
  };

  template <typename F>
  ControlBlock<F>* getControlBlock() {
    static ControlBlock<F> cb;
    return &cb;
  }  

// Static Functions Implementation
  template <typename F>
  static Ret invoker(F* f, Args... args) {
    return std::invoke<F, Args...>(std::forward<F>(*f), std::forward<Args>(args)...);
  }

  template <typename F>
  static void* construct_by_ptr(F* other_fptr, char* buffer) {
    if constexpr(sizeof(F) > 16) {
      return new F(std::forward<F>(*other_fptr));
    } else {
      return new(buffer) F(std::forward<F>(*other_fptr));
    }
  }

  template <typename F>
  void* construct_by_value(F&& func, char* buffer) {
    if constexpr(sizeof(F) > 16) {
      return new F(std::forward<F>(func));
    } else {
      return new(buffer) F(std::forward<F>(func));
    }
  }

  template <typename F>
  static const std::type_info& get_target_type(F* fptr) {
    return typeid(F);
  };

  template <typename F>
  static void destroyer(F* f) {
    if constexpr(sizeof(F) > 16) {
      delete f;
    } else {
      f->~F();
    }
  }

public:

// Constructors
  BaseFunction() : fptr(nullptr) {}

  BaseFunction(std::nullptr_t) : fptr(nullptr) {}

  template <typename F, typename D = std::decay_t<F>,
            typename = std::enable_if_t<
                        !std::is_same_v<D, BaseFunction>
                        && std::invocable<F, Args...> 
    && std::convertible_to<std::invoke_result_t<F,Args...>, Ret>
            >>
  BaseFunction(F&& func)
          : invoke_ptr(reinterpret_cast<invoke_ptr_t>(&invoker<std::decay_t<F>>))
  {
    using F_noref = std::decay_t<F>;
    fptr = construct_by_value<F_noref>(std::forward<F_noref>(func), buffer);
    cb = getControlBlock<F_noref>();
  }

// Copy-constructors
  BaseFunction(const BaseFunction& other) requires(!MoveOnly) : cb(other.cb), invoke_ptr(other.invoke_ptr) {
    fptr = cb->construct_ptr(other.fptr, buffer);
  }

  BaseFunction(BaseFunction&& other) noexcept : cb(std::move(other.cb)), invoke_ptr(other.invoke_ptr) {
    fptr = cb->construct_ptr(std::move(other.fptr), buffer);
    other.fptr = nullptr;
    other.cb = nullptr;
    other.invoke_ptr = nullptr;
  }

// Copy/move-assignment operators
  BaseFunction& operator=(const BaseFunction& other) requires(!MoveOnly) {
    if (fptr) {
      cb->destroy_ptr(fptr);
    }
    cb = other.cb;
    invoke_ptr = other.invoke_ptr;
    fptr = cb->construct_ptr(other.fptr, buffer);
    return *this;
  }

  
  BaseFunction& operator=(BaseFunction&& other) {
    if (fptr) {
      cb->destroy_ptr(fptr);
    }
    cb = std::move(other.cb);
    invoke_ptr = other.invoke_ptr;
    fptr = cb->construct_ptr(std::move(other.fptr), buffer);
    other.fptr = nullptr;
    other.cb = nullptr;
    other.invoke_ptr = nullptr;
    return *this;
  }

// F&& assignment-operator

  // template <typename F, typename D = std::decay_t<F>, typename = std::enable_if_t<!std::is_same_v<D, BaseFunction>>>
  template <typename F, typename D = std::decay_t<F>,
            typename = std::enable_if_t<
                        !std::is_same_v<D, BaseFunction>
                        && std::invocable<F, Args...> 
    && std::convertible_to<std::invoke_result_t<F,Args...>, Ret>
            >>
  BaseFunction& operator=(F&& func) {
    if (fptr) {
      cb->destroy_ptr(fptr);
    }
    using F_noref = std::decay_t<F>;
    fptr = construct_by_value<F_noref>(std::forward<F_noref>(func), buffer);
    cb = getControlBlock<F_noref>();
    invoke_ptr = reinterpret_cast<invoke_ptr_t>(&invoker<F_noref>);
    return *this;
  }

// Ref_wrapper assignment operator
  template <typename F>
  BaseFunction& operator=(std::reference_wrapper<F> func) requires(!MoveOnly) {
    if (fptr) {
      cb->destroy_ptr(fptr);
    }
    using F_noref = std::decay_t<std::reference_wrapper<F>>;
    fptr = construct_by_value<F_noref>(func, buffer);
    cb = getControlBlock<F_noref>();
    invoke_ptr = reinterpret_cast<invoke_ptr_t>(&invoker<F_noref>);
    return *this;
  }

// Operator bool()
  operator bool() const {
    return fptr != nullptr;
  }

// Target, target-type
  const std::type_info& target_type() const noexcept {
    if (fptr) {
      return cb->get_target_type_ptr(fptr);
    }
    return typeid(void);
  }

  template <typename T>
  T* target() noexcept {
    if (target_type() == typeid(T)) {
      return fptr;
    }
    return nullptr;
  }

  template <typename T>
  const T* target() const noexcept {
    if (target_type() == typeid(T)) {
      return fptr;
    }
    return nullptr;
  }


// Destructor
  ~BaseFunction() {
    if (fptr) {
      cb->destroy_ptr(fptr);
    }
  }

  Ret operator()(Args... args) const {
    if (!fptr) {
      throw std::bad_function_call();
    }
    return invoke_ptr(fptr, std::forward<Args>(args)...);
  }

private:
  static const size_t BUFFER_SIZE = 16;
  void* fptr;
  alignas(max_align_t) char buffer[BUFFER_SIZE];
  invoke_ptr_t invoke_ptr;

  ControlBlockBase* cb;
};

template <bool MoveOnly, typename R, typename... Args>
bool operator==(const BaseFunction<MoveOnly, R(Args...)>& f, std::nullptr_t) {
  return !f;
}

template <bool MoveOnly, typename R, typename... Args>
bool operator!=(const BaseFunction<MoveOnly, R(Args...)>& f, std::nullptr_t) {
  return f;
}


template <typename Signature>
using Function = BaseFunction<false, Signature>;

template <typename Signature>
using MoveOnlyFunction = BaseFunction<true, Signature>;

template<typename> struct function_traits;

template<typename C, typename R, typename... Args>
struct function_traits<R(C::*)(Args...) const> {
  using type = R(Args...);
};

template <typename Ret, typename... Args>
BaseFunction(Ret(*)(Args...)) -> BaseFunction<false, Ret(Args...)>;

template <typename Ret, typename... Args>
BaseFunction(Ret(*)(Args...)) -> BaseFunction<true, Ret(Args...)>;

template<typename F>
BaseFunction(F) -> BaseFunction<false, typename function_traits<decltype(&std::remove_cvref_t<F>::operator())>::type>;

template<typename F>
BaseFunction(F) -> BaseFunction<true, typename function_traits<decltype(&std::remove_cvref_t<F>::operator())>::type>;
