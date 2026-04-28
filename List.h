#include <iostream>
#include <memory>


template<size_t N>
class StackStorage {
 private:
  std::byte arr[N];
  size_t top = 0;
 public:
  StackStorage() {
    top = 0;
  }
  StackStorage(const StackStorage& storage) = delete;
  StackStorage operator=(const StackStorage& storage) = delete;

  void* allocate(size_t count, size_t alignment) {
    void* current = static_cast<void*>(arr + top);
    size_t space = N - top;
    void* aligned = std::align(alignment, count, current, space);
    if (!aligned) {
        throw std::bad_alloc();
    }
    size_t offset = static_cast<char*>(aligned) - reinterpret_cast<char*>(current);
    top += offset + count;
    return aligned;
  }

  ~StackStorage() = default;
};

template<typename T, size_t N>
class StackAllocator {
 private:
  StackStorage<N>* storage_ptr = nullptr;
 public:
  using value_type = T;
  using pointer = T*;
  using const_pointer = const T*;
  StackAllocator() = default;

  StackAllocator(StackStorage<N>& stor) : storage_ptr(&stor) {}

  StackAllocator(const StackAllocator& other) : storage_ptr(other.storage_ptr) {}

  template<typename U>
  StackAllocator(const StackAllocator<U, N>& other) {
    storage_ptr = other.get_data();
  }

  StackAllocator& operator=(const StackAllocator& other) {
    if (this != &other) {
      storage_ptr = other.get_data();
    }
    return *this;
  }

  pointer allocate(size_t count) {
    void* ptr = storage_ptr->allocate(count * sizeof(T), alignof(T));
    return reinterpret_cast<T*>(ptr);
  }

  template<typename U>
  struct rebind {
    using other = StackAllocator<U, N>;
  };

  template<typename U, typename... Args>
  void construct(U* ptr, const Args&... args) {
    new(ptr) U(args...);
  }

  void destroy(T* ptr) {
    ptr->~T();
  }

  void deallocate(T*, size_t) {
    return;
  }

  StackStorage<N>* get_data() const {
    return storage_ptr;
  }

  ~StackAllocator() = default;
};

template<typename T, typename U, size_t N>
bool operator==(const StackAllocator<T, N>& frst, const StackAllocator<U, N>& second) {
  if constexpr(std::is_same<T, U>::value) {
    if (frst.get_data() == second.get_data()) {
      return true;
    }
  }
  return false;
}

template<typename T, typename U, size_t N>
bool operator!=(const StackAllocator<T, N>& frst, const StackAllocator<U, N>& second) {
  return !(frst == second);
}


template<typename T, typename Allocator = std::allocator<T>>
class List {
 private:
  struct BaseNode {
    BaseNode* prev = nullptr;
    BaseNode* next = nullptr;
    BaseNode() = default;
    BaseNode(BaseNode* ptr_p, BaseNode* ptr_n) 
    : prev(ptr_p), next(ptr_n) {}

    void link(BaseNode* p, BaseNode* n) {
      prev = p;
      next = n;
      p->next = this;
      n->prev = this;
    }

    void unlink() {
      prev->next = next;
      next->prev = prev;
    }
  };

  struct Node : BaseNode {
    T value;
    Node() : BaseNode(), value() {}
    Node(BaseNode* ptr_p, BaseNode* ptr_n, const T& val) : BaseNode(ptr_p, ptr_n), value(val) {}
    Node(const T& val) : value(val) {}
  };

  BaseNode endNode;
  size_t sz = 0;
  using NodeAlloc = typename std::allocator_traits<Allocator>::template rebind_alloc<Node>;
  using Traits = std::allocator_traits<NodeAlloc>;
  [[ no_unique_address ]] NodeAlloc allocator;
  
