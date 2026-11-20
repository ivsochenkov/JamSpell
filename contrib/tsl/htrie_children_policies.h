/**
 * MIT License
 *
 * Copyright (c) 2017 Thibaut Goetghebuer-Planchon <tessil@gmx.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef TSL_HTRIE_CHILDREN_POLICIES_H
#define TSL_HTRIE_CHILDREN_POLICIES_H

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

/**
 * Children policies for tsl::htrie_hash.
 *
 * A trie_node has to map a "letter" (any integral CharT) to an owning handle
 * on a child node. How this mapping is stored is the only thing that changes
 * between policies, so it is factored out here.
 *
 * ---------------------------------------------------------------------------
 * Policy concept
 * ---------------------------------------------------------------------------
 * A policy is a (usually empty) class exposing one member alias template:
 *
 *   struct my_policy {
 *     template <class CharT, class NodeHandle, class Allocator>
 *     using children = my_container<CharT, NodeHandle, Allocator>;
 *   };
 *
 *   - CharT      : the letter type (any integral type but bool).
 *   - NodeHandle : owning smart pointer to a node (move only, null by default,
 *                  get() returns a raw pointer, element_type is the node type).
 *                  The container must never destroy nodes by itself, it only
 *                  moves/destroys handles: the handle knows how to free the
 *                  node. A handle may hold a fancy pointer (offset_ptr...), so
 *                  handles must never be relocated bitwise (memcpy/memmove):
 *                  always move-construct/move-assign them.
 *   - Allocator  : allocator used for the container's *own* storage. It can be
 *                  stateful and use fancy pointers. Rebind it with
 *                  std::allocator_traits<Allocator>::rebind_alloc<U>, allocate
 *                  through std::allocator_traits and store only
 *                  allocator_traits<...>::pointer (never a raw pointer) in the
 *                  container, so that a container placed in shared memory can
 *                  be mapped at any address. Deallocations must use the size
 *                  that was allocated.
 *
 * The container type must provide (node_type = NodeHandle::element_type):
 *
 *   children(const Allocator& alloc)            empty, keeps a copy of alloc
 *   ~children()                                 destroys the remaining handles
 *   Allocator get_allocator() const noexcept    the allocator the container was
 *                                               built with (trie nodes are
 *                                               freed with it)
 *   size_t size() const noexcept
 *   bool   empty() const noexcept
 *   void   reserve(size_t n)                    capacity hint, may be a no-op
 *   node_type* find(CharT c) const noexcept     nullptr if absent
 *   node_type* first() const noexcept           child with the smallest letter
 *   node_type* next(CharT after) const noexcept child with the smallest letter
 *                                               strictly greater than 'after'
 *   void set(CharT c, NodeHandle h)             insert or replace, h != nullptr.
 *                                               Replacing must not throw.
 *   NodeHandle release(CharT c) noexcept        detach and return (null if
 *                                               none)
 *   void erase(CharT c) noexcept                detach and destroy
 *   template<class F> void for_each(F&& f) const
 *                                               f(CharT, node_type&) in
 *                                               ascending order
 *
 * "Smallest/greater" refers to the order of the *unsigned* value of the letter
 * (so that the iteration order of a trie does not depend on the signedness of
 * char and is identical for every policy). The containers are not copyable nor
 * movable; trie_node is not either.
 * ---------------------------------------------------------------------------
 */
namespace tsl {
namespace htrie_policy {
namespace detail {

template <class CharT>
inline typename std::make_unsigned<CharT>::type to_unsigned(CharT c) noexcept {
  return static_cast<typename std::make_unsigned<CharT>::type>(c);
}

template <class CharT>
struct unsigned_less {
  bool operator()(CharT lhs, CharT rhs) const noexcept {
    return to_unsigned(lhs) < to_unsigned(rhs);
  }
};

/**
 * Raw pointer to the object a (possibly fancy) pointer points to, null if the
 * pointer is null.
 */
template <class T>
inline T* to_raw_pointer(T* ptr) noexcept {
  return ptr;
}

template <class Pointer>
inline typename std::pointer_traits<Pointer>::element_type* to_raw_pointer(
    const Pointer& ptr) noexcept {
  return !ptr ? nullptr : std::addressof(*ptr);
}

/**
 * Stores an allocator without taking any space when it is stateless (empty base
 * optimization).
 */
template <class Allocator, bool = std::is_empty<Allocator>::value>
class allocator_holder : private Allocator {
 public:
  explicit allocator_holder(const Allocator& alloc) : Allocator(alloc) {}

