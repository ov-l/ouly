// SPDX-License-Identifier: MIT
#pragma once

#include "ouly/allocators/allocator.hpp"
#include "ouly/allocators/default_allocator.hpp"
#include "ouly/allocators/detail/custom_allocator.hpp"
#include "ouly/utility/utils.hpp"
#include <cstddef>
#include <optional>

namespace ouly
{
/**
 * @brief A queue container with block-based memory allocation
 *
 * @tparam Ty The type of elements stored in the queue
 * @tparam Config Configuration config for the queue, affecting allocation and size type
 *
 * basic_queue implements a queue data structure that allocates memory in fixed-size blocks.
 * It provides efficient push and pop operations with memory reuse through a free list.
 * The container supports both trivial and non-trivial types, handling construction and
 * destruction appropriately.
 *
 * Key features:
 * - Block-based memory allocation with configurable block size
 * - Efficient memory reuse through free list
 * - Support for move and copy operations
 * - Exception-safe operations
 * - Custom allocator support
 *
 * Memory layout:
 * - Elements are stored in blocks of fixed size (determined by Config)
 * - Each block maintains a pointer to the next block
 * - Freed blocks are kept in a free list for reuse
 *
 * Usage example:
 * ```
 * basic_queue<int> queue;
 * queue.emplace_back(1);
 * int value = queue.pop_front();
 * ```
 *
 * @note The queue size grows automatically as elements are added
 * @warning pop_front() throws an exception when the queue is empty
 */
template <typename Ty, typename Config = ouly::default_config<Ty>>
class basic_queue : public ouly::detail::custom_allocator_t<Config>
{
public:
  using value_type     = Ty;
  using size_type      = ouly::detail::choose_size_t<uint32_t, Config>;
  using allocator_type = ouly::detail::custom_allocator_t<Config>;

private:
  static constexpr auto pool_mul  = ouly::detail::log2(ouly::detail::pool_size_v<Config>);
  static constexpr auto pool_size = static_cast<size_type>(1) << pool_mul;
  static constexpr auto pool_mod  = pool_size - 1;
  static constexpr bool has_pod   = ouly::detail::HasTrivialAttrib<Config>;

  static constexpr size_t alignment = std::max(alignof(std::max_align_t), alignof(Ty));
  using storage                     = ouly::detail::aligned_storage<sizeof(value_type), alignment>;

  struct deque_block
  {
    alignas(alignment) std::array<storage, pool_size> data_;
    deque_block* next_ = nullptr;
  };

public:
  basic_queue() noexcept = default;

  basic_queue(allocator_type const& alloc) noexcept : allocator_type(alloc) {}

  basic_queue(basic_queue const& other, allocator_type const& alloc)
      : allocator_type(alloc), head_(other.head_), tail_(other.tail_), free_(other.free_), size_(other.size_),
        front_(other.front_), back_(other.back_)
  {
    copy(other);
  }

  basic_queue(basic_queue const& other)
    requires(std::is_copy_constructible_v<Ty>)
  {
    copy(other);
  }

  basic_queue(basic_queue&& other) noexcept
      : head_(other.head_), tail_(other.tail_), free_(other.free_), size_(other.size_), front_(other.front_),
        back_(other.back_)
  {
    other.head_  = nullptr;
    other.tail_  = nullptr;
    other.free_  = nullptr;
    other.size_  = 0;
    other.front_ = 0;
    other.back_  = 0;
  }

  auto operator=(basic_queue const& other) -> basic_queue& requires(std::is_copy_constructible_v<Ty>) {
    if (this == &other)
    {
      return *this;
    }
    copy(other);
    return *this;
  }

  auto operator=(basic_queue&& other) noexcept -> basic_queue&
  {
    if (this == &other)
    {
      return *this;
    }
    clear();
    free_chain(free_);

    head_        = other.head_;
    tail_        = other.tail_;
    free_        = other.free_;
    size_        = other.size_;
    front_       = other.front_;
    back_        = other.back_;
    other.head_  = nullptr;
    other.tail_  = nullptr;
    other.free_  = nullptr;
    other.size_  = 0;
    other.front_ = 0;
    other.back_  = 0;

    return *this;
  }

