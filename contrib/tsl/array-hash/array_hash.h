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
#ifndef TSL_ARRAY_HASH_H
#define TSL_ARRAY_HASH_H

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "array_growth_policy.h"

/*
 * __has_include is a bit useless
 * (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=79433), check also __cplusplus
 * version.
 */
#ifdef __has_include
#if __has_include(<string_view>) && __cplusplus >= 201703L
#define TSL_AH_HAS_STRING_VIEW
#endif
#endif

#ifdef TSL_AH_HAS_STRING_VIEW
#include <string_view>
#endif

/*
 * Branch hint: keeps the "no bucket" case out of the lookups' critical path
 * (the compiler may otherwise turn the test into a conditional move, which
 * makes the address of the bucket depend on a load).
 */
#if defined(__GNUC__) || defined(__clang__)
#define TSL_AH_LIKELY(expr) (__builtin_expect(static_cast<bool>(expr), 1))
#else
#define TSL_AH_LIKELY(expr) (expr)
#endif

#ifdef TSL_DEBUG
#define tsl_ah_assert(expr) assert(expr)
#else
#define tsl_ah_assert(expr) (static_cast<void>(0))
#endif

/**
 * Implementation of the array hash structure described in the
 * "Cache-conscious collision resolution in string hash tables." (Askitis
 * Nikolas and Justin Zobel, 2005) paper.
 */
namespace tsl {

namespace ah {

template <class CharT>
struct str_hash {
#ifdef TSL_AH_HAS_STRING_VIEW
  std::size_t operator()(const CharT* key, std::size_t key_size) const {
    return std::hash<std::basic_string_view<CharT>>()(
        std::basic_string_view<CharT>(key, key_size));
  }
#else
  /**
   * FNV-1a hash
   */
  std::size_t operator()(const CharT* key, std::size_t key_size) const {
    static const std::size_t init = std::size_t(
        (sizeof(std::size_t) == 8) ? 0xcbf29ce484222325 : 0x811c9dc5);
    static const std::size_t multiplier =
        std::size_t((sizeof(std::size_t) == 8) ? 0x100000001b3 : 0x1000193);

    std::size_t hash = init;
    for (std::size_t i = 0; i < key_size; ++i) {
      hash ^= key[i];
      hash *= multiplier;
    }

    return hash;
  }
#endif
};

template <class CharT>
struct str_equal {
  bool operator()(const CharT* key_lhs, std::size_t key_size_lhs,
                  const CharT* key_rhs, std::size_t key_size_rhs) const {
    if (key_size_lhs != key_size_rhs) {
      return false;
    } else {
      return std::memcmp(key_lhs, key_rhs, key_size_lhs * sizeof(CharT)) == 0;
    }
  }
};
}  // namespace ah

namespace detail_array_hash {

template <typename T, typename = void>
struct is_iterator : std::false_type {};

template <typename T>
struct is_iterator<T, typename std::enable_if<!std::is_same<
                          typename std::iterator_traits<T>::iterator_category,
                          void>::value>::type> : std::true_type {};

static constexpr bool is_power_of_two(std::size_t value) {
  return value != 0 && (value & (value - 1)) == 0;
}

template <typename T, typename U>
static T numeric_cast(U value,
                      const char* error_message = "numeric_cast() failed.") {
  T ret = static_cast<T>(value);
  if (static_cast<U>(ret) != value) {
    throw std::runtime_error(error_message);
  }

  const bool is_same_signedness =
      (std::is_unsigned<T>::value && std::is_unsigned<U>::value) ||
      (std::is_signed<T>::value && std::is_signed<U>::value);
  if (!is_same_signedness && (ret < T{}) != (value < U{})) {
    throw std::runtime_error(error_message);
  }

  return ret;
}

/**
 * Fixed size type used to represent size_type values on serialization. Need to
 * be big enough to represent a std::size_t on 32 and 64 bits platforms, and
 * must be the same size on both platforms.
 */
using slz_size_type = std::uint64_t;

template <class T, class Deserializer>
static T deserialize_value(Deserializer& deserializer) {
  // MSVC < 2017 is not conformant, circumvent the problem by removing the
  // template keyword
#if defined(_MSC_VER) && _MSC_VER < 1910
  return deserializer.Deserializer::operator()<T>();
#else
  return deserializer.Deserializer::template operator()<T>();
#endif
}

/**
 * Raw pointer to the object a (possibly fancy) pointer points to. Fancy
 * pointers (boost::interprocess::offset_ptr, ...) are what allows the
 * structures to live in memory that is mapped at different addresses.
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

template <class Allocator, class U>
using rebind_alloc_t =
    typename std::allocator_traits<Allocator>::template rebind_alloc<U>;

/**
 * Stores an allocator, without taking any space if it is stateless (empty base
 * optimization).
 */
template <class Allocator, bool = std::is_empty<Allocator>::value>
class allocator_storage : private Allocator {
 public:
  explicit allocator_storage(const Allocator& alloc) : Allocator(alloc) {}

  Allocator& char_alloc() noexcept { return *this; }
  const Allocator& char_alloc() const noexcept { return *this; }
};

template <class Allocator>
class allocator_storage<Allocator, false> {
 public:
  explicit allocator_storage(const Allocator& alloc) : m_alloc(alloc) {}

  Allocator& char_alloc() noexcept { return m_alloc; }
  const Allocator& char_alloc() const noexcept { return m_alloc; }

 private:
  Allocator m_alloc;
};

/**
 * Allocation of the buffers of the buckets.
 *
 * A buffer always has exactly the size needed by its entries plus the end of
 * bucket marker (that's how deallocate can be given the right size, as the
 * allocator requires), so the operations that change the size of a buffer
 * allocate a new one. A buffer is never modified when an operation fails.
 */
template <class CharT, class CharAllocator>
struct bucket_memory {
  using traits = std::allocator_traits<CharAllocator>;
  using pointer = typename traits::pointer;

  static constexpr bool NEEDS_SIZE_ON_DEALLOCATE = true;

  static pointer allocate(CharAllocator& alloc, std::size_t units) {
    return traits::allocate(alloc, units);
  }

  static void deallocate(CharAllocator& alloc, pointer ptr,
                         std::size_t units) noexcept {
    traits::deallocate(alloc, ptr, units);
  }

  /**
   * New buffer of new_units CharT whose first old_units CharT are the ones of
   * old_buffer, which is deallocated.
   */
  static pointer reallocate(CharAllocator& alloc, pointer old_buffer,
                            std::size_t old_units, std::size_t new_units) {
    tsl_ah_assert(new_units >= old_units);

    pointer new_buffer = traits::allocate(alloc, new_units);
    std::memcpy(to_raw_pointer(new_buffer), to_raw_pointer(old_buffer),
                old_units * sizeof(CharT));
    traits::deallocate(alloc, old_buffer, old_units);

    return new_buffer;
  }

  /**
   * New buffer of old_units - erase_units CharT which is old_buffer without
   * the erase_units CharT starting at erase_offset. old_buffer is deallocated.
   */
  static pointer erase_units(CharAllocator& alloc, pointer old_buffer,
                             std::size_t old_units, std::size_t erase_offset,
                             std::size_t erase_units) {
    tsl_ah_assert(erase_offset + erase_units <= old_units);

    pointer new_buffer = traits::allocate(alloc, old_units - erase_units);

    CharT* dst = to_raw_pointer(new_buffer);
    const CharT* src = to_raw_pointer(old_buffer);
    std::memcpy(dst, src, erase_offset * sizeof(CharT));
    std::memcpy(dst + erase_offset, src + erase_offset + erase_units,
                (old_units - erase_offset - erase_units) * sizeof(CharT));

    traits::deallocate(alloc, old_buffer, old_units);

    return new_buffer;
  }
};

/**
 * std::allocator: use std::malloc, std::realloc and std::free instead of new
 * and delete so we can have access to std::realloc (grow or shrink the buffer
 * in place when possible).
 */
template <class CharT, class U>
struct bucket_memory<CharT, std::allocator<U>> {
  using pointer = CharT*;

  static constexpr bool NEEDS_SIZE_ON_DEALLOCATE = false;

  static pointer allocate(std::allocator<U>&, std::size_t units) {
    pointer buffer = static_cast<CharT*>(std::malloc(units * sizeof(CharT)));
    if (buffer == nullptr) {
      throw std::bad_alloc();
    }

    return buffer;
  }

  static void deallocate(std::allocator<U>&, pointer ptr,
                         std::size_t /*units*/) noexcept {
    std::free(ptr);
  }

  static pointer reallocate(std::allocator<U>&, pointer old_buffer,
                            std::size_t /*old_units*/, std::size_t new_units) {
    pointer new_buffer = static_cast<CharT*>(
        std::realloc(old_buffer, new_units * sizeof(CharT)));
    if (new_buffer == nullptr) {
      throw std::bad_alloc();
    }

    return new_buffer;
  }