  void ConstructNodes(size_t number, const T& value, NodeAlloc alloc) {
    BaseNode* prev = &endNode;
    for (size_t i = 0; i < number; ++i) {
      Node* ptr = std::allocator_traits<NodeAlloc>::allocate(alloc, 1);
      try {
        std::allocator_traits<NodeAlloc>::construct(alloc, ptr, value);
      } catch(...) {
        std::allocator_traits<NodeAlloc>::deallocate(alloc, ptr, 1);
        while(prev != &endNode) {
          Node* node = static_cast<Node*>(prev);
          prev = prev->prev;
          std::allocator_traits<NodeAlloc>::destroy(node);
          std::allocator_traits<NodeAlloc>::deallocate(alloc, node, 1);
        }
        throw;
      }
      prev->next = ptr;
      ptr->prev = prev;
      prev = ptr;
    }
    endNode.prev = prev;
    prev->next = &endNode; 
  }

  BaseNode CopyNodes(size_t number, const BaseNode& other_ptr, NodeAlloc alloc) {
    BaseNode newend;
    BaseNode* prev = &newend;
    Node* val_ptr = static_cast<Node*>(other_ptr.next);
    for (size_t i = 0; i < number; ++i) {
      Node* ptr = std::allocator_traits<NodeAlloc>::allocate(alloc, 1);
      try {
        std::allocator_traits<NodeAlloc>::construct(alloc, ptr, val_ptr->value);
      } catch(...) {
        std::allocator_traits<NodeAlloc>::deallocate(alloc, ptr, 1);
        while(prev != &newend) {
          Node* node = static_cast<Node*>(prev);
          prev = prev->prev;
          std::allocator_traits<NodeAlloc>::destroy(alloc, node);
          std::allocator_traits<NodeAlloc>::deallocate(alloc, node, 1);
        }
        throw;
      }
      prev->next = ptr;
      ptr->prev = prev;
      prev = ptr;
      val_ptr = static_cast<Node*>(val_ptr->next);
    }
    newend.prev = prev;
    prev->next = &newend;
    return newend;
  }

  template<bool IsConst>
  class base_iterator {
    public:
    using pointer = std::conditional_t<IsConst, const T*, T*>;
    using value_type = T;
    using reference = std::conditional_t<IsConst, const T&, T&>;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::bidirectional_iterator_tag;
    using BaseNode_ptr = std::conditional_t<IsConst, const BaseNode*, BaseNode*>;
    using Node_ptr = std::conditional_t<IsConst, const Node*, Node*>;

    BaseNode_ptr current;
    base_iterator(BaseNode_ptr pointer) : current(pointer) {}
    base_iterator(const base_iterator& iter) : current(iter.current) {}
    
    template<bool B = IsConst, typename = std::enable_if_t<B>>
    base_iterator(const base_iterator<false>& iter) : current(iter.current) {}

    base_iterator& operator=(const base_iterator& other) = default;

    base_iterator& operator++() {
      current = current->next;
      return *this;
    }

    base_iterator operator++(int) {
      base_iterator copy = *this;
      current = current->next;
      return copy;
    }

    base_iterator& operator--() {
      current = current->prev;
      return *this;
    }

    base_iterator operator--(int) {
      base_iterator copy = *this;
      current = current->prev;
      return copy;
    }

    reference operator*() const {
      return static_cast<Node_ptr>(current)->value;
    }

    pointer operator->() const {
      return &(static_cast<Node*>(current)->value);
    }

    bool operator==(const base_iterator& iter) const {
      return (current == iter.current);
    }

    bool operator!=(const base_iterator& iter) const {
      return (current != iter.current);
    }
  };

 public:
  using iterator = base_iterator<false>;
  using const_iterator = base_iterator<true>;
  using reverse_iterator = std::reverse_iterator<base_iterator<false>>;
  using const_reverse_iterator = std::reverse_iterator<base_iterator<true>>;
  List() = default;

  List(size_t number) {
    BaseNode* prev = &endNode;
    for (size_t i = 0; i < number; ++i) {
      Node* ptr = std::allocator_traits<NodeAlloc>::allocate(allocator, 1);
      ptr->prev = prev;
      prev->next = ptr;
      prev = ptr;
    }
    prev->next = &endNode;
    endNode.prev = prev;
    sz = number;
  }

