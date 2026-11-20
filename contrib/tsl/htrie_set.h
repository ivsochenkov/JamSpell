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
#ifndef TSL_HTRIE_SET_H
#define TSL_HTRIE_SET_H

#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>

#include "htrie_hash.h"

namespace tsl {

/**
 * Implementation of a hat-trie set.
 *
 * A key is a sequence of "letters" of type CharT, which can be any integral
 * type (but bool): char, wchar_t, char16_t, char32_t, std::uint16_t, unsigned
 * int, ... Where a key is expected (insert, find, erase, ...) it can be given
 * as a null-terminated array (`const CharT*`) or as any contiguous container
 * of CharT having data() and size() (std::basic_string, std::basic_string_view,
 * std::vector<CharT>, std::array<CharT, N>, ...). The `_ks` variants take a
 * pointer and a size instead. `iterator::key()` returns a
 * std::basic_string<CharT> when it is usable with CharT (char, wchar_t,
 * char16_t, char32_t, char8_t) and a std::vector<CharT> otherwise, see
 * `key_buffer_type`.
 *
 * The size of a key string is limited to std::numeric_limits<KeySizeT>::max()
 * - 1. That is 65 535 characters by default, but can be raised with the
 * KeySizeT template parameter. See max_key_size() for an easy access to this
 * limit.
 *
 * ChildrenPolicy selects how the inner (trie) nodes store their children, see
 * tsl/htrie_children_policies.h. The default is a dense array indexed by the
 * letter for 8 bits letters and a sorted flat map for wider ones.
 *
 * Allocator is the allocator all the memory of the set comes from. It can be
 * stateful and use fancy pointers, so the set can be built in a shared
 * memory segment or a memory mapped file (for example with a
 * boost::interprocess::allocator) and mapped at different addresses by
 * different processes. See the README for the requirements.
 *
 * Iterators invalidation:
 *  - clear, operator=: always invalidate the iterators.
 *  - insert: always invalidate the iterators.
 *  - erase: always invalidate the iterators.
 */
template <class CharT, class Hash = tsl::detail_htrie_hash::default_hash<CharT>,
          class KeySizeT = std::uint16_t,
          class ChildrenPolicy =
              tsl::htrie_policy::default_children_policy<CharT>,
          class Allocator = std::allocator<char>>
class htrie_set {
 private:
  template <typename U>
  using is_iterator = tsl::detail_array_hash::is_iterator<U>;

  using ht = tsl::detail_htrie_hash::htrie_hash<CharT, void, Hash, KeySizeT,
                                                ChildrenPolicy, Allocator>;

 public:
  using char_type = typename ht::char_type;
  using key_size_type = typename ht::key_size_type;
  using size_type = typename ht::size_type;
  using hasher = typename ht::hasher;
  using allocator_type = typename ht::allocator_type;
  using children_policy = typename ht::children_policy;
  using key_buffer_type = typename ht::key_buffer_type;
  using key_view = tsl::detail_htrie_hash::basic_key_view<CharT>;
  using iterator = typename ht::iterator;
  using const_iterator = typename ht::const_iterator;
  using prefix_iterator = typename ht::prefix_iterator;
  using const_prefix_iterator = typename ht::const_prefix_iterator;

 public:
  explicit htrie_set(const Hash& hash = Hash(),
                     const Allocator& alloc = Allocator())
      : m_ht(hash, ht::HASH_NODE_DEFAULT_MAX_LOAD_FACTOR,
             ht::DEFAULT_BURST_THRESHOLD, alloc) {}

  explicit htrie_set(const Allocator& alloc) : htrie_set(Hash(), alloc) {}

  explicit htrie_set(size_type burst_threshold, const Hash& hash = Hash(),
                     const Allocator& alloc = Allocator())
      : m_ht(hash, ht::HASH_NODE_DEFAULT_MAX_LOAD_FACTOR, burst_threshold,
             alloc) {}

  htrie_set(size_type burst_threshold, const Allocator& alloc)
      : htrie_set(burst_threshold, Hash(), alloc) {}

  template <class InputIt, typename std::enable_if<
                               is_iterator<InputIt>::value>::type* = nullptr>
  htrie_set(InputIt first, InputIt last, const Hash& hash = Hash(),
            const Allocator& alloc = Allocator())
      : htrie_set(hash, alloc) {
    insert(first, last);
  }