  Allocator& stored_allocator() noexcept { return *this; }
  const Allocator& stored_allocator() const noexcept { return *this; }
};

template <class Allocator>
class allocator_holder<Allocator, false> {
 public:
  explicit allocator_holder(const Allocator& alloc) : m_alloc(alloc) {}

  Allocator& stored_allocator() noexcept { return m_alloc; }
  const Allocator& stored_allocator() const noexcept { return m_alloc; }

 private:
  Allocator m_alloc;
};

inline unsigned popcount64(std::uint64_t x) noexcept {
#if defined(__GNUC__) || defined(__clang__)
  return static_cast<unsigned>(__builtin_popcountll(x));
#else
  x = x - ((x >> 1) & 0x5555555555555555ULL);
  x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
  x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
  return static_cast<unsigned>((x * 0x0101010101010101ULL) >> 56);
#endif
}

/** x must be != 0. */
inline unsigned ctz64(std::uint64_t x) noexcept {
#if defined(__GNUC__) || defined(__clang__)
  return static_cast<unsigned>(__builtin_ctzll(x));
#else
  unsigned n = 0;
  while ((x & 1u) == 0) {
    x >>= 1;
    ++n;
  }
  return n;
#endif
}

/* =========================================================================
 * 1. Dense array: one slot per possible letter (the historical layout).
 *
 *   + O(1) everything, no search, no reallocation.
 *   - sizeof(CharT) * 2^(8*sizeof(CharT)) bytes per trie node: 2 KiB for
 *     8-bit letters, 512 KiB for 16-bit ones, impossible for 32-bit ones.
 *
 * The fastest lookup of all the policies and the reference for 8-bit letters
 * (the layout the library always had), the most memory hungry when nodes have
 * few children. Only for sizeof(CharT) <= 2 and, for 16-bit letters, only
 * sensible for tiny tries.
 * ========================================================================= */
template <class CharT, class NodeHandle, class Allocator>
class dense_array_container : private allocator_holder<Allocator> {
  static_assert(sizeof(CharT) <= 2,
                "dense_array_children needs one slot per letter: it only "
                "supports 8 and 16 bits letters. Use flat_map_children, "
                "bitmap_children or std_map_children instead.");

  static constexpr std::size_t SLOTS = std::size_t(1)
                                       << (CHAR_BIT * sizeof(CharT));

 public:
  using node_type = typename NodeHandle::element_type;

  explicit dense_array_container(const Allocator& alloc)
      : allocator_holder<Allocator>(alloc), m_size(0), m_slots() {}
  dense_array_container(const dense_array_container&) = delete;
  dense_array_container& operator=(const dense_array_container&) = delete;

  Allocator get_allocator() const noexcept { return this->stored_allocator(); }

  std::size_t size() const noexcept { return m_size; }
  bool empty() const noexcept { return m_size == 0; }
  void reserve(std::size_t /*n*/) noexcept {}

  node_type* find(CharT c) const noexcept { return m_slots[index(c)].get(); }

  node_type* first() const noexcept { return scan(0); }

  node_type* next(CharT after) const noexcept { return scan(index(after) + 1); }

  void set(CharT c, NodeHandle h) {
    NodeHandle& slot = m_slots[index(c)];
    if (slot == nullptr) {
      ++m_size;
    }
    slot = std::move(h);
  }

  NodeHandle release(CharT c) noexcept {
    NodeHandle& slot = m_slots[index(c)];
    if (slot != nullptr) {
      --m_size;
    }
    return std::move(slot);
  }

  void erase(CharT c) noexcept {
    NodeHandle& slot = m_slots[index(c)];
    if (slot != nullptr) {
      --m_size;
      slot.reset();
    }
  }