  List(size_t number, const T& value) {
    ConstructNodes(number, value);
  }

  List(const Allocator& alloc) : allocator(alloc) {}

  List(size_t number, const Allocator& alloc) : allocator(alloc) {
    sz = number;
    BaseNode* prev = &endNode;

    for (size_t i = 0; i < number; ++i) {
      Node* ptr = std::allocator_traits<NodeAlloc>::allocate(allocator, 1);
      std::allocator_traits<NodeAlloc>::construct(allocator, ptr);
      ptr->prev = prev;
      prev->next = ptr;
      prev = ptr;
    }
    endNode.prev = prev;
    prev->next = &endNode;
  }

  List(size_t number, const T& value, const Allocator& alloc) 
    : allocator(alloc) 
  {
    ConstructNodes(number, value);
  }

  NodeAlloc& get_allocator() {
    return allocator;
  }

  List(const List& other) 
    : allocator(Traits::select_on_container_copy_construction(other.allocator)) 
  {
    if (other.sz == 0) {
      return;
    }
    BaseNode newend;
    try {
      newend = CopyNodes(other.sz, other.endNode, allocator);
    } catch(...) {
      throw;
    }
    endNode = newend;
    endNode.prev->next = &endNode;
    endNode.next->prev = &endNode;
    sz = other.sz;
  }

  List& operator=(const List& other) {
    constexpr bool change = std::is_base_of_v<std::true_type, typename Traits::propagate_on_container_copy_assignment>;
    if (other.sz == 0) {
      BaseNode* ptr = endNode.next;
      while (ptr != &endNode && sz != 0) {
        std::allocator_traits<NodeAlloc>::destroy(allocator, static_cast<Node*>(ptr));
        ptr = ptr->next;
      }
      std::allocator_traits<NodeAlloc>::deallocate(allocator, static_cast<Node*>(endNode.next), sz);
      endNode.next = nullptr;
      endNode.prev = nullptr;
      if constexpr(change) {
        allocator = other.allocator;
      }
      sz = 0;
      return *this;
    }
    BaseNode newend;
    try {
      if constexpr(change) {
        newend = CopyNodes(other.sz, other.endNode, other.allocator);
      } else {
        if (other.sz < sz) {
          Node* ptr = static_cast<Node*>(endNode.next);
          Node* other_ptr = static_cast<Node*>(other.endNode.next);
          for (size_t i = 0; i < other.sz; ++i) {
            ptr->value = other_ptr->value;
            ptr = static_cast<Node*>(ptr->next);
            other_ptr = static_cast<Node*>(other_ptr->next);
          }
          endNode.prev = ptr->prev;
          ptr->prev->next = &endNode;
          for (size_t i = other.sz; i < sz; ++i) {
            Node* prev = static_cast<Node*>(ptr->next);
            std::allocator_traits<NodeAlloc>::destroy(allocator, ptr);
            std::allocator_traits<NodeAlloc>::deallocate(allocator, ptr, 1);
            ptr = prev;
          }
          sz = other.sz;
          return *this;
        } else {
          newend = CopyNodes(other.sz, other.endNode, allocator); 
        }
      }
    } catch(...) {
      throw;
    }
    BaseNode* ptr = endNode.next;
    while (ptr != &endNode) {
      std::allocator_traits<NodeAlloc>::destroy(allocator, static_cast<Node*>(ptr));
      ptr = ptr->next;
      std::allocator_traits<NodeAlloc>::deallocate(allocator, static_cast<Node*>(ptr->prev), 1);
    }
    endNode = newend;
    endNode.prev->next = &endNode;
    endNode.next->prev = &endNode;
    newend.next = nullptr;
    newend.prev = nullptr;
    sz = other.sz;
    if constexpr(change) {
      allocator = other.allocator;
    }
    return *this;
  }

  size_t size() const {
    return sz;
  }

  bool empty() const {
    return (sz == 0);
  }

