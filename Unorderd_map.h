#include <iostream>
#include <cmath>
#include <memory>
#include <vector>

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
  void construct(U* ptr, Args&&... args) {
    new(ptr) U(std::forward(args)...);
  }

  void destroy(T* ptr) {
    ptr->~T();
  }

  void deallocate(T* ptr, size_t count) {
    (void)ptr;
    (void)count;
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

  template <typename Key, typename Value, typename Hash, typename Equal, typename Alloc>
  friend class UnorderedMap;
  
  struct BaseNode {
    BaseNode* prev = nullptr;
    BaseNode* next = nullptr;
    BaseNode() = default;
    BaseNode(BaseNode* ptr_p, BaseNode* ptr_n) 
    : prev(ptr_p), next(ptr_n) {}
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
  NodeAlloc allocator;
  
  template<typename U>
  void ConstructNodes(size_t number, U&& value, NodeAlloc alloc) {
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

  template <typename U>
  void common_push_back(U&& value) {
    try {
      Node* ptr = std::allocator_traits<NodeAlloc>::allocate(allocator, 1);
      std::allocator_traits<NodeAlloc>::construct(allocator, ptr, value);
      BaseNode* last = &endNode;
      if (sz > 0) {
        last = endNode.prev;
      }
      ptr->prev = last;
      ptr->next = &endNode;
      last->next = ptr;
      endNode.prev = ptr;
      ++sz;
    } catch(...) {
      throw;
    }
  }

  template <typename U>
  void common_push_front(U&& value) {
    try {
      Node* ptr = std::allocator_traits<NodeAlloc>::allocate(allocator, 1);
      std::allocator_traits<NodeAlloc>::construct(allocator, ptr, value);
      BaseNode* first = &endNode;
      if (sz > 0) {
        first = endNode.next;
      }
      ptr->next = first;
      ptr->prev = &endNode;
      first->prev = ptr;
      endNode.next = ptr;
      ++sz;
    } catch(...) {
      throw;
    }
  }

  template <typename U>
  void common_insert(base_iterator<true> iter, U&& value) {
    try {
      Node* ptr = std::allocator_traits<NodeAlloc>::allocate(allocator, 1);
      std::allocator_traits<NodeAlloc>::construct(allocator, ptr, value);
      Node* iter_node = static_cast<Node*>(const_cast<BaseNode*>(iter.current));
      ptr->prev = iter_node->prev;
      iter_node->prev->next = ptr;
      ptr->next = iter_node;
      iter_node->prev = ptr;
      ++sz;
    } catch(...) {
      throw;
    }
  }

 public:
  using iterator = base_iterator<false>;
  using const_iterator = base_iterator<true>;
  using reverse_iterator = std::reverse_iterator<base_iterator<false>>;
  using const_reverse_iterator = std::reverse_iterator<base_iterator<true>>;
  List() {
    endNode.next = &endNode;
    endNode.prev = &endNode;
  }

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
    ConstructNodes<const T&>(number, value);
  }

  List(size_t number, T&& value) {
    ConstructNodes<T&&>(number, value);
  }

  List(const Allocator& alloc) : allocator(std::move(alloc)) {
    endNode.prev = &endNode;
    endNode.next = &endNode;
  }

  List(size_t number, const Allocator& alloc) : allocator(std::move(alloc)) {
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
    : allocator(std::move(alloc)) 
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

  List(List&& other) 
    : allocator(std::move(other.allocator)) 
  {
    if (other.sz == 0) {
      return;
    }
    this -> ~List();
    std::swap(endNode, other.endNode);
    std::swap(sz, other.sz);
  }

  List& operator=(const List& other) {
    constexpr bool change = std::is_base_of_v<std::true_type, typename Traits::propagate_on_container_copy_assignment>;
    if (other.sz == 0) {
      BaseNode* ptr = endNode.next;
      while (ptr != &endNode) {
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
        newend = CopyNodes(other.sz, other.endNode, allocator); 
      }
    } catch(...) {
      throw;
    }
    BaseNode* ptr = endNode.next;
    while (ptr != &endNode) {
      std::allocator_traits<NodeAlloc>::destroy(allocator, static_cast<Node*>(ptr));
      ptr = ptr->next;
    }
    std::allocator_traits<NodeAlloc>::deallocate(allocator, static_cast<Node*>(endNode.next), sz);
    endNode = newend;
    endNode.prev->next = &endNode;
    endNode.next->prev = &endNode;
    sz = other.sz;
    if constexpr(change) {
      allocator = other.allocator;
    }
    return *this;
  }

  List& operator=(List&& other) {
    constexpr bool change = std::is_base_of_v<std::true_type, typename Traits::propagate_on_container_copy_assignment>;
    this->~List();

    if (other.sz == 0) {
      endNode.next = &endNode;
      endNode.prev = &endNode;
      if constexpr(change) {
        allocator = other.allocator;
      }
      sz = 0;
      other.endNode.next = &other.endNode;
      other.endNode.prev = &other.endNode;
      other.sz = 0;
      return *this;
    }

    endNode = other.endNode;
    other.endNode.next = &endNode;
    other.endNode.prev = &endNode;
    endNode.next->prev = &endNode;
    endNode.prev->next = &endNode;
    sz = other.sz;
    other.endNode.next = &other.endNode;
    other.endNode.prev = &other.endNode;
    other.sz = 0;
    if (change) {
      allocator = std::move(other.allocator);
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
    return common_push_back(value);
  }

  void push_back(T&& value) {
    return common_push_back(std::move(value));
  }

  void push_front(const T& value) {
    return common_push_front(value);
  }

  void push_front(T&& value) {
    return common_push_front(std::move(value));
  }

  void pop_back() {
    Node* last = static_cast<Node*>(endNode.prev);
    --sz;
    if (sz > 0) {
      endNode.prev = last->prev;
      last->prev->next = &endNode;
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
      endNode.next = first->next;
      first->next->prev = &endNode;
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
    return common_insert(iter, value);
  }

  void insert(const_iterator iter, T&& value) {
    return common_insert(iter, std::move(value));
  }

  void erase(const_iterator iter) {
    Node* iter_node = static_cast<Node*>(const_cast<BaseNode*>(iter.current));
    --sz;
    if (sz > 0) {
      iter_node->prev->next = iter_node->next;
      iter_node->next->prev = iter_node->prev;
      std::allocator_traits<NodeAlloc>::destroy(allocator, iter_node);
      std::allocator_traits<NodeAlloc>::deallocate(allocator, iter_node, 1);
      return;
    }
    endNode.next = &endNode;
    endNode.prev = &endNode;
    std::allocator_traits<NodeAlloc>::destroy(allocator, iter_node);
    std::allocator_traits<NodeAlloc>::deallocate(allocator, iter_node, 1);
  }

  void HelpSwap(BaseNode& frst, BaseNode& sec) {
    frst.prev = sec.prev;
    frst.next = sec.next;
    if (frst.next) {
      frst.next->prev = &frst;
    }
    if (frst.prev) {
      frst.prev->next = &frst;
    }
    sec.next = &sec;
    sec.prev = &sec;
  }

  void swap(List& other) noexcept {
    if (sz == 0 && other.sz == 0) {
      return;
    }
    if (sz == 0) {
      HelpSwap(endNode, other.endNode);
    } else if (other.sz == 0) {
      HelpSwap(other.endNode, endNode);
    } else {
      std::swap(endNode.next, other.endNode.next);
      std::swap(endNode.prev, other.endNode.prev);
      endNode.next->prev = &endNode;
      endNode.prev->next = &endNode;
      other.endNode.next->prev = &other.endNode;
      other.endNode.prev->next = &other.endNode;
    }
    std::swap(sz, other.sz);
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


template <typename Key, typename Value, typename Hash = std::hash<Key>,
          typename Equal = std::equal_to<Key>, typename Alloc = std::allocator<std::pair<const Key, Value>>>
class UnorderedMap {
 public:
  using NodeType = std::pair<const Key, Value>;
  struct Node {
    NodeType kv;
    size_t hash;

    public:
    template<typename K, typename V>
    Node(Key&& key, Value&& value, size_t h)
        : kv(std::piecewise_construct,
          std::forward_as_tuple(std::forward<K>(key)),
          std::forward_as_tuple(std::forward<V>(value))),
          hash(h) {}

    Node(const Node& other) : kv(other.kv), hash(other.hash) {}
  
    Node(Node&& other) noexcept : kv(std::move(other.kv)), hash(other.hash) {}
  
    ~Node() = default;
  };
 private:
  using NodeAlloc = typename std::allocator_traits<Alloc>::template rebind_alloc<Node>;
  using ListBaseNodePtr = typename List<Node, NodeAlloc>::BaseNode*;
  using ListNodePtr = typename List<Node, NodeAlloc>::Node*;
  using ListPtrAlloc = typename std::allocator_traits<Alloc>::template rebind_alloc<ListBaseNodePtr>;
  using ListNodeAlloc = typename std::allocator_traits<
        Alloc>::template rebind_alloc<typename List<Node, NodeAlloc>::Node>;
  
  using ListNode = typename List<Node, NodeAlloc>::Node;
  using ListBaseNode = typename List<Node, NodeAlloc>::BaseNode;
  template<typename T>
  using TraitsMap = std::allocator_traits<T>;

  template<bool IsConst>
  struct base_iterator {
  using iterator_category = std::forward_iterator_tag;
  using value_type = std::conditional_t<IsConst, const NodeType, NodeType>;
  using difference_type = std::ptrdiff_t;
  using pointer = std::conditional_t<IsConst, const NodeType*, NodeType*>;
  using reference = std::conditional_t<IsConst, const NodeType&, NodeType&>;

    using ListNode_ptr = std::conditional_t<IsConst, 
          const typename List<Node, NodeAlloc>::Node*,
          typename List<Node, NodeAlloc>::Node*>;

    using ListBaseNode_ptr = std::conditional_t<IsConst, 
          const typename List<Node, NodeAlloc>::BaseNode*,
          typename List<Node, NodeAlloc>::BaseNode*>;

    ListBaseNode_ptr ptr;

    base_iterator(ListBaseNode_ptr other) : ptr(other) {}
    
    base_iterator(const base_iterator& iter) : ptr(iter.ptr) {}

    template<bool B = IsConst, typename = std::enable_if_t<B>>
    base_iterator(const base_iterator<false>& iter) : ptr(iter.ptr) {}
    
    base_iterator& operator=(const base_iterator& iter) {
      ptr = iter.ptr;
      return *this;
    }

    base_iterator& operator++() {
      ptr = ptr->next;
      return *this;
    }

    base_iterator operator++(int) {
      base_iterator copy = *this;
      ptr = ptr->next;
      return copy;
    }

    pointer operator->() const noexcept {
      return const_cast<pointer>(&((static_cast<ListNode_ptr>(ptr)->value).kv));
    }

    reference operator*() const noexcept {
      return const_cast<reference>(static_cast<ListNode_ptr>(ptr)->value.kv);
    }

    bool operator==(const base_iterator& iter) const {
      return (ptr == iter.ptr);
    }

    bool operator!=(const base_iterator& iter) const {
      return (ptr != iter.ptr);
    }
  };

  template <typename K, bool IsConst>
  auto general_find(K&& key) -> base_iterator<IsConst> {
    if (bucket_count == 0) {
      return end();
    }
    const size_t h     = hash(key);
    const size_t index = h % bucket_count;
    ListBaseNodePtr ptr = buckets[index];

    while (ptr && ptr != &lst.endNode && 
           static_cast<ListNodePtr>(ptr)->value.hash % bucket_count == index)
    {
      if (key_equal(static_cast<ListNodePtr>(ptr)->value.kv.first, std::forward<K>(key))) {
        return base_iterator<IsConst>(ptr);
      }
      ptr = ptr->next;
    }
    return end();
  }

  void MakeConnection(ListBaseNodePtr frst, ListBaseNodePtr sec, ListBaseNodePtr ptr) {
    if (ptr->prev) {
      ptr->prev->next = ptr->next;
    }
    if (ptr->next) {
      ptr->next->prev = ptr->prev;
    }

    if (sec) {
      sec->next = ptr;
    }
    ptr->prev = sec;
    ptr->next = frst;
    frst->prev = ptr;
  }

  void Rehash_func(std::vector<ListBaseNodePtr, ListPtrAlloc>& newbuckets, List<Node, NodeAlloc>& newlst) {
    while(lst.size() > 0) {
      auto iter = lst.begin();
      ListBaseNodePtr ptr = iter.current;
      size_t h = hash(static_cast<ListNodePtr>(ptr)->value.kv.first);
      size_t index = h % bucket_count;
      if (newbuckets[index]) {
        MakeConnection(newbuckets[index], newbuckets[index]->prev, ptr);
      } else {
        MakeConnection(&newlst.endNode, newlst.endNode.prev, ptr);
        newbuckets[index] = ptr;
      }
      --lst.sz;
      ++newlst.sz;
    }
  }

 public:
  using iterator = base_iterator<false>;
  using const_iterator = base_iterator<true>;
  using reverse_iterator = std::reverse_iterator<base_iterator<false>>;
  using const_reverse_iterator = std::reverse_iterator<base_iterator<true>>;

  UnorderedMap() : lst(), buckets(0, nullptr), bucket_count(0) {};

  UnorderedMap(const UnorderedMap& other) 
    : lst(other.lst), 
      buckets(other.buckets),
      bucket_count(other.bucket_count),
      hash(other.hash),
      key_equal(other.key_equal),
      alloc(TraitsMap<Alloc>::select_on_container_copy_construction(other.alloc)),
      mx_load_factor(other.mx_load_factor)
    {}

  UnorderedMap(UnorderedMap&& other) noexcept
    : lst(std::move(other.lst)),
      buckets(std::move(other.buckets)),
      bucket_count(other.bucket_count),
      hash(other.hash),
      key_equal(other.key_equal),
      alloc(std::move(other.alloc)),
      mx_load_factor(other.mx_load_factor)
  {
    other.bucket_count = 0;
  }

  UnorderedMap& operator=(const UnorderedMap& other) {
    if (this == &other) {
      return *this;
    }
    buckets.assign(other.bucket_count, nullptr);
    bucket_count = other.bucket_count;
    hash = other.hash;
    key_equal = other.key_equal;
    mx_load_factor = other.mx_load_factor;
    lst = other.lst;
    for (auto it = lst.begin; it != lst.end(); ++it) {
      size_t index = *it.value.hash % bucket_count;
      if (buckets[index] == nullptr) {
        buckets[index] = it;
      }
    }
    constexpr bool change = std::is_base_of_v<std::true_type, typename TraitsMap<Alloc>::propagate_on_container_copy_assignment>;
    if constexpr(change) {
      alloc = other.alloc;
    }
    return *this;
  }

  UnorderedMap& operator=(UnorderedMap&& other) {
    lst = std::move(other.lst);
    buckets = std::move(other.buckets);
    bucket_count = other.bucket_count;
    hash = other.hash;
    key_equal = other.key_equal;
    mx_load_factor = other.mx_load_factor;
    constexpr bool change = std::is_base_of_v<std::true_type, typename TraitsMap<Alloc>::propagate_on_container_move_assignment>;
    if constexpr(change) {
      alloc = std::move(other.alloc);
    }
    return *this;
  }

  size_t size() {
    return lst.size();
  }

  bool empty() {
    return lst.size() == 0 ? true : false;
  }

  iterator begin()  {
    return iterator(lst.endNode.next);
  }

  iterator end()  {
    return iterator(&lst.endNode);
  }

  const_iterator begin() const {
    return const_iterator(lst.endNode.next);
  }

  const_iterator end() const {
    return const_iterator(&lst.endNode);
  }

  const_iterator cbegin() const {
    return const_iterator(lst.endNode.next);
  }

  const_iterator cend() const {
    return const_iterator(&lst.endNode);
  }

  reverse_iterator rbegin()  {
    return reverse_iterator(begin());
  }

  reverse_iterator rend()  {
    return reverse_iterator(end());
  }

  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(begin());
  }

  const_reverse_iterator rend() const {
    return const_reverse_iterator(end());
  }

  const_reverse_iterator crbegin() const {
    return const_reverse_iterator(cbegin());
  }

  const_reverse_iterator crend() const {
    return const_reverse_iterator(cend());
  }

  iterator find(const Key& key) {
    return general_find<decltype(key), false>(key);
  }

  iterator find(Key&& key) {
    return general_find<decltype(key), false>(std::move(key));
  }

  const_iterator find(const Key& key) const {
    return general_find<decltype(key), true>(key);
  }

  const_iterator find(Key&& key) const {
    return general_find<decltype(key), true>(std::move(key));
  }

  void rehash(size_t count) {
    if (count <= bucket_count || count == 0) {
      return;
    }
    bucket_count = count * 2;

    std::vector<ListBaseNodePtr, ListPtrAlloc> newbuckets(bucket_count, nullptr, buckets.get_allocator());
    List<Node, NodeAlloc> newlst(alloc);
    Rehash_func(newbuckets, newlst);
    lst.swap(newlst);
    buckets.swap(newbuckets);
  }

  void reserve(size_t count) {
    size_t b = std::ceil(count / mx_load_factor);
    if (b > bucket_count) {
      rehash(b);
    }
  }

  template<typename... Args>
  auto emplace(Args&&... args) -> std::pair<iterator, bool> {
    if (bucket_count == 0) {
      rehash(1);
    }
    if (load_factor() > max_load_factor()) {
      rehash(bucket_count + 1);
    }
    ListNodePtr ptr = TraitsMap<ListNodeAlloc>::allocate(lst.get_allocator(), 1);
    bool flag = false;
    try {
      TraitsMap<Alloc>::construct(alloc, std::addressof(ptr->value.kv), std::forward<Args>(args)...);

      flag = true;

      size_t h = hash(ptr->value.kv.first);
      ptr->value.hash = h;

      size_t index = h % bucket_count;
      ListBaseNodePtr point = buckets[index];
      while (point && static_cast<ListNodePtr>(point)->value.hash == ptr->value.hash) {
        if (key_equal(static_cast<ListNodePtr>(point)->value.kv.first, ptr->value.kv.first)) {
          TraitsMap<Alloc>::destroy(alloc, ptr);
          TraitsMap<ListNodeAlloc>::deallocate(lst.get_allocator(), ptr, 1);
          return {iterator(point), false};
        }
        point = point->next;
      }

      if (buckets[index] == nullptr) {
        buckets[index] = ptr;
        ptr->prev = lst.endNode.prev;
        ptr->next = &lst.endNode;
        lst.endNode.prev->next = ptr;
        lst.endNode.prev = ptr;
      } else {
        ptr->next = buckets[index];
        ptr->prev = buckets[index]->prev;
        buckets[index]->prev->next = ptr;
        buckets[index]->prev = ptr;
        buckets[index] = ptr;
      }
      ++lst.sz;
      return {iterator(ptr), true};
    } catch(...) {
      if (flag) {
        TraitsMap<Alloc>::destroy(alloc, ptr);
      }
      TraitsMap<ListNodeAlloc>::deallocate(lst.get_allocator(), ptr, 1);
      throw;
    }
  }

  auto insert(const NodeType& node) -> std::pair<iterator, bool> {
    if (find(node.first) != end()) {
      return {find(node.first), false};
    }
    return emplace(const_cast<Key&>(node.first), const_cast<Value&>(node.second));
  }

  auto insert(NodeType&& node) -> std::pair<iterator, bool> {
    if (find(node.first) != end()) {
      return {find(node.first), false};
    }
    return emplace(const_cast<Key&&>(std::move(node.first)), const_cast<Value&&>(std::move(node.second)));
  }

  template<typename InputIt>
  void insert(InputIt it1, InputIt it2) {
    while (it1 != it2) {
      insert(std::forward<decltype(*it1)>(*it1));
      ++it1;
    }
  }

  void erase(iterator iter) {
    size_t h = hash(iter->first) % bucket_count;
    if (iter.ptr == buckets[h]) {
      if (h == static_cast<ListNodePtr>(iter.ptr->next)->value.hash) {
        buckets[h] = iter.ptr->next;
      } else {
        buckets[h] = nullptr;
      }
    }
    lst.erase(iter.ptr);
  }

  void erase(iterator it1, iterator it2) {
    while(it1 != it2) {
      erase(it1++);
    }
  }

  float load_factor() const {
    return bucket_count ? static_cast<float>(lst.size()) / static_cast<float>(bucket_count) : 0.0f;
  }
  
  float max_load_factor() const {
    return mx_load_factor;
  }

  void max_load_factor(float ml) {
    mx_load_factor = ml;
  }

  template<typename T, typename U>
  void swap(const UnorderedMap<T, U>& other) {
    lst.swap(other.lst);
    std::swap(other.buckets, buckets);
    std::swap(bucket_count, other.bucket_count);
    std::swap(hash, other.hash);
    std::swap(key_equal, other.key_equal);
    std::swap(mx_load_factor, other.mx_load_factor);
    if (TraitsMap<Alloc>::propogate_on_container_swap::value) {
      std::swap(alloc, other.alloc);
    }
  }

  Value& operator[](const Key& key) {
    auto iter = find(key);
    if (iter != end()) {
      return iter->second;
    }
    return emplace(key, Value()).first->second;
  }

  Value& operator[](Key&& key) {
    auto iter = find(key);
    if (iter != end()) {
      return iter->second;
    }
    return emplace(std::move(key), Value()).first->second;
  }

  Value& at(const Key& key) {
    auto iter = find(key);
    if (iter == end()) {
      throw std::out_of_range("No object with this key");
    }
    return iter->second;
  }

  ~UnorderedMap() = default;

 private:
  List<Node, NodeAlloc> lst;
  std::vector<ListBaseNodePtr, ListPtrAlloc> buckets;
  size_t bucket_count = 0;
  [[no_unique_address]] Hash hash;
  [[no_unique_address]] Equal key_equal;
  [[no_unique_address]] Alloc alloc;
  float mx_load_factor = 0.75F;
};