  template <class F>
  void for_each(F&& f) const {
    std::size_t remaining = m_size;
    for (std::size_t i = 0; remaining > 0 && i < SLOTS; i++) {
      if (m_slots[i] != nullptr) {
        f(static_cast<CharT>(i), *m_slots[i]);
        --remaining;
      }
    }
  }

 private:
  static std::size_t index(CharT c) noexcept {
    return static_cast<std::size_t>(to_unsigned(c));
  }

  node_type* scan(std::size_t start) const noexcept {
    if (m_size == 0) {
      return nullptr;
    }
    for (std::size_t i = start; i < SLOTS; i++) {
      if (m_slots[i] != nullptr) {
        return m_slots[i].get();
      }
    }
    return nullptr;
  }

 private:
  std::size_t m_size;
  std::array<NodeHandle, SLOTS> m_slots;
};

/* =========================================================================
 * 2. Flat map: sorted, contiguous, structure-of-arrays, one allocation.
 *
 * Memory layout of the single block:  [handle * cap][letter * cap]
 * Letters are packed apart from the pointers so a binary search only touches
 * a few cache lines and there is no padding between a letter and a pointer.
 *
 *   + works for every CharT (8/16/32/64 bits), whatever the alphabet size
 *   + 16 bytes of header, then sizeof(handle)+sizeof(CharT) per *used* child
 *   + cache friendly lookup, ordered traversal for free
 *   - O(log n) lookup, O(n) insertion in the middle (shifts the arrays)
 *
 * The choice for wide alphabets (the only compact one for 32/64 bits letters)
 * and for sparsely populated nodes. On 8-bit letters it is slower than the
 * dense array and the bitmap (binary search instead of a direct access), on
 * 32-bit letters it matches std::map with the default burst threshold and is
 * 1.5x to 2.4x faster and 8% to 10% smaller with small burst thresholds.
 * ========================================================================= */
template <class CharT, class NodeHandle, class Allocator>
class flat_map_container : private allocator_holder<Allocator> {
  static constexpr std::size_t ALIGN = alignof(NodeHandle) > alignof(CharT)
                                           ? alignof(NodeHandle)
                                           : alignof(CharT);

  struct alignas(ALIGN) unit {
    unsigned char bytes[ALIGN];
  };

  using unit_allocator =
      typename std::allocator_traits<Allocator>::template rebind_alloc<unit>;
  using unit_traits = std::allocator_traits<unit_allocator>;
  using unit_pointer = typename unit_traits::pointer;

  using size_type = std::uint32_t;

 public:
  using node_type = typename NodeHandle::element_type;

  explicit flat_map_container(const Allocator& alloc) noexcept
      : allocator_holder<Allocator>(alloc),
        m_data(),
        m_size(0),
        m_capacity(0) {}
  flat_map_container(const flat_map_container&) = delete;
  flat_map_container& operator=(const flat_map_container&) = delete;

  ~flat_map_container() {
    if (m_data) {
      NodeHandle* hs = handles();
      for (size_type i = 0; i < m_size; i++) {
        hs[i].~NodeHandle();
      }

      unit_allocator alloc(this->stored_allocator());
      unit_traits::deallocate(alloc, m_data, units_for(m_capacity));
    }
  }

  Allocator get_allocator() const noexcept { return this->stored_allocator(); }

  std::size_t size() const noexcept { return m_size; }
  bool empty() const noexcept { return m_size == 0; }

  void reserve(std::size_t n) {
    if (n > m_capacity) {
      if (n > std::numeric_limits<size_type>::max()) {
        throw std::length_error("flat_map_children: too many children.");
      }
      reallocate(static_cast<size_type>(n));
    }
  }

  node_type* find(CharT c) const noexcept {
    if (m_size == 0) {
      return nullptr;
    }

    const std::size_t idx = lower_bound(c);
    return (idx < m_size && keys()[idx] == c) ? handles()[idx].get() : nullptr;
  }

  node_type* first() const noexcept {
    return (m_size == 0) ? nullptr : handles()[0].get();
  }