  /**
   * Copy/move with a specific allocator, like the allocator extended
   * constructors of the standard containers.
   */
  htrie_set(const htrie_set& other, const Allocator& alloc)
      : m_ht(other.m_ht, alloc) {}

  htrie_set(htrie_set&& other, const Allocator& alloc)
      : m_ht(std::move(other.m_ht), alloc) {}

  htrie_set(std::initializer_list<key_view> init, const Hash& hash = Hash(),
            const Allocator& alloc = Allocator())
      : htrie_set(hash, alloc) {
    insert(init);
  }

  htrie_set& operator=(std::initializer_list<key_view> ilist) {
    clear();
    insert(ilist);

    return *this;
  }

  /*
   * Iterators
   */
  iterator begin() noexcept { return m_ht.begin(); }
  const_iterator begin() const noexcept { return m_ht.begin(); }
  const_iterator cbegin() const noexcept { return m_ht.cbegin(); }

  iterator end() noexcept { return m_ht.end(); }
  const_iterator end() const noexcept { return m_ht.end(); }
  const_iterator cend() const noexcept { return m_ht.cend(); }

  /*
   * Capacity
   */
  bool empty() const noexcept { return m_ht.empty(); }
  size_type size() const noexcept { return m_ht.size(); }
  size_type max_size() const noexcept { return m_ht.max_size(); }
  size_type max_key_size() const noexcept { return m_ht.max_key_size(); }

  /**
   * Call shrink_to_fit() on each hash node of the hat-trie to reduce its size.
   */
  void shrink_to_fit() { m_ht.shrink_to_fit(); }

  /*
   * Modifiers
   */
  void clear() noexcept { m_ht.clear(); }

  std::pair<iterator, bool> insert_ks(const CharT* key, size_type key_size) {
    return m_ht.insert(key, key_size);
  }

  std::pair<iterator, bool> insert(const key_view& key) {
    return m_ht.insert(key.data(), key.size());
  }

  template <class InputIt, typename std::enable_if<
                               is_iterator<InputIt>::value>::type* = nullptr>
  void insert(InputIt first, InputIt last) {
    for (auto it = first; it != last; ++it) {
      insert(*it);
    }
  }

  void insert(std::initializer_list<key_view> ilist) {
    insert(ilist.begin(), ilist.end());
  }

  std::pair<iterator, bool> emplace_ks(const CharT* key, size_type key_size) {
    return m_ht.insert(key, key_size);
  }

  std::pair<iterator, bool> emplace(const key_view& key) {
    return m_ht.insert(key.data(), key.size());
  }

  iterator erase(const_iterator pos) { return m_ht.erase(pos); }
  iterator erase(const_iterator first, const_iterator last) {
    return m_ht.erase(first, last);
  }

  size_type erase_ks(const CharT* key, size_type key_size) {
    return m_ht.erase(key, key_size);
  }

  size_type erase(const key_view& key) {
    return m_ht.erase(key.data(), key.size());
  }

  /**
   * Erase all the elements which have 'prefix' as prefix. Return the number of
   * erase elements.
   */
  size_type erase_prefix_ks(const CharT* prefix, size_type prefix_size) {
    return m_ht.erase_prefix(prefix, prefix_size);
  }

  /**
   * @copydoc erase_prefix_ks(const CharT* prefix, size_type prefix_size)
   */
  size_type erase_prefix(const key_view& prefix) {
    return m_ht.erase_prefix(prefix.data(), prefix.size());
  }

  void swap(htrie_set& other) { other.m_ht.swap(m_ht); }

  /*
   * Lookup
   */
  size_type count_ks(const CharT* key, size_type key_size) const {
    return m_ht.count(key, key_size);
  }

  size_type count(const key_view& key) const {
    return m_ht.count(key.data(), key.size());
  }

  iterator find_ks(const CharT* key, size_type key_size) {
    return m_ht.find(key, key_size);
  }

  const_iterator find_ks(const CharT* key, size_type key_size) const {
    return m_ht.find(key, key_size);
  }

  iterator find(const key_view& key) {
    return m_ht.find(key.data(), key.size());
  }

  const_iterator find(const key_view& key) const {
    return m_ht.find(key.data(), key.size());
  }

  std::pair<iterator, iterator> equal_range_ks(const CharT* key,
                                               size_type key_size) {
    return m_ht.equal_range(key, key_size);
  }