  ~basic_queue()
  {
    clear();
    free_chain(free_);
    free_ = nullptr;
    head_ = nullptr;
    tail_ = nullptr;
    size_ = 0;
  }

  void push_back(value_type const& item)
    requires(std::is_copy_constructible_v<Ty>)
  {
    emplace_back(item);
  }

  template <typename... Args>
  auto emplace_back(Args&&... args) -> auto&
  {
    if (back_ >= pool_size || !tail_)
    {
      add_tail();
      back_ = 0;
    }
    auto ptr = tail_->data_[back_++].template as<Ty>();
    std::construct_at(ptr, std::forward<Args>(args)...);
    size_++;
    return *ptr;
  }

  auto pop_front() -> Ty
  {
    if (empty())
    {
      throw std::runtime_error("Deque is empty");
    }

    return pop_front_unsafe();
  }

  auto pop_front_unsafe() noexcept -> Ty
  {
    OULY_ASSERT(!empty());

    Ty ret = std::move(*head_->data_[front_].template as<Ty>());

    if constexpr (!std::is_trivially_destructible_v<Ty>)
    {
      std::destroy_at(head_->data_[front_].template as<Ty>());
    }

    if (++front_ == pool_size)
    {
      remove_head();
      front_ = 0;
    }

    size_--;
    return ret;
  }

  auto pop_front(Ty& out) -> bool
  {
    if (empty())
    {
      return false;
    }

    out = pop_front_unsafe();
    return true;
  }

  [[nodiscard]] auto size() const noexcept -> size_type
  {
    return size_;
  }

  [[nodiscard]] auto empty() const noexcept -> bool
  {
    return size_ == 0;
  }

  void clear() noexcept
  {
    if constexpr (!std::is_trivially_destructible_v<Ty>)
    {
      for_each(
       [](Ty& v)
       {
         std::destroy_at(&v);
       });
    }

    if (tail_)
    {
      OULY_ASSERT(head_);
      OULY_ASSERT(head_ != free_);
      tail_->next_ = free_;
      free_        = head_;
    }
    head_ = nullptr;
    tail_ = nullptr;

    front_ = 0;
    back_  = 0;
    size_  = 0;
  }

  template <typename L>
  void for_each(L&& l)
  {
    internal_for_each<Ty>(*this, std::forward<L>(l));
  }

  template <typename L>
  void for_each(L&& l) const
  {
    internal_for_each<Ty const>(*this, std::forward<L>(l));
  }

private:
  template <typename V, typename T, typename L>
  static void internal_for_each(T& self, L lambda)
  {
    auto block = self.head_;
    auto start = self.front_;
    while (block)
    {
      auto end = block == self.tail_ ? self.back_ : pool_size;
      for (; start < end; ++start)
      {
        lambda(*block->data_[start].template as<V>());
      }
      start = 0;
      block = block->next_;
    }
  }

  void copy(basic_queue const& src)
    requires(std::is_copy_constructible_v<Ty>)
  {
    clear();
    src.for_each(
     [this](Ty const& v)
     {
       emplace_back(v);
     });
  }

  void add_tail()
  {
    deque_block* db = free_;
    if (free_)
    {
      free_ = free_->next_;
    }
    else
    {
      db        = ouly::allocate<deque_block>(static_cast<allocator_type&>(*this), sizeof(deque_block), alignarg<Ty>);
      db->next_ = nullptr;
    }

    if (tail_)
    {
      tail_->next_ = db;
    }
    else
    {
      head_ = db;
    }
    tail_ = db;
  }

  void remove_head()
  {
    auto h   = head_;
    head_    = head_->next_;
    h->next_ = free_;
    free_    = h;
    if (h == tail_)
    {
      head_  = nullptr;
      tail_  = nullptr;
      front_ = 0;
      back_  = 0;
    }
  }

  void free_chain(deque_block* start)
  {
    while (start)
    {
      auto next = start->next_;
      ouly::deallocate(static_cast<allocator_type&>(*this), start, sizeof(deque_block), alignarg<Ty>);
      start = next;
    }
  }

  deque_block* head_  = nullptr;
  deque_block* tail_  = nullptr;
  deque_block* free_  = nullptr;
  size_type    size_  = 0;
  size_type    front_ = 0;
  size_type    back_  = 0;
};
} // namespace ouly