  node_type* next(CharT after) const noexcept {
    if (m_size == 0) {
      return nullptr;
    }

    const CharT* ks = keys();
    const std::size_t idx = std::size_t(
        std::upper_bound(ks, ks + m_size, after, unsigned_less<CharT>()) - ks);
    return (idx < m_size) ? handles()[idx].get() : nullptr;
  }

  void set(CharT c, NodeHandle h) {
    std::size_t idx = (m_size == 0) ? 0 : lower_bound(c);
    if (idx < m_size && keys()[idx] == c) {
      handles()[idx] = std::move(h);
      return;
    }

    if (m_size == m_capacity) {
      grow();
    }

    NodeHandle* hs = handles();
    CharT* ks = keys();
    if (idx == m_size) {
      ::new (static_cast<void*>(hs + m_size)) NodeHandle(std::move(h));
      ks[m_size] = c;
    } else {
      ::new (static_cast<void*>(hs + m_size))
          NodeHandle(std::move(hs[m_size - 1]));
      std::move_backward(hs + idx, hs + m_size - 1, hs + m_size);
      std::copy_backward(ks + idx, ks + m_size, ks + m_size + 1);
      hs[idx] = std::move(h);
      ks[idx] = c;
    }
    ++m_size;
  }

  NodeHandle release(CharT c) noexcept {
    if (m_size == 0) {
      return NodeHandle();
    }

    const std::size_t idx = lower_bound(c);
    if (idx >= m_size || keys()[idx] != c) {
      return NodeHandle();
    }

    NodeHandle* hs = handles();
    CharT* ks = keys();

    NodeHandle result(std::move(hs[idx]));
    std::move(hs + idx + 1, hs + m_size, hs + idx);
    hs[m_size - 1].~NodeHandle();
    std::copy(ks + idx + 1, ks + m_size, ks + idx);
    --m_size;

    return result;
  }

  void erase(CharT c) noexcept {
    // The removed node (if any) is destroyed once the container is consistent.
    NodeHandle removed = release(c);
    (void)removed;
  }

  template <class F>
  void for_each(F&& f) const {
    if (m_size == 0) {
      return;
    }

    NodeHandle* hs = handles();
    const CharT* ks = keys();
    for (size_type i = 0; i < m_size; i++) {
      f(ks[i], *hs[i]);
    }
  }

 private:
  static std::size_t keys_offset(std::size_t capacity) noexcept {
    const std::size_t raw = capacity * sizeof(NodeHandle);
    return (raw + alignof(CharT) - 1) / alignof(CharT) * alignof(CharT);
  }

  static std::size_t units_for(std::size_t capacity) noexcept {
    const std::size_t bytes = keys_offset(capacity) + capacity * sizeof(CharT);
    return (bytes + sizeof(unit) - 1) / sizeof(unit);
  }

  unit* data() const noexcept { return to_raw_pointer(m_data); }

  NodeHandle* handles() const noexcept {
    return reinterpret_cast<NodeHandle*>(data());
  }

  CharT* keys() const noexcept {
    return reinterpret_cast<CharT*>(reinterpret_cast<unsigned char*>(data()) +
                                    keys_offset(m_capacity));
  }

  std::size_t lower_bound(CharT c) const noexcept {
    const CharT* ks = keys();
    return std::size_t(
        std::lower_bound(ks, ks + m_size, c, unsigned_less<CharT>()) - ks);
  }

  void grow() {
    if (m_capacity == std::numeric_limits<size_type>::max()) {
      throw std::length_error("flat_map_children: too many children.");
    }

    // 2 children fit in the 24 usable bytes of the smallest malloc chunks.
    const size_type new_capacity =
        (m_capacity == 0)
            ? 2
            : (m_capacity > std::numeric_limits<size_type>::max() / 2
                   ? std::numeric_limits<size_type>::max()
                   : m_capacity * 2);
    reallocate(new_capacity);
  }