  std::pair<const_iterator, const_iterator> equal_range_ks(
      const CharT* key, size_type key_size) const {
    return m_ht.equal_range(key, key_size);
  }

  std::pair<iterator, iterator> equal_range(const key_view& key) {
    return m_ht.equal_range(key.data(), key.size());
  }

  std::pair<const_iterator, const_iterator> equal_range(
      const key_view& key) const {
    return m_ht.equal_range(key.data(), key.size());
  }

  /**
   * Return a range containing all the elements which have 'prefix' as prefix.
   * The range is defined by a pair of iterator, the first being the begin
   * iterator and the second being the end iterator.
   */
  std::pair<prefix_iterator, prefix_iterator> equal_prefix_range_ks(
      const CharT* prefix, size_type prefix_size) {
    return m_ht.equal_prefix_range(prefix, prefix_size);
  }

  /**
   * @copydoc equal_prefix_range_ks(const CharT* prefix, size_type prefix_size)
   */
  std::pair<const_prefix_iterator, const_prefix_iterator> equal_prefix_range_ks(
      const CharT* prefix, size_type prefix_size) const {
    return m_ht.equal_prefix_range(prefix, prefix_size);
  }

  /**
   * @copydoc equal_prefix_range_ks(const CharT* prefix, size_type prefix_size)
   */
  std::pair<prefix_iterator, prefix_iterator> equal_prefix_range(
      const key_view& prefix) {
    return m_ht.equal_prefix_range(prefix.data(), prefix.size());
  }

  /**
   * @copydoc equal_prefix_range_ks(const CharT* prefix, size_type prefix_size)
   */
  std::pair<const_prefix_iterator, const_prefix_iterator> equal_prefix_range(
      const key_view& prefix) const {
    return m_ht.equal_prefix_range(prefix.data(), prefix.size());
  }

  /**
   * Return the element in the trie which is the longest prefix of `key`. If no
   * element in the trie is a prefix of `key`, the end iterator is returned.
   *
   * Example:
   *
   *     tsl::htrie_set<char> set = {"/foo", "/foo/bar"};
   *
   *     set.longest_prefix("/foo"); // returns "/foo"
   *     set.longest_prefix("/foo/baz"); // returns "/foo"
   *     set.longest_prefix("/foo/bar/baz"); // returns "/foo/bar"
   *     set.longest_prefix("/foo/bar/"); // returns "/foo/bar"
   *     set.longest_prefix("/bar"); // returns end()
   *     set.longest_prefix(""); // returns end()
   */
  iterator longest_prefix_ks(const CharT* key, size_type key_size) {
    return m_ht.longest_prefix(key, key_size);
  }

  /**
   * @copydoc longest_prefix_ks(const CharT* key, size_type key_size)
   */
  const_iterator longest_prefix_ks(const CharT* key, size_type key_size) const {
    return m_ht.longest_prefix(key, key_size);
  }

  /**
   * @copydoc longest_prefix_ks(const CharT* key, size_type key_size)
   */
  iterator longest_prefix(const key_view& key) {
    return m_ht.longest_prefix(key.data(), key.size());
  }

  /**
   * @copydoc longest_prefix_ks(const CharT* key, size_type key_size)
   */
  const_iterator longest_prefix(const key_view& key) const {
    return m_ht.longest_prefix(key.data(), key.size());
  }

  /**
   * Invoke the given `visitor` function for each element in the trie which is
   * a prefix of `key`.
   *
   * @tparam F Callable target taking a single `iterator` or `const_iterator`
   *         argument.
   *
   * Example:
   *
   *     tsl::htrie_set<char> set = {"/foo", "/foo/bar"};
   *     auto print = [](tsl::htrie_set<char>::iterator it) {
   *        std::cout << it.key() << "\n";
   *     };
   *
   *     set.for_each_prefix_of("/foo", print); // prints "/foo"
   *     set.for_each_prefix_of("/foo/baz", print); // prints "/foo"
   *     set.for_each_prefix_of("/foo/bar/baz", print); // prints "/foo" and
   * "/foo/bar" set.for_each_prefix_of("/foo/bar/", print); // prints "/foo" and
   * "/foo/bar" set.for_each_prefix_of("/bar", print); // prints nothing
   *     set.for_each_prefix_of("", print); // prints nothing
   */
  template <typename F>
  void for_each_prefix_of_ks(const CharT* key, size_type key_size,
                             F&& visitor) {
    return m_ht.for_each_prefix_of(key, key_size, std::forward<F>(visitor));
  }