  void push_back(const T& value) {
    try {
      Node* ptr = std::allocator_traits<NodeAlloc>::allocate(allocator, 1);
      std::allocator_traits<NodeAlloc>::construct(allocator, ptr, value);
      BaseNode* last = &endNode;
      if (sz > 0) {
        last = endNode.prev;
      }
      ptr->link(last, &endNode);
      ++sz;
    } catch(...) {
      throw;
    }
  }

  void push_front(const T& value) {
    try {
      Node* ptr = std::allocator_traits<NodeAlloc>::allocate(allocator, 1);
      std::allocator_traits<NodeAlloc>::construct(allocator, ptr, value);
      BaseNode* first = &endNode;
      if (sz > 0) {
        first = endNode.next;
      }
      ptr->link(&endNode, first);
      ++sz;
    } catch(...) {
      throw;
    }
  }

  void pop_back() {
    Node* last = static_cast<Node*>(endNode.prev);
    --sz;
    if (sz > 0) {
      last->unlink();
      std::allocator_traits<NodeAlloc>::destroy(allocator, last);
      std::allocator_traits<NodeAlloc>::deallocate(allocator, last, 1);
      return;
    }
    endNode.next = nullptr;
    endNode.prev = nullptr;
    std::allocator_traits<NodeAlloc>::destroy(allocator, last);
    std::allocator_traits<NodeAlloc>::deallocate(allocator, last, 1);
    return;
  }

  void pop_front() {
    Node* first = static_cast<Node*>(endNode.next);
    --sz;
    if (sz > 0) {
      first->unlink();
      std::allocator_traits<NodeAlloc>::destroy(allocator, first);
      std::allocator_traits<NodeAlloc>::deallocate(allocator, first, 1);
      return;
    }
    endNode.next = nullptr;
    endNode.prev = nullptr;
    std::allocator_traits<NodeAlloc>::destroy(allocator, first);
    std::allocator_traits<NodeAlloc>::deallocate(allocator, first, 1);
    return;
  }

  iterator begin() {
    return iterator(endNode.next);
  }

  iterator end() {
    return iterator(&endNode);
  }

  const_iterator begin() const {
    return const_iterator(endNode.next);
  }

  const_iterator end() const {
    return const_iterator(&endNode);
  }

  const_iterator cbegin() const {
    return const_iterator(endNode.next);
  }

  const_iterator cend() const {
    return const_iterator(&endNode.next);
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
    return std::make_reverse_iterator(cend());
  }

  const_reverse_iterator crend() const {
    return std::make_reverse_iterator(cbegin());
  }

  void insert(const_iterator iter, const T& value) {
    try {
      Node* ptr = std::allocator_traits<NodeAlloc>::allocate(allocator, 1);
      std::allocator_traits<NodeAlloc>::construct(allocator, ptr, value);
      Node* iter_node = static_cast<Node*>(const_cast<BaseNode*>(iter.current));
      ptr->link(iter_node->prev, iter_node);
      ++sz;
    } catch(...) {
      throw;
    }
  }

  void erase(const_iterator iter) {
    Node* iter_node = static_cast<Node*>(const_cast<BaseNode*>(iter.current));
    --sz;
    if (sz > 0) {
      iter_node->unlink();
      std::allocator_traits<NodeAlloc>::destroy(allocator, iter_node);
      std::allocator_traits<NodeAlloc>::deallocate(allocator, iter_node, 1);
      return;
    }
    endNode.next = nullptr;
    endNode.prev = nullptr;
    std::allocator_traits<NodeAlloc>::destroy(allocator, iter_node);
    std::allocator_traits<NodeAlloc>::deallocate(allocator, iter_node, 1);
  }

  ~List() {
    if (sz == 0) {
      return;
    }
    BaseNode* ptr = endNode.prev;
    while(&endNode != ptr) {
      Node* node = static_cast<Node*>(ptr);
      ptr = ptr->prev;
      std::allocator_traits<NodeAlloc>::destroy(allocator, node);
      std::allocator_traits<NodeAlloc>::deallocate(allocator, node, 1);
    }
  }
};