  void reallocate(size_type new_capacity) {
    unit_allocator alloc(this->stored_allocator());
    unit_pointer new_data =
        unit_traits::allocate(alloc, units_for(new_capacity));

    unit* new_raw_data = to_raw_pointer(new_data);
    NodeHandle* new_hs = reinterpret_cast<NodeHandle*>(new_raw_data);
    CharT* new_ks = reinterpret_cast<CharT*>(
        reinterpret_cast<unsigned char*>(new_raw_data) +
        keys_offset(new_capacity));

    if (m_data) {
      // Handles may hold fancy pointers: they are moved one by one, never
      // copied bitwise.
      NodeHandle* old_hs = handles();
      const CharT* old_ks = keys();
      for (size_type i = 0; i < m_size; i++) {
        ::new (static_cast<void*>(new_hs + i)) NodeHandle(std::move(old_hs[i]));
        old_hs[i].~NodeHandle();
        new_ks[i] = old_ks[i];
      }

      unit_traits::deallocate(alloc, m_data, units_for(m_capacity));
    }

    m_data = new_data;
    m_capacity = new_capacity;
  }

 private:
  unit_pointer m_data;
  size_type m_size;
  size_type m_capacity;
};

/* =========================================================================
 * 3. Bitmap: 256-bit presence bitmap + packed handle array (8-bit letters).
 *
 *   + O(1) lookup (popcount rank), ordered traversal with ctz
 *   + 32 bytes bitmap + pointer + 4 bytes, then 8 bytes per *used* child
 *     (vs 2048 bytes for the dense array)
 *   - insertion/removal shifts the packed array (at most 255 pointers)
 *   - only for sizeof(CharT) == 1
 *
 * Smaller trie nodes than the dense array (48 bytes + 8 per child instead of
 * 2056 bytes, so the gain depends on the fan-out) at the price of a slower
 * lookup: on a 400k keys benchmark (alphabet of 60 letters) lookups were about
 * 1.3x to 1.9x slower than the dense array and the whole trie was 5% to 12%
 * smaller with a small burst threshold (128 and 16), ~1% with the default one.
 * Faster than the flat map and the std::map on 8-bit letters. Worth it for byte
 * strings when there are many sparsely populated trie nodes (low burst
 * threshold, long common prefixes); keep the dense array if speed is all that
 * matters.
 * ========================================================================= */
template <class CharT, class NodeHandle, class Allocator>
class bitmap_container : private allocator_holder<Allocator> {
  static_assert(sizeof(CharT) == 1,
                "bitmap_children only supports 8 bits letters.");

  using handle_allocator = typename std::allocator_traits<
      Allocator>::template rebind_alloc<NodeHandle>;
  using handle_traits = std::allocator_traits<handle_allocator>;
  using handle_pointer = typename handle_traits::pointer;

  static constexpr unsigned WORDS = 4;

 public:
  using node_type = typename NodeHandle::element_type;

  explicit bitmap_container(const Allocator& alloc) noexcept
      : allocator_holder<Allocator>(alloc),
        m_bits(),
        m_handles(),
        m_size(0),
        m_capacity(0) {}
  bitmap_container(const bitmap_container&) = delete;
  bitmap_container& operator=(const bitmap_container&) = delete;

  ~bitmap_container() {
    if (m_handles) {
      NodeHandle* hs = handles();
      for (std::uint16_t i = 0; i < m_size; i++) {
        hs[i].~NodeHandle();
      }

      handle_allocator alloc(this->stored_allocator());
      handle_traits::deallocate(alloc, m_handles, m_capacity);
    }
  }

  Allocator get_allocator() const noexcept { return this->stored_allocator(); }

  std::size_t size() const noexcept { return m_size; }
  bool empty() const noexcept { return m_size == 0; }

  void reserve(std::size_t n) {
    if (n > 256) {
      n = 256;
    }
    if (n > m_capacity) {
      reallocate(static_cast<std::uint16_t>(n));
    }
  }

  node_type* find(CharT c) const noexcept {
    const unsigned p = position(c);
    return test(p) ? handles()[rank(p)].get() : nullptr;
  }

  node_type* first() const noexcept {
    return (m_size == 0) ? nullptr : handles()[0].get();
  }

  node_type* next(CharT after) const noexcept {
    const unsigned p = position(after);
    const std::size_t idx = rank(p) + (test(p) ? 1 : 0);
    return (idx < m_size) ? handles()[idx].get() : nullptr;
  }