  /**
   * @copydoc for_each_prefix_of_ks(const CharT*, size_type, F&&)
   */
  template <typename F>
  void for_each_prefix_of_ks(const CharT* key, size_type key_size,
                             F&& visitor) const {
    return m_ht.for_each_prefix_of(key, key_size, std::forward<F>(visitor));
  }

  /**
   * @copydoc for_each_prefix_of_ks(const CharT*, size_type, F&&)
   */
  template <typename F>
  void for_each_prefix_of(const key_view& key, F&& visitor) {
    return m_ht.for_each_prefix_of(key.data(), key.size(),
                                   std::forward<F>(visitor));
  }

  /**
   * @copydoc for_each_prefix_of_ks(const CharT*, size_type, F&&)
   */
  template <typename F>
  void for_each_prefix_of(const key_view& key, F&& visitor) const {
    return m_ht.for_each_prefix_of(key.data(), key.size(),
                                   std::forward<F>(visitor));
  }

  /*
   *  Hash policy
   */
  float max_load_factor() const { return m_ht.max_load_factor(); }
  void max_load_factor(float ml) { m_ht.max_load_factor(ml); }

  /*
   * Burst policy
   */
  size_type burst_threshold() const { return m_ht.burst_threshold(); }
  void burst_threshold(size_type threshold) { m_ht.burst_threshold(threshold); }

  /*
   * Observers
   */
  hasher hash_function() const { return m_ht.hash_function(); }
  allocator_type get_allocator() const { return m_ht.get_allocator(); }

  /*
   * Other
   */

  /**
   * Serialize the set through the `serializer` parameter.
   *
   * The `serializer` parameter must be a function object that supports the
   * following calls:
   *  - `void operator()(const U& value);` where the types `std::uint64_t` and
   * `float` must be supported for U.
   *  - `void operator()(const CharT* value, std::size_t value_size);`
   *
   * The implementation leaves binary compatibility (endianness, IEEE 754 for
   * floats, ...) of the types it serializes in the hands of the `Serializer`
   * function object if compatibility is required.
   */
  template <class Serializer>
  void serialize(Serializer& serializer) const {
    m_ht.serialize(serializer);
  }

  /**
   * Deserialize a previously serialized set through the `deserializer`
   * parameter.
   *
   * The `deserializer` parameter must be a function object that supports the
   * following calls:
   *  - `template<typename U> U operator()();` where the types `std::uint64_t`
   * and `float` must be supported for U.
   *  - `void operator()(CharT* value_out, std::size_t value_size);`
   *
   * If the deserialized hash set part of the hat-trie is hash compatible with
   * the serialized set, the deserialization process can be sped up by setting
   * `hash_compatible` to true. To be hash compatible, the Hash (take care of
   * the 32-bits vs 64 bits), and KeySizeT must behave the same than the ones
   * used in the serialized set. Otherwise the behaviour is undefined with
   * `hash_compatible` sets to true.
   *
   * The behaviour is undefined if the type `CharT` of the `htrie_set` is not
   * the same as the type used during serialization.
   *
   * The implementation leaves binary compatibility (endianness, IEEE 754 for
   * floats, size of int, ...) of the types it deserializes in the hands of the
   * `Deserializer` function object if compatibility is required.
   */
  template <class Deserializer>
  static htrie_set deserialize(Deserializer& deserializer,
                               bool hash_compatible = false,
                               const Allocator& alloc = Allocator()) {
    htrie_set set(alloc);
    set.m_ht.deserialize(deserializer, hash_compatible);

    return set;
  }

  friend bool operator==(const htrie_set& lhs, const htrie_set& rhs) {
    if (lhs.size() != rhs.size()) {
      return false;
    }

    key_buffer_type key_buffer;
    for (auto it = lhs.cbegin(); it != lhs.cend(); ++it) {
      it.key(key_buffer);

      const auto it_element_rhs =
          rhs.find_ks(key_buffer.data(), key_buffer.size());
      if (it_element_rhs == rhs.cend()) {
        return false;
      }
    }

    return true;
  }

  friend bool operator!=(const htrie_set& lhs, const htrie_set& rhs) {
    return !operator==(lhs, rhs);
  }

  friend void swap(htrie_set& lhs, htrie_set& rhs) { lhs.swap(rhs); }

 private:
  ht m_ht;
};

}  // end namespace tsl

#endif