  static pointer erase_units(std::allocator<U>&, pointer old_buffer,
                             std::size_t old_units, std::size_t erase_offset,
                             std::size_t erase_units) {
    tsl_ah_assert(erase_offset + erase_units <= old_units);

    std::memmove(old_buffer + erase_offset,
                 old_buffer + erase_offset + erase_units,
                 (old_units - erase_offset - erase_units) * sizeof(CharT));

    // Shrinking should not fail, but if it does the old buffer is still valid.
    pointer new_buffer = static_cast<CharT*>(
        std::realloc(old_buffer, (old_units - erase_units) * sizeof(CharT)));
    return (new_buffer != nullptr) ? new_buffer : old_buffer;
  }
};

/**
 * For each string in the bucket, store the size of the string, the chars of the
 * string and T, if it's not void. T should be either void or an unsigned type.
 *
 * End the buffer with END_OF_BUCKET flag. END_OF_BUCKET has the same type as
 * the string size variable.
 *
 * m_buffer (CharT*):
 * | size of str1 (KeySizeT) | str1 (const CharT*) | value (T if T != void) |
 * ... | | size of strN (KeySizeT) | strN (const CharT*) | value (T if T !=
 * void) | END_OF_BUCKET (KeySizeT) |
 *
 * m_buffer is null if there is no string in the bucket.
 *
 * KeySizeT and T are extended to be a multiple of CharT when stored in the
 * buffer.
 *
 * The memory of the buffer comes from CharAllocator (see bucket_memory). The
 * bucket doesn't store the allocator (it would double its size), the owner of
 * the bucket gives it to the methods that allocate/deallocate and has to call
 * clear(alloc) before the bucket is destroyed.
 */
template <class CharT, class T, class KeyEqual, class KeySizeT,
          bool StoreNullTerminator, class CharAllocator>
class array_bucket {
  template <typename U>
  using has_mapped_type =
      typename std::integral_constant<bool, !std::is_same<U, void>::value>;

  static_assert(!has_mapped_type<T>::value || std::is_unsigned<T>::value,
                "T should be either void or an unsigned type.");

  static_assert(std::is_unsigned<KeySizeT>::value,
                "KeySizeT should be an unsigned type.");

 public:
  template <bool IsConst>
  class array_bucket_iterator;

  using char_type = CharT;
  using key_size_type = KeySizeT;
  using mapped_type = T;
  using size_type = std::size_t;
  using key_equal = KeyEqual;
  using iterator = array_bucket_iterator<false>;
  using const_iterator = array_bucket_iterator<true>;
  using char_allocator = CharAllocator;

 private:
  using memory = bucket_memory<CharT, CharAllocator>;
  using buffer_pointer = typename memory::pointer;

 public:
  static_assert(sizeof(KeySizeT) <= sizeof(size_type),
                "sizeof(KeySizeT) should be <= sizeof(std::size_t;)");
  static_assert(std::is_unsigned<size_type>::value, "");

 private:
  /**
   * Return how much space in bytes the type U will take when stored in the
   * buffer. As the buffer is of type CharT, U may take more space than
   * sizeof(U).
   *
   * Example: sizeof(CharT) = 4, sizeof(U) = 2 => U will take 4 bytes in the
   * buffer instead of 2.
   */
  template <typename U>
  static constexpr size_type sizeof_in_buff() noexcept {
    static_assert(is_power_of_two(sizeof(U)),
                  "sizeof(U) should be a power of two.");
    static_assert(is_power_of_two(sizeof(CharT)),
                  "sizeof(CharT) should be a power of two.");

    return std::max(sizeof(U), sizeof(CharT));
  }

  /**
   * Same as sizeof_in_buff<U>, but instead of returning the size in bytes
   * return it in term of sizeof(CharT).
   */
  template <typename U>
  static constexpr size_type size_as_char_t() noexcept {
    return sizeof_in_buff<U>() / sizeof(CharT);
  }

  static key_size_type read_key_size(const CharT* buffer) noexcept {
    key_size_type key_size;
    std::memcpy(&key_size, buffer, sizeof(key_size));

    return key_size;
  }

  static mapped_type read_value(const CharT* buffer) noexcept {
    mapped_type value;
    std::memcpy(&value, buffer, sizeof(value));

    return value;
  }

  static bool is_end_of_bucket(const CharT* buffer) noexcept {
    return read_key_size(buffer) == END_OF_BUCKET;
  }

 public:
  /**
   * Return the size required for an entry with a key of size 'key_size'.
   */
  template <class U = T, typename std::enable_if<
                             !has_mapped_type<U>::value>::type* = nullptr>
  static size_type entry_required_bytes(size_type key_size) noexcept {
    return sizeof_in_buff<key_size_type>() +
           (key_size + KEY_EXTRA_SIZE) * sizeof(CharT);
  }

  template <class U = T,
            typename std::enable_if<has_mapped_type<U>::value>::type* = nullptr>
  static size_type entry_required_bytes(size_type key_size) noexcept {
    return sizeof_in_buff<key_size_type>() +
           (key_size + KEY_EXTRA_SIZE) * sizeof(CharT) +
           sizeof_in_buff<mapped_type>();
  }

 private:
  /**
   * Return the size of the current entry in buffer.
   */
  static size_type entry_size_bytes(const CharT* buffer) noexcept {
    return entry_required_bytes(read_key_size(buffer));
  }

 public:
  template <bool IsConst>
  class array_bucket_iterator {
    friend class array_bucket;

    using buffer_type =
        typename std::conditional<IsConst, const CharT, CharT>::type;

    explicit array_bucket_iterator(buffer_type* position) noexcept
        : m_position(position) {}

   public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = void;
    using difference_type = std::ptrdiff_t;
    using reference = void;
    using pointer = void;

   public:
    array_bucket_iterator() noexcept : m_position(nullptr) {}

    const CharT* key() const {
      return m_position + size_as_char_t<key_size_type>();
    }

    size_type key_size() const { return read_key_size(m_position); }

    template <class U = T, typename std::enable_if<
                               has_mapped_type<U>::value>::type* = nullptr>
    U value() const {
      return read_value(m_position + size_as_char_t<key_size_type>() +
                        key_size() + KEY_EXTRA_SIZE);
    }

    template <class U = T, typename std::enable_if<
                               has_mapped_type<U>::value && !IsConst &&
                               std::is_same<U, T>::value>::type* = nullptr>
    void set_value(U value) noexcept {
      std::memcpy(m_position + size_as_char_t<key_size_type>() + key_size() +
                      KEY_EXTRA_SIZE,
                  &value, sizeof(value));
    }

    array_bucket_iterator& operator++() {
      m_position += entry_size_bytes(m_position) / sizeof(CharT);
      if (is_end_of_bucket(m_position)) {
        m_position = nullptr;
      }

      return *this;
    }

    array_bucket_iterator operator++(int) {
      array_bucket_iterator tmp(*this);
      ++*this;

      return tmp;
    }

    friend bool operator==(const array_bucket_iterator& lhs,
                           const array_bucket_iterator& rhs) {
      return lhs.m_position == rhs.m_position;
    }

    friend bool operator!=(const array_bucket_iterator& lhs,
                           const array_bucket_iterator& rhs) {
      return !(lhs == rhs);
    }

   private:
    buffer_type* m_position;
  };

  static iterator end_it() noexcept { return iterator(nullptr); }

  static const_iterator cend_it() noexcept { return const_iterator(nullptr); }

 public:
  array_bucket() noexcept : m_buffer() {}

  /**
   * Reserve 'reserved_bytes' (the sum of entry_required_bytes of the entries
   * that will be added) in the buffer of the bucket. The created bucket is
   * empty.
   */
  array_bucket(CharAllocator& alloc, size_type reserved_bytes) : m_buffer() {
    if (reserved_bytes == 0) {
      return;
    }

    m_buffer = memory::allocate(alloc, reserved_bytes / sizeof(CharT) +
                                           size_as_char_t<key_size_type>());

    const key_size_type end_of_bucket = END_OF_BUCKET;
    std::memcpy(buf(), &end_of_bucket, sizeof(end_of_bucket));
  }

  ~array_bucket() {
    // The owner has to call clear(alloc).
    tsl_ah_assert(!m_buffer);
  }

  array_bucket(const array_bucket& other) = delete;
  array_bucket& operator=(const array_bucket& other) = delete;

  array_bucket(array_bucket&& other) noexcept : m_buffer(other.m_buffer) {
    other.m_buffer = buffer_pointer();
  }

  array_bucket& operator=(array_bucket&& other) noexcept {
    other.swap(*this);

    return *this;
  }

  void swap(array_bucket& other) noexcept {
    std::swap(m_buffer, other.m_buffer);
  }

  /**
   * Copy of the bucket allocated with alloc.
   */
  array_bucket clone(CharAllocator& alloc) const {
    array_bucket result;
    if (!m_buffer) {
      return result;
    }

    const size_type units = size() + size_as_char_t<key_size_type>();
    result.m_buffer = memory::allocate(alloc, units);
    std::memcpy(result.buf(), buf(), units * sizeof(CharT));

    return result;
  }

  iterator begin() noexcept { return iterator(buf()); }
  iterator end() noexcept { return iterator(nullptr); }
  const_iterator begin() const noexcept { return cbegin(); }
  const_iterator end() const noexcept { return cend(); }
  const_iterator cbegin() const noexcept { return const_iterator(buf()); }
  const_iterator cend() const noexcept { return const_iterator(nullptr); }

  /**
   * Return an iterator pointing to the key entry if presents or, if not there,
   * to the position past the last element of the bucket. Return end() if the
   * bucket has not be initialized yet.
   *
   * The boolean of the pair is set to true if the key is there, false
   * otherwise.
   */
  std::pair<const_iterator, bool> find_or_end_of_bucket(
      const CharT* key, size_type key_size) const noexcept {
    if (!m_buffer) {
      return std::make_pair(cend(), false);
    }

    const CharT* buffer_ptr_in_out = buf();
    const bool found =
        find_or_end_of_bucket_impl(key, key_size, buffer_ptr_in_out);

    return std::make_pair(const_iterator(buffer_ptr_in_out), found);
  }