  void set(CharT c, NodeHandle h) {
    const unsigned p = position(c);
    const std::size_t idx = rank(p);

    if (test(p)) {
      handles()[idx] = std::move(h);
      return;
    }

    if (m_size == m_capacity) {
      grow();
    }

    NodeHandle* hs = handles();
    if (idx == m_size) {
      ::new (static_cast<void*>(hs + m_size)) NodeHandle(std::move(h));
    } else {
      ::new (static_cast<void*>(hs + m_size))
          NodeHandle(std::move(hs[m_size - 1]));
      std::move_backward(hs + idx, hs + m_size - 1, hs + m_size);
      hs[idx] = std::move(h);
    }

    m_bits[p >> 6] |= (std::uint64_t(1) << (p & 63));
    ++m_size;
  }

  NodeHandle release(CharT c) noexcept {
    const unsigned p = position(c);
    if (!test(p)) {
      return NodeHandle();
    }

    const std::size_t idx = rank(p);
    NodeHandle* hs = handles();
    NodeHandle result(std::move(hs[idx]));
    std::move(hs + idx + 1, hs + m_size, hs + idx);
    hs[m_size - 1].~NodeHandle();

    m_bits[p >> 6] &= ~(std::uint64_t(1) << (p & 63));
    --m_size;

    return result;
  }

  void erase(CharT c) noexcept {
    NodeHandle removed = release(c);
    (void)removed;
  }

  template <class F>
  void for_each(F&& f) const {
    const NodeHandle* hs = handles();
    std::size_t idx = 0;
    for (unsigned w = 0; w < WORDS; w++) {
      std::uint64_t bits = m_bits[w];
      while (bits != 0) {
        const unsigned b = ctz64(bits);
        f(static_cast<CharT>(w * 64 + b), *hs[idx]);
        ++idx;
        bits &= bits - 1;
      }
    }
  }

 private:
  static unsigned position(CharT c) noexcept {
    return static_cast<unsigned>(static_cast<unsigned char>(c));
  }

  NodeHandle* handles() const noexcept { return to_raw_pointer(m_handles); }

  bool test(unsigned p) const noexcept {
    return ((m_bits[p >> 6] >> (p & 63)) & 1u) != 0;
  }

  /** Number of children whose letter is strictly lower than p. */
  std::size_t rank(unsigned p) const noexcept {
    const unsigned w = p >> 6;
    std::size_t r = 0;
    for (unsigned i = 0; i < w; i++) {
      r += popcount64(m_bits[i]);
    }

    return r + popcount64(m_bits[w] & ((std::uint64_t(1) << (p & 63)) - 1));
  }

  void grow() {
    reallocate(m_capacity == 0 ? std::uint16_t(2)
                               : static_cast<std::uint16_t>(std::min<unsigned>(
                                     m_capacity * 2u, 256u)));
  }

  void reallocate(std::uint16_t new_capacity) {
    handle_allocator alloc(this->stored_allocator());
    handle_pointer new_handles = handle_traits::allocate(alloc, new_capacity);

    if (m_handles) {
      // Handles may hold fancy pointers: they are moved one by one, never
      // copied bitwise.
      NodeHandle* old_hs = handles();
      NodeHandle* new_hs = to_raw_pointer(new_handles);
      for (std::uint16_t i = 0; i < m_size; i++) {
        ::new (static_cast<void*>(new_hs + i)) NodeHandle(std::move(old_hs[i]));
        old_hs[i].~NodeHandle();
      }

      handle_traits::deallocate(alloc, m_handles, m_capacity);
    }

    m_handles = new_handles;
    m_capacity = new_capacity;
  }

 private:
  std::array<std::uint64_t, WORDS> m_bits;
  handle_pointer m_handles;
  std::uint16_t m_size;
  std::uint16_t m_capacity;
};

/* =========================================================================
 * 4. std::map: reference implementation / node based.
 *
 *   + any CharT, O(log n) everything, no shifting on insertion, works with
 *     stateful allocators (but not with fancy pointers: not for shared memory)
 *   - ~48 bytes + pointer per child, one allocation per child, pointer
 *     chasing: usually the slowest (up to ~2.5x slower than the flat map with
 *     small burst thresholds) and the largest. Mostly useful as a baseline.
 * ========================================================================= */
template <class CharT, class NodeHandle, class Allocator>
class std_map_container {
  using value_type = std::pair<const CharT, NodeHandle>;
  using map_allocator = typename std::allocator_traits<
      Allocator>::template rebind_alloc<value_type>;
  using map_type =
      std::map<CharT, NodeHandle, unsigned_less<CharT>, map_allocator>;