  /**
   * Append the element 'key' with its potential value at the end of the bucket.
   * 'end_of_bucket' should point past the end of the last element in the
   * bucket, end() if the bucket was not initialized yet. You usually get this
   * value from find_or_end_of_bucket.
   *
   * Return the position where the element was actually inserted.
   */
  template <class... ValueArgs>
  const_iterator append(CharAllocator& alloc, const_iterator end_of_bucket,
                        const CharT* key, size_type key_size,
                        ValueArgs&&... value) {
    const key_size_type key_sz = as_key_size_type(key_size);

    if (end_of_bucket == cend()) {
      tsl_ah_assert(!m_buffer);

      const size_type buffer_units =
          entry_required_bytes(key_sz) / sizeof(CharT) +
          size_as_char_t<key_size_type>();

      m_buffer = memory::allocate(alloc, buffer_units);
      append_impl(key, key_sz, buf(), std::forward<ValueArgs>(value)...);

      return const_iterator(buf());
    } else {
      tsl_ah_assert(is_end_of_bucket(end_of_bucket.m_position));

      const size_type current_units = size_type(
          (end_of_bucket.m_position + size_as_char_t<key_size_type>()) - buf());
      const size_type new_units =
          current_units + entry_required_bytes(key_sz) / sizeof(CharT);

      m_buffer = memory::reallocate(alloc, m_buffer, current_units, new_units);

      CharT* buffer_append_pos =
          buf() + current_units - size_as_char_t<key_size_type>();
      append_impl(key, key_sz, buffer_append_pos,
                  std::forward<ValueArgs>(value)...);

      return const_iterator(buffer_append_pos);
    }
  }

  const_iterator erase(CharAllocator& alloc, const_iterator position) {
    tsl_ah_assert(position.m_position != nullptr &&
                  !is_end_of_bucket(position.m_position));

    CharT* buffer = buf();

    // get mutable pointers
    CharT* start_entry = buffer + (position.m_position - buffer);
    const size_type entry_units = entry_size_bytes(start_entry) / sizeof(CharT);
    CharT* start_next_entry = start_entry + entry_units;

    CharT* end_buffer_ptr = start_next_entry;
    while (!is_end_of_bucket(end_buffer_ptr)) {
      end_buffer_ptr += entry_size_bytes(end_buffer_ptr) / sizeof(CharT);
    }
    end_buffer_ptr += size_as_char_t<key_size_type>();

    const size_type old_units = size_type(end_buffer_ptr - buffer);
    const size_type start_offset = size_type(start_entry - buffer);

    if (start_offset == 0 && is_end_of_bucket(start_next_entry)) {
      // Last element of the bucket
      memory::deallocate(alloc, m_buffer, old_units);
      m_buffer = buffer_pointer();

      return cend();
    }

    m_buffer = memory::erase_units(alloc, m_buffer, old_units, start_offset,
                                   entry_units);

    CharT* next_entry = buf() + start_offset;
    return is_end_of_bucket(next_entry) ? cend() : const_iterator(next_entry);
  }

  /**
   * Return true if an element has been erased
   */
  bool erase(CharAllocator& alloc, const CharT* key, size_type key_size) {
    if (!m_buffer) {
      return false;
    }

    const CharT* entry_buffer_ptr_in_out = buf();
    bool found =
        find_or_end_of_bucket_impl(key, key_size, entry_buffer_ptr_in_out);
    if (found) {
      erase(alloc, const_iterator(entry_buffer_ptr_in_out));

      return true;
    } else {
      return false;
    }
  }

  /**
   * Bucket should be big enough and there is no check to see if the key already
   * exists. No check on key_size.
   */
  template <class... ValueArgs>
  void append_in_reserved_bucket_no_check(const CharT* key, size_type key_size,
                                          ValueArgs&&... value) noexcept {
    CharT* buffer_ptr = buf();
    while (!is_end_of_bucket(buffer_ptr)) {
      buffer_ptr += entry_size_bytes(buffer_ptr) / sizeof(CharT);
    }

    append_impl(key, key_size_type(key_size), buffer_ptr,
                std::forward<ValueArgs>(value)...);
  }

  bool empty() const noexcept { return !m_buffer || is_end_of_bucket(buf()); }

  /**
   * Release the buffer. Has to be called before the destruction of a bucket
   * that holds one, with the allocator that allocated it.
   */
  void clear(CharAllocator& alloc) noexcept {
    if (m_buffer) {
      memory::deallocate(alloc, m_buffer,
                         memory::NEEDS_SIZE_ON_DEALLOCATE
                             ? size() + size_as_char_t<key_size_type>()
                             : 0);
      m_buffer = buffer_pointer();
    }
  }

  iterator mutable_iterator(const_iterator pos) noexcept {
    return iterator(buf() + (pos.m_position - buf()));
  }

  template <class Serializer>
  void serialize(Serializer& serializer) const {
    const slz_size_type bucket_size = size();
    tsl_ah_assert(m_buffer || bucket_size == 0);

    serializer(bucket_size);
    serializer(buf(), bucket_size);
  }

  template <class Deserializer>
  static array_bucket deserialize(CharAllocator& alloc,
                                  Deserializer& deserializer) {
    array_bucket bucket;
    const slz_size_type bucket_size_ds =
        deserialize_value<slz_size_type>(deserializer);

    if (bucket_size_ds == 0) {
      return bucket;
    }

    const std::size_t bucket_size = numeric_cast<std::size_t>(
        bucket_size_ds, "Deserialized bucket_size is too big.");
    const size_type units = bucket_size + size_as_char_t<key_size_type>();

    bucket.m_buffer = memory::allocate(alloc, units);
    try {
      deserializer(bucket.buf(), bucket_size);
    } catch (...) {
      memory::deallocate(alloc, bucket.m_buffer, units);
      bucket.m_buffer = buffer_pointer();
      throw;
    }

    const key_size_type end_of_bucket = END_OF_BUCKET;
    std::memcpy(bucket.buf() + bucket_size, &end_of_bucket,
                sizeof(end_of_bucket));

    tsl_ah_assert(bucket.size() == bucket_size);
    return bucket;
  }

 private:
  key_size_type as_key_size_type(size_type key_size) const {
    if (key_size > MAX_KEY_SIZE) {
      throw std::length_error("Key is too long.");
    }

    return key_size_type(key_size);
  }

  /*
   * Return true if found, false otherwise.
   * If true, buffer_ptr_in_out points to the start of the entry matching 'key'.
   * If false, buffer_ptr_in_out points to where the 'key' should be inserted.
   *
   * Start search from buffer_ptr_in_out.
   */
  bool find_or_end_of_bucket_impl(
      const CharT* key, size_type key_size,
      const CharT*& buffer_ptr_in_out) const noexcept {
    while (!is_end_of_bucket(buffer_ptr_in_out)) {
      const key_size_type buffer_key_size = read_key_size(buffer_ptr_in_out);
      const CharT* buffer_str =
          buffer_ptr_in_out + size_as_char_t<key_size_type>();
      if (KeyEqual()(buffer_str, buffer_key_size, key, key_size)) {
        return true;
      }

      buffer_ptr_in_out += entry_size_bytes(buffer_ptr_in_out) / sizeof(CharT);
    }

    return false;
  }

  template <typename U = T, typename std::enable_if<
                                !has_mapped_type<U>::value>::type* = nullptr>
  void append_impl(const CharT* key, key_size_type key_size,
                   CharT* buffer_append_pos) noexcept {
    std::memcpy(buffer_append_pos, &key_size, sizeof(key_size));
    buffer_append_pos += size_as_char_t<key_size_type>();

    std::memcpy(buffer_append_pos, key, key_size * sizeof(CharT));
    buffer_append_pos += key_size;

    const CharT zero = 0;
    std::memcpy(buffer_append_pos, &zero, KEY_EXTRA_SIZE * sizeof(CharT));
    buffer_append_pos += KEY_EXTRA_SIZE;

    const auto end_of_bucket = END_OF_BUCKET;
    std::memcpy(buffer_append_pos, &end_of_bucket, sizeof(end_of_bucket));
  }

  template <typename U = T,
            typename std::enable_if<has_mapped_type<U>::value>::type* = nullptr>
  void append_impl(
      const CharT* key, key_size_type key_size, CharT* buffer_append_pos,
      typename array_bucket<CharT, U, KeyEqual, KeySizeT, StoreNullTerminator,
                            CharAllocator>::mapped_type value) noexcept {
    std::memcpy(buffer_append_pos, &key_size, sizeof(key_size));
    buffer_append_pos += size_as_char_t<key_size_type>();

    std::memcpy(buffer_append_pos, key, key_size * sizeof(CharT));
    buffer_append_pos += key_size;

    const CharT zero = 0;
    std::memcpy(buffer_append_pos, &zero, KEY_EXTRA_SIZE * sizeof(CharT));
    buffer_append_pos += KEY_EXTRA_SIZE;

    std::memcpy(buffer_append_pos, &value, sizeof(value));
    buffer_append_pos += size_as_char_t<mapped_type>();

    const auto end_of_bucket = END_OF_BUCKET;
    std::memcpy(buffer_append_pos, &end_of_bucket, sizeof(end_of_bucket));
  }

  /**
   * Return the number of CharT in m_buffer. As the size of the buffer is not
   * stored to gain some space, the method need to find the EOF marker and is
   * thus in O(n).
   */
  size_type size() const noexcept {
    if (!m_buffer) {
      return 0;
    }

    const CharT* buffer_ptr = buf();
    while (!is_end_of_bucket(buffer_ptr)) {
      buffer_ptr += entry_size_bytes(buffer_ptr) / sizeof(CharT);
    }

    return size_type(buffer_ptr - buf());
  }

  CharT* buf() const noexcept { return to_raw_pointer(m_buffer); }

 private:
  static const key_size_type END_OF_BUCKET =
      std::numeric_limits<key_size_type>::max();
  static const key_size_type KEY_EXTRA_SIZE = StoreNullTerminator ? 1 : 0;

  buffer_pointer m_buffer;

 public:
  static const key_size_type MAX_KEY_SIZE =
      // -1 for END_OF_BUCKET
      key_size_type(std::numeric_limits<key_size_type>::max() - KEY_EXTRA_SIZE -
                    1);
};

template <class T, class CharAllocator>
class value_container {
 protected:
  using value_allocator = rebind_alloc_t<CharAllocator, T>;

 public:
  explicit value_container(const CharAllocator& alloc)
      : m_values(value_allocator(alloc)) {}

  value_container(const value_container& other, const CharAllocator& alloc)
      : m_values(other.m_values, value_allocator(alloc)) {}

  value_container(const value_container& other) = default;
  value_container(value_container&& other) = default;
  value_container& operator=(const value_container& other) = default;
  value_container& operator=(value_container&& other) = default;

  void clear() noexcept { m_values.clear(); }

  void reserve(std::size_t new_cap) { m_values.reserve(new_cap); }

  void shrink_to_fit() { m_values.shrink_to_fit(); }

  /**
   * Exchange the values and the allocator, whether the allocator propagates on
   * swap or not.
   */
  void swap_with_allocator(value_container& other) noexcept {
    using vector_type = decltype(m_values);

    vector_type tmp(std::move(m_values));
    m_values.~vector_type();
    ::new (static_cast<void*>(std::addressof(m_values)))
        vector_type(std::move(other.m_values));
    other.m_values.~vector_type();
    ::new (static_cast<void*>(std::addressof(other.m_values)))
        vector_type(std::move(tmp));
  }

 protected:
  static constexpr float VECTOR_GROWTH_RATE = 1.5f;

  // TODO use a sparse array? or a std::deque
  std::vector<T, value_allocator> m_values;
};

template <class CharAllocator>
class value_container<void, CharAllocator> {
 public:
  explicit value_container(const CharAllocator& /*alloc*/) {}
  value_container(const value_container& /*other*/,
                  const CharAllocator& /*alloc*/) {}

  void clear() noexcept {}

  void shrink_to_fit() {}

  void reserve(std::size_t /*new_cap*/) {}