  // The standard library node based containers are only required to work with
  // raw pointers, and some of them (std::map in libstdc++) don't with fancy
  // ones. Use flat_map_children or bitmap_children for shared memory.
  static_assert(
      std::is_same<typename std::allocator_traits<map_allocator>::pointer,
                   value_type*>::value,
      "std_map_children does not support allocators with fancy pointers (the "
      "pointers of std::map nodes are not relocatable): use "
      "flat_map_children or bitmap_children.");

 public:
  using node_type = typename NodeHandle::element_type;

  explicit std_map_container(const Allocator& alloc)
      : m_map(unsigned_less<CharT>(), map_allocator(alloc)) {}
  std_map_container(const std_map_container&) = delete;
  std_map_container& operator=(const std_map_container&) = delete;

  Allocator get_allocator() const noexcept {
    return Allocator(m_map.get_allocator());
  }

  std::size_t size() const noexcept { return m_map.size(); }
  bool empty() const noexcept { return m_map.empty(); }
  void reserve(std::size_t /*n*/) noexcept {}

  node_type* find(CharT c) const noexcept {
    auto it = m_map.find(c);
    return (it == m_map.end()) ? nullptr : it->second.get();
  }

  node_type* first() const noexcept {
    return m_map.empty() ? nullptr : m_map.begin()->second.get();
  }

  node_type* next(CharT after) const noexcept {
    auto it = m_map.upper_bound(after);
    return (it == m_map.end()) ? nullptr : it->second.get();
  }

  void set(CharT c, NodeHandle h) {
    auto it = m_map.find(c);
    if (it != m_map.end()) {
      it->second = std::move(h);
    } else {
      m_map.emplace(c, std::move(h));
    }
  }

  NodeHandle release(CharT c) noexcept {
    auto it = m_map.find(c);
    if (it == m_map.end()) {
      return NodeHandle();
    }

    NodeHandle result(std::move(it->second));
    m_map.erase(it);
    return result;
  }

  void erase(CharT c) noexcept {
    NodeHandle removed = release(c);
    (void)removed;
  }

  template <class F>
  void for_each(F&& f) const {
    for (auto it = m_map.begin(); it != m_map.end(); ++it) {
      f(it->first, *it->second);
    }
  }

 private:
  // mutable-free: handles are pointers, constness of the map is shallow.
  map_type m_map;
};

}  // namespace detail

/** One slot per letter (historical behaviour). 8 and 16 bits letters only. */
struct dense_array_children {
  template <class CharT, class NodeHandle, class Allocator>
  using children = detail::dense_array_container<CharT, NodeHandle, Allocator>;
};

/** Sorted contiguous (letters, handles) arrays. Any letter size. */
struct flat_map_children {
  template <class CharT, class NodeHandle, class Allocator>
  using children = detail::flat_map_container<CharT, NodeHandle, Allocator>;
};

/** 256-bit bitmap + packed handles. 8 bits letters only. */
struct bitmap_children {
  template <class CharT, class NodeHandle, class Allocator>
  using children = detail::bitmap_container<CharT, NodeHandle, Allocator>;
};

/** std::map based, mostly a reference implementation. Any letter size. */
struct std_map_children {
  template <class CharT, class NodeHandle, class Allocator>
  using children = detail::std_map_container<CharT, NodeHandle, Allocator>;
};

/**
 * Policy used when none is specified: dense array for 8 bits letters (same
 * behaviour/performance as before), flat map for anything wider.
 */
template <class CharT>
using default_children_policy =
    typename std::conditional<sizeof(CharT) == 1, dense_array_children,
                              flat_map_children>::type;

}  // namespace htrie_policy
}  // namespace tsl

#endif