  void swap_with_allocator(value_container& /*other*/) noexcept {}
};

/**
 * If there is no value in the array_hash (in the case of a set for example), T
 * should be void.
 *
 * The size of a key string is limited to std::numeric_limits<KeySizeT>::max()
 * - 1.
 *
 * The number of elements in the map is limited to
 * std::numeric_limits<IndexSizeT>::max().
 *
 * All the memory (buckets, buffers of the buckets, values) is allocated through
 * Allocator, which can be stateful and use fancy pointers: the only pointers
 * stored in the structure are Allocator::pointer ones, so it can be placed in
 * memory mapped at different addresses (shared memory, memory mapped files).
 * Iterators are plain C++ objects and must not be shared.
 */
template <class CharT, class T, class Hash, class KeyEqual,
          bool StoreNullTerminator, class KeySizeT, class IndexSizeT,
          class GrowthPolicy, class Allocator = std::allocator<CharT>>
class array_hash : private value_container<T, rebind_alloc_t<Allocator, CharT>>,
                   private Hash,
                   private GrowthPolicy,
                   private allocator_storage<rebind_alloc_t<Allocator, CharT>> {
 private:
  template <typename U>
  using has_mapped_type =
      typename std::integral_constant<bool, !std::is_same<U, void>::value>;

  /**
   * If there is a mapped type in array_hash, we store the values in m_values of
   * value_container class and we store an index to m_values in the bucket. The
   * index is of type IndexSizeT.
   */
  using char_allocator = rebind_alloc_t<Allocator, CharT>;
  using char_alloc_traits = std::allocator_traits<char_allocator>;
  using value_container_type = value_container<T, char_allocator>;
  using allocator_storage_type = allocator_storage<char_allocator>;

  using array_bucket = tsl::detail_array_hash::array_bucket<
      CharT,
      typename std::conditional<has_mapped_type<T>::value, IndexSizeT,
                                void>::type,
      KeyEqual, KeySizeT, StoreNullTerminator, char_allocator>;

  using bucket_allocator = rebind_alloc_t<Allocator, array_bucket>;
  using bucket_alloc_traits = std::allocator_traits<bucket_allocator>;
  using bucket_pointer = typename bucket_alloc_traits::pointer;

 public:
  template <bool IsConst>
  class array_hash_iterator;

  using char_type = CharT;
  using key_size_type = KeySizeT;
  using index_size_type = IndexSizeT;
  using size_type = std::size_t;
  using hasher = Hash;
  using key_equal = KeyEqual;
  using allocator_type = Allocator;
  using iterator = array_hash_iterator<false>;
  using const_iterator = array_hash_iterator<true>;

  /*
   * Iterator classes
   */
 public:
  template <bool IsConst>
  class array_hash_iterator {
    friend class array_hash;

   private:
    using iterator_array_bucket = typename array_bucket::const_iterator;

    using iterator_buckets =
        typename std::conditional<IsConst, const array_bucket*,
                                  array_bucket*>::type;

    using array_hash_ptr = typename std::conditional<IsConst, const array_hash*,
                                                     array_hash*>::type;

   public:
    using iterator_category = std::forward_iterator_tag;
    using value_type =
        typename std::conditional<has_mapped_type<T>::value, T, void>::type;
    using difference_type = std::ptrdiff_t;
    using reference = typename std::conditional<
        has_mapped_type<T>::value,
        typename std::conditional<
            IsConst, typename std::add_lvalue_reference<const T>::type,
            typename std::add_lvalue_reference<T>::type>::type,
        void>::type;
    using pointer = typename std::conditional<
        has_mapped_type<T>::value,
        typename std::conditional<IsConst, const T*, T*>::type, void>::type;

   private:
    array_hash_iterator(iterator_buckets buckets_iterator,
                        iterator_array_bucket array_bucket_iterator,
                        array_hash_ptr array_hash_p) noexcept
        : m_buckets_iterator(buckets_iterator),
          m_array_bucket_iterator(array_bucket_iterator),
          m_array_hash(array_hash_p) {
      tsl_ah_assert(m_array_hash != nullptr);
    }

   public:
    array_hash_iterator() noexcept
        : m_buckets_iterator(),
          m_array_bucket_iterator(),
          m_array_hash(nullptr) {}

    template <bool TIsConst = IsConst,
              typename std::enable_if<TIsConst>::type* = nullptr>
    array_hash_iterator(const array_hash_iterator<!TIsConst>& other) noexcept
        : m_buckets_iterator(other.m_buckets_iterator),
          m_array_bucket_iterator(other.m_array_bucket_iterator),
          m_array_hash(other.m_array_hash) {}

    array_hash_iterator(const array_hash_iterator& other) = default;
    array_hash_iterator(array_hash_iterator&& other) = default;
    array_hash_iterator& operator=(const array_hash_iterator& other) = default;
    array_hash_iterator& operator=(array_hash_iterator&& other) = default;

    const CharT* key() const { return m_array_bucket_iterator.key(); }

    size_type key_size() const { return m_array_bucket_iterator.key_size(); }

#ifdef TSL_AH_HAS_STRING_VIEW
    std::basic_string_view<CharT> key_sv() const {
      return std::basic_string_view<CharT>(key(), key_size());
    }
#endif

    template <class U = T, typename std::enable_if<
                               has_mapped_type<U>::value>::type* = nullptr>
    reference value() const {
      return this->m_array_hash->m_values[value_position()];
    }

    template <class U = T, typename std::enable_if<
                               has_mapped_type<U>::value>::type* = nullptr>
    reference operator*() const {
      return value();
    }

    template <class U = T, typename std::enable_if<
                               has_mapped_type<U>::value>::type* = nullptr>
    pointer operator->() const {
      return std::addressof(value());
    }

    array_hash_iterator& operator++() {
      tsl_ah_assert(m_buckets_iterator != m_array_hash->buckets_end());
      tsl_ah_assert(m_array_bucket_iterator != m_buckets_iterator->cend());

      ++m_array_bucket_iterator;
      if (m_array_bucket_iterator == m_buckets_iterator->cend()) {
        do {
          ++m_buckets_iterator;
        } while (m_buckets_iterator != m_array_hash->buckets_end() &&
                 m_buckets_iterator->empty());

        if (m_buckets_iterator != m_array_hash->buckets_end()) {
          m_array_bucket_iterator = m_buckets_iterator->cbegin();
        }
      }

      return *this;
    }

    array_hash_iterator operator++(int) {
      array_hash_iterator tmp(*this);
      ++*this;

      return tmp;
    }

    friend bool operator==(const array_hash_iterator& lhs,
                           const array_hash_iterator& rhs) {
      return lhs.m_buckets_iterator == rhs.m_buckets_iterator &&
             lhs.m_array_bucket_iterator == rhs.m_array_bucket_iterator &&
             lhs.m_array_hash == rhs.m_array_hash;
    }

    friend bool operator!=(const array_hash_iterator& lhs,
                           const array_hash_iterator& rhs) {
      return !(lhs == rhs);
    }

   private:
    template <class U = T, typename std::enable_if<
                               has_mapped_type<U>::value>::type* = nullptr>
    IndexSizeT value_position() const {
      return this->m_array_bucket_iterator.value();
    }

   private:
    iterator_buckets m_buckets_iterator;
    iterator_array_bucket m_array_bucket_iterator;

    array_hash_ptr m_array_hash;
  };

 public:
  array_hash(size_type bucket_count, const Hash& hash, float max_load_factor,
             const Allocator& alloc = Allocator())
      : value_container_type(char_allocator(alloc)),
        Hash(hash),
        GrowthPolicy(bucket_count),
        allocator_storage_type(char_allocator(alloc)),
        m_buckets(),
        m_bucket_count(0),
        m_nb_elements(0) {
    if (bucket_count > max_bucket_count()) {
      throw std::length_error("The map exceeds its maximum bucket count.");
    }

    m_buckets = allocate_bucket_array(bucket_count);
    m_bucket_count = bucket_count;

    this->max_load_factor(max_load_factor);
  }

  array_hash(const array_hash& other)
      : array_hash(other,
                   char_alloc_traits::select_on_container_copy_construction(
                       other.char_alloc())) {}

  array_hash(const array_hash& other, const Allocator& alloc)
      : value_container_type(other, char_allocator(alloc)),
        Hash(other),
        GrowthPolicy(other),
        allocator_storage_type(char_allocator(alloc)),
        m_buckets(),
        m_bucket_count(0),
        m_nb_elements(other.m_nb_elements),
        m_max_load_factor(other.m_max_load_factor),
        m_load_threshold(other.m_load_threshold) {
    m_buckets = copy_bucket_array(other);
    m_bucket_count = other.m_bucket_count;
  }

  /**
   * Take the content of other if the allocators are equal, otherwise move the
   * elements one by one in the memory of alloc (which also works for values
   * that are not copyable).
   */
  array_hash(array_hash&& other, const Allocator& alloc)
      : value_container_type(char_allocator(alloc)),
        Hash(other),
        GrowthPolicy(other),
        allocator_storage_type(char_allocator(alloc)),
        m_buckets(),
        m_bucket_count(0),
        m_nb_elements(0),
        m_max_load_factor(other.m_max_load_factor),
        m_load_threshold(0) {
    if (this->char_alloc() == other.char_alloc()) {
      static_cast<value_container_type&>(*this) =
          std::move(static_cast<value_container_type&>(other));
      m_buckets = other.m_buckets;
      m_bucket_count = other.m_bucket_count;
      m_nb_elements = other.m_nb_elements;
      m_load_threshold = other.m_load_threshold;

      other.value_container_type::clear();
      other.GrowthPolicy::clear();
      other.m_buckets = bucket_pointer();
      other.m_bucket_count = 0;
      other.m_nb_elements = 0;
      other.m_load_threshold = 0;
    } else {
      move_elements_from(other);
    }
  }

  array_hash(array_hash&& other) noexcept(
      std::is_nothrow_move_constructible<value_container_type>::value &&
      std::is_nothrow_move_constructible<Hash>::value &&
      std::is_nothrow_move_constructible<GrowthPolicy>::value)
      : value_container_type(std::move(other)),
        Hash(std::move(other)),
        GrowthPolicy(std::move(other)),
        allocator_storage_type(other.char_alloc()),
        m_buckets(other.m_buckets),
        m_bucket_count(other.m_bucket_count),
        m_nb_elements(other.m_nb_elements),
        m_max_load_factor(other.m_max_load_factor),
        m_load_threshold(other.m_load_threshold) {
    other.value_container_type::clear();
    other.GrowthPolicy::clear();
    other.m_buckets = bucket_pointer();
    other.m_bucket_count = 0;
    other.m_nb_elements = 0;
    other.m_load_threshold = 0;
  }

  ~array_hash() { release_bucket_array(m_buckets, m_bucket_count); }

  /**
   * Same allocator propagation rules as the standard containers.
   */
  array_hash& operator=(const array_hash& other) {
    if (&other != this) {
      array_hash tmp(
          other,
          Allocator(
              char_alloc_traits::propagate_on_container_copy_assignment::value
                  ? other.char_alloc()
                  : this->char_alloc()));
      tmp.swap_with_allocator(*this);
    }

    return *this;
  }

  array_hash& operator=(array_hash&& other) {
    if (&other != this) {
      if (char_alloc_traits::propagate_on_container_move_assignment::value ||
          this->char_alloc() == other.char_alloc()) {
        array_hash tmp(std::move(other));
        tmp.swap_with_allocator(*this);
      } else {
        // Can't take the memory of other, move the elements
        array_hash tmp(std::move(other), Allocator(this->char_alloc()));
        tmp.swap_with_allocator(*this);
      }
    }

    return *this;
  }

  /*
   * Iterators
   */
  iterator begin() noexcept {
    array_bucket* begin = buckets_data();
    array_bucket* end = buckets_end();
    while (begin != end && begin->empty()) {
      ++begin;
    }

    return (begin != end) ? iterator(begin, begin->cbegin(), this)
                          : this->end();
  }

  const_iterator begin() const noexcept { return cbegin(); }

  const_iterator cbegin() const noexcept {
    const array_bucket* begin = buckets_data();
    const array_bucket* end = buckets_end();
    while (begin != end && begin->empty()) {
      ++begin;
    }

    return (begin != end) ? const_iterator(begin, begin->cbegin(), this)
                          : cend();
  }

  iterator end() noexcept {
    return iterator(buckets_end(), array_bucket::cend_it(), this);
  }

  const_iterator end() const noexcept { return cend(); }

  const_iterator cend() const noexcept {
    return const_iterator(buckets_end(), array_bucket::cend_it(), this);
  }

  /*
   * Capacity
   */
  bool empty() const noexcept { return m_nb_elements == 0; }

  size_type size() const noexcept { return m_nb_elements; }

  size_type max_size() const noexcept {
    return std::numeric_limits<IndexSizeT>::max();
  }

  size_type max_key_size() const noexcept { return MAX_KEY_SIZE; }

  void shrink_to_fit() {
    clear_old_erased_values();
    value_container_type::shrink_to_fit();

    rehash_impl(size_type(std::ceil(float(size()) / max_load_factor())));
  }

  /*
   * Modifiers
   */
  void clear() noexcept {
    value_container_type::clear();

    array_bucket* buckets = buckets_data();
    for (size_type ibucket = 0; ibucket < m_bucket_count; ibucket++) {
      buckets[ibucket].clear(this->char_alloc());
    }

    m_nb_elements = 0;
  }

  template <class... ValueArgs>
  std::pair<iterator, bool> emplace(const CharT* key, size_type key_size,
                                    ValueArgs&&... value_args) {
    const std::size_t hash = hash_key(key, key_size);
    std::size_t ibucket = bucket_for_hash(hash);

    auto it_find =
        lookup_buckets()[ibucket].find_or_end_of_bucket(key, key_size);
    if (it_find.second) {
      return std::make_pair(
          iterator(buckets_data() + ibucket, it_find.first, this), false);
    }

    if (grow_on_high_load()) {
      ibucket = bucket_for_hash(hash);
      it_find = lookup_buckets()[ibucket].find_or_end_of_bucket(key, key_size);
    }

    return emplace_impl(ibucket, it_find.first, key, key_size,
                        std::forward<ValueArgs>(value_args)...);
  }

  template <class M>
  std::pair<iterator, bool> insert_or_assign(const CharT* key,
                                             size_type key_size, M&& obj) {
    auto it = emplace(key, key_size, std::forward<M>(obj));
    if (!it.second) {
      it.first.value() = std::forward<M>(obj);
    }

    return it;
  }

  iterator erase(const_iterator pos) {
    if (should_clear_old_erased_values()) {
      clear_old_erased_values();
    }

    return erase_from_bucket(mutable_iterator(pos));
  }

  iterator erase(const_iterator first, const_iterator last) {
    if (first == last) {
      return mutable_iterator(first);
    }

    /**
     * When erasing an element from a bucket with erase_from_bucket, it
     * invalidates all the iterators in the array bucket of the element
     * (m_array_bucket_iterator) but not the iterators of the buckets itself
     * (m_buckets_iterator).
     *
     * So first erase all the values between first and last which are not part
     * of the bucket of last, and then erase carefully the values in last's
     * bucket.
     */
    auto to_delete = mutable_iterator(first);
    while (to_delete.m_buckets_iterator != last.m_buckets_iterator) {
      to_delete = erase_from_bucket(to_delete);
    }

    std::size_t nb_elements_until_last = std::distance(
        to_delete.m_array_bucket_iterator, last.m_array_bucket_iterator);
    while (nb_elements_until_last > 0) {
      to_delete = erase_from_bucket(to_delete);
      nb_elements_until_last--;
    }

    if (should_clear_old_erased_values()) {
      clear_old_erased_values();
    }

    return to_delete;
  }

  size_type erase(const CharT* key, size_type key_size) {
    return erase(key, key_size, hash_key(key, key_size));
  }

  size_type erase(const CharT* key, size_type key_size, std::size_t hash) {
    if (should_clear_old_erased_values()) {
      clear_old_erased_values();
    }

    const std::size_t ibucket = bucket_for_hash(hash);
    if (lookup_buckets()[ibucket].erase(this->char_alloc(), key, key_size)) {
      m_nb_elements--;
      return 1;
    } else {
      return 0;
    }
  }

  /**
   * As for the standard containers, if the allocators are not equal and don't
   * propagate on swap, the behaviour is undefined. The allocators are swapped
   * with the content.
   */
  void swap(array_hash& other) {
    tsl_ah_assert(char_alloc_traits::propagate_on_container_swap::value ||
                  this->char_alloc() == other.char_alloc());

    swap_with_allocator(other);
  }

  /**
   * Exchange the contents and the allocators.
   */
  void swap_with_allocator(array_hash& other) {
    using std::swap;

    value_container_type::swap_with_allocator(
        static_cast<value_container_type&>(other));
    swap(static_cast<Hash&>(*this), static_cast<Hash&>(other));
    swap(static_cast<GrowthPolicy&>(*this), static_cast<GrowthPolicy&>(other));
    swap(this->char_alloc(), other.char_alloc());
    swap(m_buckets, other.m_buckets);
    swap(m_bucket_count, other.m_bucket_count);
    swap(m_nb_elements, other.m_nb_elements);
    swap(m_max_load_factor, other.m_max_load_factor);
    swap(m_load_threshold, other.m_load_threshold);
  }

  /*
   * Lookup
   */
  template <class U = T,
            typename std::enable_if<has_mapped_type<U>::value>::type* = nullptr>
  U& at(const CharT* key, size_type key_size) {
    return at(key, key_size, hash_key(key, key_size));
  }

  template <class U = T,
            typename std::enable_if<has_mapped_type<U>::value>::type* = nullptr>
  const U& at(const CharT* key, size_type key_size) const {
    return at(key, key_size, hash_key(key, key_size));
  }

  template <class U = T,
            typename std::enable_if<has_mapped_type<U>::value>::type* = nullptr>
  U& at(const CharT* key, size_type key_size, std::size_t hash) {
    return const_cast<U&>(
        static_cast<const array_hash*>(this)->at(key, key_size, hash));
  }

  template <class U = T,
            typename std::enable_if<has_mapped_type<U>::value>::type* = nullptr>
  const U& at(const CharT* key, size_type key_size, std::size_t hash) const {
    const std::size_t ibucket = bucket_for_hash(hash);

    auto it_find =
        lookup_buckets()[ibucket].find_or_end_of_bucket(key, key_size);
    if (it_find.second) {
      return this->m_values[it_find.first.value()];
    } else {
      throw std::out_of_range("Couldn't find key.");
    }
  }

  template <class U = T,
            typename std::enable_if<has_mapped_type<U>::value>::type* = nullptr>
  U& access_operator(const CharT* key, size_type key_size) {
    const std::size_t hash = hash_key(key, key_size);
    std::size_t ibucket = bucket_for_hash(hash);

    auto it_find =
        lookup_buckets()[ibucket].find_or_end_of_bucket(key, key_size);
    if (it_find.second) {
      return this->m_values[it_find.first.value()];
    } else {
      if (grow_on_high_load()) {
        ibucket = bucket_for_hash(hash);
        it_find =
            lookup_buckets()[ibucket].find_or_end_of_bucket(key, key_size);
      }

      return emplace_impl(ibucket, it_find.first, key, key_size, U{})
          .first.value();
    }
  }

  size_type count(const CharT* key, size_type key_size) const {
    return count(key, key_size, hash_key(key, key_size));
  }

  size_type count(const CharT* key, size_type key_size,
                  std::size_t hash) const {
    const std::size_t ibucket = bucket_for_hash(hash);

    auto it_find =
        lookup_buckets()[ibucket].find_or_end_of_bucket(key, key_size);
    if (it_find.second) {
      return 1;
    } else {
      return 0;
    }
  }

  iterator find(const CharT* key, size_type key_size) {
    return find(key, key_size, hash_key(key, key_size));
  }

  const_iterator find(const CharT* key, size_type key_size) const {
    return find(key, key_size, hash_key(key, key_size));
  }

  iterator find(const CharT* key, size_type key_size, std::size_t hash) {
    const std::size_t ibucket = bucket_for_hash(hash);

    auto it_find =
        lookup_buckets()[ibucket].find_or_end_of_bucket(key, key_size);
    if (it_find.second) {
      return iterator(buckets_data() + ibucket, it_find.first, this);
    } else {
      return end();
    }
  }

  const_iterator find(const CharT* key, size_type key_size,
                      std::size_t hash) const {
    const std::size_t ibucket = bucket_for_hash(hash);

    auto it_find =
        lookup_buckets()[ibucket].find_or_end_of_bucket(key, key_size);
    if (it_find.second) {
      return const_iterator(buckets_data() + ibucket, it_find.first, this);
    } else {
      return cend();
    }
  }

  std::pair<iterator, iterator> equal_range(const CharT* key,
                                            size_type key_size) {
    return equal_range(key, key_size, hash_key(key, key_size));
  }

  std::pair<const_iterator, const_iterator> equal_range(
      const CharT* key, size_type key_size) const {
    return equal_range(key, key_size, hash_key(key, key_size));
  }

  std::pair<iterator, iterator> equal_range(const CharT* key,
                                            size_type key_size,
                                            std::size_t hash) {
    iterator it = find(key, key_size, hash);
    return std::make_pair(it, (it == end()) ? it : std::next(it));
  }

  std::pair<const_iterator, const_iterator> equal_range(
      const CharT* key, size_type key_size, std::size_t hash) const {
    const_iterator it = find(key, key_size, hash);
    return std::make_pair(it, (it == cend()) ? it : std::next(it));
  }

  /*
   * Bucket interface
   */
  size_type bucket_count() const { return m_bucket_count; }

  size_type max_bucket_count() const {
    const bucket_allocator alloc(this->char_alloc());
    return std::min(GrowthPolicy::max_bucket_count(),
                    size_type(bucket_alloc_traits::max_size(alloc)));
  }

  /*
   *  Hash policy
   */
  float load_factor() const {
    if (bucket_count() == 0) {
      return 0;
    }

    return float(m_nb_elements) / float(bucket_count());
  }

  float max_load_factor() const { return m_max_load_factor; }

  void max_load_factor(float ml) {
    const float min_max_load_factor = MIN_MAX_LOAD_FACTOR;
    m_max_load_factor = std::max(min_max_load_factor, ml);
    m_load_threshold = size_type(float(bucket_count()) * m_max_load_factor);
  }

  void rehash(size_type count) {
    count = std::max(count,
                     size_type(std::ceil(float(size()) / max_load_factor())));
    rehash_impl(count);
  }

  void reserve(size_type count) {
    rehash(size_type(std::ceil(float(count) / max_load_factor())));
  }

  /*
   * Observers
   */
  hasher hash_function() const { return static_cast<const hasher&>(*this); }

  allocator_type get_allocator() const {
    return allocator_type(this->char_alloc());
  }

  // TODO add support for statefull KeyEqual
  key_equal key_eq() const { return KeyEqual(); }

  /*
   * Other
   */
  iterator mutable_iterator(const_iterator it) noexcept {
    array_bucket* it_bucket =
        buckets_data() + (it.m_buckets_iterator - buckets_data());
    return iterator(it_bucket, it.m_array_bucket_iterator, this);
  }

  template <class Serializer>
  void serialize(Serializer& serializer) const {
    serialize_impl(serializer);
  }

  template <class Deserializer>
  void deserialize(Deserializer& deserializer, bool hash_compatible) {
    deserialize_impl(deserializer, hash_compatible);
  }

 private:
  std::size_t hash_key(const CharT* key, size_type key_size) const {
    return Hash::operator()(key, key_size);
  }

  std::size_t bucket_for_hash(std::size_t hash) const {
    return GrowthPolicy::bucket_for_hash(hash);
  }

  /**
   * If there is a mapped_type, the mapped value in m_values is not erased now.
   * It will be erased when the ratio between the size of the map and
   * the size of the map + the number of deleted values still stored is low
   * enough (see clear_old_erased_values).
   */
  iterator erase_from_bucket(iterator pos) {
    auto array_bucket_next_it = pos.m_buckets_iterator->erase(
        this->char_alloc(), pos.m_array_bucket_iterator);
    m_nb_elements--;

    if (array_bucket_next_it != pos.m_buckets_iterator->cend()) {
      return iterator(pos.m_buckets_iterator, array_bucket_next_it, this);
    } else {
      do {
        ++pos.m_buckets_iterator;
      } while (pos.m_buckets_iterator != buckets_end() &&
               pos.m_buckets_iterator->empty());

      if (pos.m_buckets_iterator != buckets_end()) {
        return iterator(pos.m_buckets_iterator,
                        pos.m_buckets_iterator->cbegin(), this);
      } else {
        return end();
      }
    }
  }

  template <class U = T, typename std::enable_if<
                             !has_mapped_type<U>::value>::type* = nullptr>
  bool should_clear_old_erased_values(
      float /*threshold*/ = DEFAULT_CLEAR_OLD_ERASED_VALUE_THRESHOLD) const {
    return false;
  }

  template <class U = T,
            typename std::enable_if<has_mapped_type<U>::value>::type* = nullptr>
  bool should_clear_old_erased_values(
      float threshold = DEFAULT_CLEAR_OLD_ERASED_VALUE_THRESHOLD) const {
    if (this->m_values.size() == 0) {
      return false;
    }

    return float(m_nb_elements) / float(this->m_values.size()) < threshold;
  }

  template <class U = T, typename std::enable_if<
                             !has_mapped_type<U>::value>::type* = nullptr>
  void clear_old_erased_values() {}

  template <class U = T,
            typename std::enable_if<has_mapped_type<U>::value>::type* = nullptr>
  void clear_old_erased_values() {
    static_assert(std::is_nothrow_move_constructible<U>::value ||
                      std::is_copy_constructible<U>::value,
                  "mapped_value must be either copy constructible or nothrow "
                  "move constructible.");

    if (m_nb_elements == this->m_values.size()) {
      return;
    }

    decltype(this->m_values) new_values(this->m_values.get_allocator());
    new_values.reserve(size());

    for (auto it = begin(); it != end(); ++it) {
      new_values.push_back(std::move_if_noexcept(it.value()));
    }

    IndexSizeT ivalue = 0;
    for (auto it = begin(); it != end(); ++it) {
      auto it_array_bucket =
          it.m_buckets_iterator->mutable_iterator(it.m_array_bucket_iterator);
      it_array_bucket.set_value(ivalue);
      ivalue++;
    }

    new_values.swap(this->m_values);
    tsl_ah_assert(m_nb_elements == this->m_values.size());
  }

  /**
   * Return true if a rehash occurred.
   */
  bool grow_on_high_load() {
    if (size() >= m_load_threshold) {
      rehash_impl(GrowthPolicy::next_bucket_count());
      return true;
    }

    return false;
  }

  template <class... ValueArgs, class U = T,
            typename std::enable_if<has_mapped_type<U>::value>::type* = nullptr>
  std::pair<iterator, bool> emplace_impl(
      std::size_t ibucket, typename array_bucket::const_iterator end_of_bucket,
      const CharT* key, size_type key_size, ValueArgs&&... value_args) {
    if (this->m_values.size() >= max_size()) {
      // Try to clear old erased values lingering in m_values. Throw if it
      // doesn't change anything.
      clear_old_erased_values();
      if (this->m_values.size() >= max_size()) {
        throw std::length_error(
            "Can't insert value, too much values in the map.");
      }
    }

    if (this->m_values.size() == this->m_values.capacity()) {
      this->m_values.reserve(
          std::size_t(float(this->m_values.size()) *
                      value_container_type::VECTOR_GROWTH_RATE));
    }

    this->m_values.emplace_back(std::forward<ValueArgs>(value_args)...);

    try {
      auto it = buckets_data()[ibucket].append(
          this->char_alloc(), end_of_bucket, key, key_size,
          IndexSizeT(this->m_values.size() - 1));
      m_nb_elements++;

      return std::make_pair(iterator(buckets_data() + ibucket, it, this), true);
    } catch (...) {
      // Rollback
      this->m_values.pop_back();
      throw;
    }
  }

  template <class U = T, typename std::enable_if<
                             !has_mapped_type<U>::value>::type* = nullptr>
  std::pair<iterator, bool> emplace_impl(
      std::size_t ibucket, typename array_bucket::const_iterator end_of_bucket,
      const CharT* key, size_type key_size) {
    if (m_nb_elements >= max_size()) {
      throw std::length_error(
          "Can't insert value, too much values in the map.");
    }

    auto it = buckets_data()[ibucket].append(this->char_alloc(), end_of_bucket,
                                             key, key_size);
    m_nb_elements++;

    return std::make_pair(iterator(buckets_data() + ibucket, it, this), true);
  }

  void rehash_impl(size_type bucket_count) {
    GrowthPolicy new_growth_policy(bucket_count);
    if (bucket_count == this->bucket_count()) {
      return;
    }

    if (should_clear_old_erased_values(
            REHASH_CLEAR_OLD_ERASED_VALUE_THRESHOLD)) {
      clear_old_erased_values();
    }

    std::vector<std::size_t> required_size_for_bucket(bucket_count, 0);
    std::vector<std::size_t> bucket_for_ivalue(size(), 0);

    std::size_t ivalue = 0;
    for (auto it = begin(); it != end(); ++it) {
      const std::size_t hash = hash_key(it.key(), it.key_size());
      const std::size_t ibucket = new_growth_policy.bucket_for_hash(hash);

      bucket_for_ivalue[ivalue] = ibucket;
      required_size_for_bucket[ibucket] +=
          array_bucket::entry_required_bytes(it.key_size());
      ivalue++;
    }

    bucket_pointer new_buckets = allocate_bucket_array(bucket_count);
    array_bucket* new_buckets_raw = to_raw_pointer(new_buckets);
    try {
      for (std::size_t ibucket = 0; ibucket < bucket_count; ibucket++) {
        new_buckets_raw[ibucket] =
            array_bucket(this->char_alloc(), required_size_for_bucket[ibucket]);
      }
    } catch (...) {
      release_bucket_array(new_buckets, bucket_count);
      throw;
    }

    ivalue = 0;
    for (auto it = begin(); it != end(); ++it) {
      const std::size_t ibucket = bucket_for_ivalue[ivalue];
      append_iterator_in_reserved_bucket_no_check(new_buckets_raw[ibucket], it);

      ivalue++;
    }

    using std::swap;
    swap(static_cast<GrowthPolicy&>(*this), new_growth_policy);

    swap(m_buckets, new_buckets);
    const size_type old_bucket_count = m_bucket_count;
    m_bucket_count = bucket_count;

    // new_buckets now holds the old buckets
    release_bucket_array(new_buckets, old_bucket_count);

    // Call max_load_factor to change m_load_threshold
    max_load_factor(m_max_load_factor);
  }

  template <class U = T, typename std::enable_if<
                             !has_mapped_type<U>::value>::type* = nullptr>
  void append_iterator_in_reserved_bucket_no_check(array_bucket& bucket,
                                                   iterator it) {
    bucket.append_in_reserved_bucket_no_check(it.key(), it.key_size());
  }

  template <class U = T,
            typename std::enable_if<has_mapped_type<U>::value>::type* = nullptr>
  void append_iterator_in_reserved_bucket_no_check(array_bucket& bucket,
                                                   iterator it) {
    bucket.append_in_reserved_bucket_no_check(it.key(), it.key_size(),
                                              it.value_position());
  }

  /**
   * On serialization the values of each bucket (if has_mapped_type is true) are
   * serialized next to the bucket. The potential old erased values in
   * value_container are thus not serialized.
   *
   * On deserialization, when hash_compatible is true, we reaffect the value
   * index (IndexSizeT) of each bucket with set_value as the position of each
   * value is no more the same in value_container compared to when they were
   * serialized.
   *
   * It's done this way as we can't call clear_old_erased_values() because we
   * want the serialize method to remain const and we don't want to
   * serialize/deserialize old erased values. As we may not serialize all the
   * values in value_container, the values we keep can change of index. We thus
   * have to modify the value indexes in the buckets.
   */
  template <class Serializer>
  void serialize_impl(Serializer& serializer) const {
    const slz_size_type version = SERIALIZATION_PROTOCOL_VERSION;
    serializer(version);

    const slz_size_type bucket_count = m_bucket_count;
    serializer(bucket_count);

    const slz_size_type nb_elements = m_nb_elements;
    serializer(nb_elements);

    const float max_load_factor = m_max_load_factor;
    serializer(max_load_factor);

    const array_bucket* buckets = buckets_data();
    for (size_type ibucket = 0; ibucket < m_bucket_count; ibucket++) {
      buckets[ibucket].serialize(serializer);
      serialize_bucket_values(serializer, buckets[ibucket]);
    }
  }

  template <
      class Serializer, class U = T,
      typename std::enable_if<!has_mapped_type<U>::value>::type* = nullptr>
  void serialize_bucket_values(Serializer& /*serializer*/,
                               const array_bucket& /*bucket*/) const {}

  template <class Serializer, class U = T,
            typename std::enable_if<has_mapped_type<U>::value>::type* = nullptr>
  void serialize_bucket_values(Serializer& serializer,
                               const array_bucket& bucket) const {
    for (auto it = bucket.begin(); it != bucket.end(); ++it) {
      serializer(this->m_values[it.value()]);
    }
  }

  template <class Deserializer>
  void deserialize_impl(Deserializer& deserializer, bool hash_compatible) {
    tsl_ah_assert(m_bucket_count == 0);  // Current hash table must be empty

    const slz_size_type version =
        deserialize_value<slz_size_type>(deserializer);
    // For now we only have one version of the serialization protocol.
    // If it doesn't match there is a problem with the file.
    if (version != SERIALIZATION_PROTOCOL_VERSION) {
      throw std::runtime_error(
          "Can't deserialize the array_map/set. The protocol version header is "
          "invalid.");
    }

    const slz_size_type bucket_count_ds =
        deserialize_value<slz_size_type>(deserializer);
    const slz_size_type nb_elements =
        deserialize_value<slz_size_type>(deserializer);
    const float max_load_factor = deserialize_value<float>(deserializer);

    m_nb_elements = numeric_cast<IndexSizeT>(
        nb_elements, "Deserialized nb_elements is too big.");

    size_type bucket_count = numeric_cast<size_type>(
        bucket_count_ds, "Deserialized bucket_count is too big.");
    GrowthPolicy new_growth_policy(bucket_count);

    if (hash_compatible && bucket_count != bucket_count_ds) {
      throw std::runtime_error(
          "The GrowthPolicy is not the same even though hash_compatible is "
          "true.");
    }

    // The buckets have to be in place before the growth policy changes.
    if (bucket_count > max_bucket_count()) {
      throw std::length_error("The map exceeds its maximum bucket count.");
    }
    m_buckets = allocate_bucket_array(bucket_count);
    m_bucket_count = bucket_count;
    GrowthPolicy::operator=(new_growth_policy);

    this->max_load_factor(max_load_factor);
    value_container_type::reserve(m_nb_elements);

    array_bucket* buckets = buckets_data();
    if (hash_compatible) {
      for (size_type i = 0; i < bucket_count; i++) {
        buckets[i] =
            array_bucket::deserialize(this->char_alloc(), deserializer);
        deserialize_bucket_values(deserializer, buckets[i]);
      }
    } else {
      for (size_type i = 0; i < bucket_count; i++) {
        // TODO use buffer to avoid reallocation on each deserialization.
        bucket_guard guard(this->char_alloc());
        guard.bucket =
            array_bucket::deserialize(this->char_alloc(), deserializer);
        deserialize_bucket_values(deserializer, guard.bucket);

        for (auto it_val = guard.bucket.cbegin(); it_val != guard.bucket.cend();
             ++it_val) {
          const std::size_t ibucket =
              bucket_for_hash(hash_key(it_val.key(), it_val.key_size()));

          auto it_find = buckets[ibucket].find_or_end_of_bucket(
              it_val.key(), it_val.key_size());
          if (it_find.second) {
            throw std::runtime_error(
                "Error on deserialization, the same key is presents multiple "
                "times.");
          }

          append_array_bucket_iterator_in_bucket(buckets[ibucket],
                                                 it_find.first, it_val);
        }
      }
    }

    if (load_factor() > this->max_load_factor()) {
      throw std::runtime_error(
          "Invalid max_load_factor. Check that the serializer and deserializer "
          "support "
          "floats correctly as they can be converted implicitely to ints.");
    }
  }

  template <
      class Deserializer, class U = T,
      typename std::enable_if<!has_mapped_type<U>::value>::type* = nullptr>
  void deserialize_bucket_values(Deserializer& /*deserializer*/,
                                 array_bucket& /*bucket*/) {}

  template <class Deserializer, class U = T,
            typename std::enable_if<has_mapped_type<U>::value>::type* = nullptr>
  void deserialize_bucket_values(Deserializer& deserializer,
                                 array_bucket& bucket) {
    for (auto it = bucket.begin(); it != bucket.end(); ++it) {
      this->m_values.emplace_back(deserialize_value<U>(deserializer));

      tsl_ah_assert(this->m_values.size() - 1 <=
                    std::numeric_limits<IndexSizeT>::max());
      it.set_value(static_cast<IndexSizeT>(this->m_values.size() - 1));
    }
  }

  template <class U = T, typename std::enable_if<
                             !has_mapped_type<U>::value>::type* = nullptr>
  void append_array_bucket_iterator_in_bucket(
      array_bucket& bucket, typename array_bucket::const_iterator end_of_bucket,
      typename array_bucket::const_iterator it_val) {
    bucket.append(this->char_alloc(), end_of_bucket, it_val.key(),
                  it_val.key_size());
  }

  template <class U = T,
            typename std::enable_if<has_mapped_type<U>::value>::type* = nullptr>
  void append_array_bucket_iterator_in_bucket(
      array_bucket& bucket, typename array_bucket::const_iterator end_of_bucket,
      typename array_bucket::const_iterator it_val) {
    bucket.append(this->char_alloc(), end_of_bucket, it_val.key(),
                  it_val.key_size(), it_val.value());
  }

 public:
  static const size_type DEFAULT_INIT_BUCKET_COUNT = 0;
  static constexpr float DEFAULT_MAX_LOAD_FACTOR = 2.0f;
  static const size_type MAX_KEY_SIZE = array_bucket::MAX_KEY_SIZE;
  static constexpr float MIN_MAX_LOAD_FACTOR = 0.1f;

 private:
  /**
   * Protocol version currenlty used for serialization.
   */
  static const slz_size_type SERIALIZATION_PROTOCOL_VERSION = 1;

  static constexpr float DEFAULT_CLEAR_OLD_ERASED_VALUE_THRESHOLD = 0.6f;
  static constexpr float REHASH_CLEAR_OLD_ERASED_VALUE_THRESHOLD = 0.9f;

  /**
   * Move all the elements of other (whose allocator is not equal to ours, so
   * we can't take its memory) in this empty hash, then empty other. The bucket
   * count is the one of other so there is no rehash.
   */
  void move_elements_from(array_hash& other) {
    tsl_ah_assert(m_bucket_count == 0);

    m_buckets = allocate_bucket_array(other.m_bucket_count);
    m_bucket_count = other.m_bucket_count;
    this->max_load_factor(other.m_max_load_factor);
    value_container_type::reserve(other.size());

    for (auto it = other.begin(); it != other.end(); ++it) {
      move_emplace(it);
    }

    other.clear();
  }

  template <class U = T,
            typename std::enable_if<has_mapped_type<U>::value>::type* = nullptr>
  void move_emplace(iterator& it) {
    emplace(it.key(), it.key_size(), std::move(it.value()));
  }

  template <class U = T, typename std::enable_if<
                             !has_mapped_type<U>::value>::type* = nullptr>
  void move_emplace(iterator& it) {
    emplace(it.key(), it.key_size());
  }

  /**
   * Return an always valid pointer to a static empty array_bucket (only used
   * to avoid a branch on the lookups when there is no bucket, never stored).
   */
  static array_bucket* static_empty_bucket_ptr() {
    static array_bucket empty_bucket;
    return &empty_bucket;
  }

  /**
   * Owns a bucket that must be released with the allocator on scope exit.
   */
  struct bucket_guard {
    explicit bucket_guard(char_allocator& alloc_) : bucket(), alloc(alloc_) {}
    ~bucket_guard() { bucket.clear(alloc); }

    bucket_guard(const bucket_guard&) = delete;
    bucket_guard& operator=(const bucket_guard&) = delete;

    array_bucket bucket;
    char_allocator& alloc;
  };

  array_bucket* buckets_data() noexcept { return to_raw_pointer(m_buckets); }

  const array_bucket* buckets_data() const noexcept {
    return to_raw_pointer(m_buckets);
  }

  array_bucket* buckets_end() noexcept {
    return buckets_data() + m_bucket_count;
  }

  const array_bucket* buckets_end() const noexcept {
    return buckets_data() + m_bucket_count;
  }

  /**
   * Buckets to use to search a key: the static empty bucket if there is no
   * bucket (the growth policy returns 0 in this case).
   */
  array_bucket* lookup_buckets() noexcept {
    if (TSL_AH_LIKELY(m_bucket_count != 0)) {
      return buckets_data();
    }

    return static_empty_bucket_ptr();
  }

  const array_bucket* lookup_buckets() const noexcept {
    if (TSL_AH_LIKELY(m_bucket_count != 0)) {
      return buckets_data();
    }

    return static_empty_bucket_ptr();
  }

  /**
   * Array of 'count' empty buckets (null if count is 0).
   */
  bucket_pointer allocate_bucket_array(size_type count) {
    if (count == 0) {
      return bucket_pointer();
    }

    bucket_allocator alloc(this->char_alloc());
    bucket_pointer buckets = bucket_alloc_traits::allocate(alloc, count);

    array_bucket* raw_buckets = to_raw_pointer(buckets);
    for (size_type ibucket = 0; ibucket < count; ibucket++) {
      ::new (static_cast<void*>(raw_buckets + ibucket)) array_bucket();
    }

    return buckets;
  }

  /**
   * Release the buffers of the buckets and the array.
   */
  void release_bucket_array(bucket_pointer buckets, size_type count) noexcept {
    if (count == 0) {
      return;
    }

    array_bucket* raw_buckets = to_raw_pointer(buckets);
    for (size_type ibucket = 0; ibucket < count; ibucket++) {
      raw_buckets[ibucket].clear(this->char_alloc());
      raw_buckets[ibucket].~array_bucket();
    }

    bucket_allocator alloc(this->char_alloc());
    bucket_alloc_traits::deallocate(alloc, buckets, count);
  }

  /**
   * Copy of the buckets of other, allocated with our allocator.
   */
  bucket_pointer copy_bucket_array(const array_hash& other) {
    bucket_pointer buckets = allocate_bucket_array(other.m_bucket_count);

    array_bucket* raw_buckets = to_raw_pointer(buckets);
    const array_bucket* other_buckets = other.buckets_data();
    try {
      for (size_type ibucket = 0; ibucket < other.m_bucket_count; ibucket++) {
        raw_buckets[ibucket] = other_buckets[ibucket].clone(this->char_alloc());
      }
    } catch (...) {
      release_bucket_array(buckets, other.m_bucket_count);
      throw;
    }

    return buckets;
  }

  char_allocator& char_alloc() noexcept {
    return allocator_storage_type::char_alloc();
  }

  const char_allocator& char_alloc() const noexcept {
    return allocator_storage_type::char_alloc();
  }

 private:
  /**
   * Array of m_bucket_count buckets, null if there is none. Fancy pointer, no
   * raw pointer to the buckets is stored.
   */
  bucket_pointer m_buckets;
  size_type m_bucket_count;

  IndexSizeT m_nb_elements;
  float m_max_load_factor;
  size_type m_load_threshold;
};

}  // end namespace detail_array_hash
}  // end namespace tsl

#endif
