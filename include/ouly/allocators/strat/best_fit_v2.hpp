// SPDX-License-Identifier: MIT
#pragma once

#include "ouly/allocators/config.hpp"
#include "ouly/allocators/detail/arena.hpp"
#include "ouly/allocators/detail/strat_concepts.hpp"
#include "ouly/utility/type_traits.hpp"
#include <optional>

namespace ouly::strat
{
#define OULY_BINARY_SEARCH_STEP                                                                                        \
  {                                                                                                                    \
    const size_type* const middle = it + (size >> 1);                                                                  \
    size                          = (size + 1) >> 1;                                                                   \
    it                            = *middle < key ? middle : it;                                                       \
  }

/**
 * @brief  Strategy class for arena_allocator that stores a
 *         sorted list of free available slots.
 *         Binary search is used to find the best slot that fits
 *         the requested memory
 */
template <typename Config = ouly::config<>>
class best_fit_v2
{

public:
  using extension       = uint64_t;
  using size_type       = ouly::detail::choose_size_t<uint32_t, Config>;
  using arena_bank      = ouly::detail::arena_bank<size_type, extension>;
  using block_bank      = ouly::detail::block_bank<size_type, extension>;
  using block           = ouly::detail::block<size_type, extension>;
  using bank_data       = ouly::detail::bank_data<size_type, extension>;
  using block_link      = typename block_bank::link;
  using size_list       = ouly::vector<size_type>;
  using optional_addr   = size_type*;
  using allocate_result = optional_addr;

  static constexpr int bsearch_algo =
   std::conditional_t<ouly::detail::HasBsearchAlgo<Config>, Config, ouly::cfg::bsearch_min0>::bsearch_algo;

  static constexpr size_type min_granularity = 4;

  best_fit_v2() noexcept              = default;
  best_fit_v2(best_fit_v2 const&)     = default;
  best_fit_v2(best_fit_v2&&) noexcept = default;
  ~best_fit_v2() noexcept             = default;

  auto operator=(best_fit_v2 const&) -> best_fit_v2&     = default;
  auto operator=(best_fit_v2&&) noexcept -> best_fit_v2& = default;

  [[nodiscard]] auto try_allocate([[maybe_unused]] bank_data& bank, size_type size) noexcept -> optional_addr
  {
    if (sizes_.empty() || sizes_.back() < size)
    {
      return nullptr;
    }
    return find_free(size);
  }

  auto commit(bank_data& bank, size_type size, auto found) noexcept -> std::uint32_t
  {
    auto          free_idx  = std::distance(sizes_.data(), found);
    std::uint32_t free_node = free_ordering_[free_idx];
    auto&         blk       = bank.blocks_[block_link(free_node)];
    // Marker
    blk.is_free_ = false;

    auto remaining = *found - size;
    blk.size_      = size;
    if (remaining > 0)
    {
      auto& list  = bank.arenas_[blk.arena_].block_order();
      auto  arena = blk.arena_;
      auto  newblk =
       bank.blocks_.emplace(blk.offset_ + size, remaining, arena, std::numeric_limits<uint32_t>::max(), true);
      list.insert_after(bank.blocks_, free_node, (uint32_t)newblk);
      // reinsert the left-over size in free list
      reinsert_left(bank.blocks_, free_idx, remaining, (uint32_t)newblk);
    }
    else
    {
      // delete the existing found index from free list
      sizes_.erase(sizes_.begin() + free_idx);
      free_ordering_.erase(free_ordering_.begin() + free_idx);
    }

    return free_node;
  }

  void add_free_arena([[maybe_unused]] block_bank& blocks, std::uint32_t block) noexcept
  {
    sizes_.push_back(blocks[block_link(block)].size_);
    free_ordering_.push_back(block);
  }

  void add_free(block_bank& blocks, std::uint32_t block) noexcept
  {
    add_free_after_begin(blocks, block);
  }

  void grow_free_node(block_bank& blocks, std::uint32_t block, size_type newsize) noexcept
  {
    auto& blk = blocks[block_link(block)];

    auto it = find_free_it(sizes_.data(), sizes_.size(), blk.size());
    for (auto end = static_cast<uint32_t>(free_ordering_.size()); it != end && free_ordering_[it] != block; ++it)
    {
      ;
    }

    OULY_ASSERT(it != static_cast<uint32_t>(free_ordering_.size()));
    blk.size_ = newsize;
    reinsert_right(blocks, it, newsize, block);
  }

  void replace_and_grow(block_bank& blocks, std::uint32_t block, std::uint32_t new_block, size_type new_size) noexcept
  {
    size_type size                      = blocks[block_link(block)].size_;
    blocks[block_link(new_block)].size_ = new_size;

    auto it = find_free_it(sizes_.data(), sizes_.size(), size);
    for (auto end = static_cast<uint32_t>(free_ordering_.size()); it != end && free_ordering_[it] != block; ++it)
    {
      ;
    }

    OULY_ASSERT(it != static_cast<uint32_t>(free_ordering_.size()));
    reinsert_right(blocks, it, new_size, new_block);
  }

  void erase(block_bank& blocks, std::uint32_t block) noexcept
  {
    auto it = find_free_it(sizes_.data(), sizes_.size(), blocks[block_link(block)].size_);
    for (auto end = static_cast<uint32_t>(free_ordering_.size()); it != end && free_ordering_[it] != block; ++it)
    {
      ;
    }
    OULY_ASSERT(it != static_cast<uint32_t>(free_ordering_.size()));
    free_ordering_.erase(it + free_ordering_.begin());
    sizes_.erase(it + sizes_.begin());
  }

  auto total_free_nodes(block_bank const& /*blocks*/) const noexcept -> std::uint32_t
  {
    return static_cast<std::uint32_t>(free_ordering_.size());
  }

  auto total_free_size(block_bank const& /*blocks*/) const noexcept -> size_type
  {
    size_type sz = 0;
    for (auto fn : sizes_)
    {
      sz += fn;
    }

    return sz;
  }

  void validate_integrity(block_bank const& blocks) const noexcept
  {
    size_type sz = 0;
    OULY_ASSERT(free_ordering_.size() == sizes_.size());
    for (size_t i = 1; i < sizes_.size(); ++i)
    {
      OULY_ASSERT(sizes_[i - 1] <= sizes_[i]);
    }
    for (size_t i = 0; i < free_ordering_.size(); ++i)
    {
      auto fn = free_ordering_[i];
      OULY_ASSERT(sz <= blocks[block_link(fn)].size_);
      OULY_ASSERT(blocks[block_link(fn)].size_ == sizes_[i]);
      sz = blocks[block_link(fn)].size_;
    }
  }

  template <typename Owner>
  void init([[maybe_unused]] Owner const& owner)
  {}

protected:
  // Private
  void add_free_after_begin(block_bank& blocks, std::uint32_t block) noexcept
  {
    auto blkid             = block_link(block);
    blocks[blkid].is_free_ = true;
    auto size              = blocks[blkid].size_;
    auto it                = find_free_it(sizes_.data(), sizes_.size(), size);
    free_ordering_.emplace(free_ordering_.begin() + it, block);
    sizes_.emplace(sizes_.begin() + it, size);
  }

  static auto mini0(size_type const* it, size_t size, size_type key) noexcept
  {
    while (size > 2)
    {
      {
        const size_type* const middle = it + (size >> 1);
        size                          = (size + 1) >> 1;
        it                            = *middle < key ? middle : it;
      };
    }
    it += size > 1 && (*it < key);
    it += size > 0 && (*it < key);
    return it;
  }

  static auto mini1(size_type const* it, size_t size, size_type key) noexcept
  {
    if (size == 0U)
    {
      return it;
    }
    while (true)
    {
      {
        const size_type* const middle = it + (size >> 1);
        size                          = (size + 1) >> 1;
        it                            = *middle < key ? middle : it;
      };
      if (size <= 2)
      {
        break;
      }
    }

    it += size > 1 && (*it < key);
    it += size > 0 && (*it < key);
    return it;
  }

  static auto mini2(size_type const* it, size_t size, size_type key) noexcept
  {
    if (size == 0U)
    {
      return it;
    }
    while (true)
    {
      OULY_BINARY_SEARCH_STEP;
      OULY_BINARY_SEARCH_STEP;
      if (size <= 2)
      {
        break;
      }
    }
    it += size > 1 && (*it < key);
    it += size > 0 && (*it < key);
    return it;
  }

  static auto bsearch(size_type const* it, size_t s, size_type key) noexcept
  {
    if constexpr (bsearch_algo == 0)
    {
      return mini0(it, s, key);
    }
    else if constexpr (bsearch_algo == 1)
    {
      return mini1(it, s, key);
    }
    else if constexpr (bsearch_algo == 2)
    {
      return mini2(it, s, key);
    }
  }

  static auto find_free_it(size_type const* it, size_t s, size_type key) noexcept
  {
    return std::distance(it, bsearch(it, s, key));
  }

  auto find_free(size_type size) const noexcept -> optional_addr
  {
    auto it = bsearch(sizes_.data(), sizes_.size(), size);
    return (it < (sizes_.data() + sizes_.size())) ? optional_addr(it) : optional_addr(nullptr);
  }

  void reinsert_left([[maybe_unused]] block_bank& blocks, size_t of, size_type size, std::uint32_t node) noexcept
  {
    if (of == 0U)
    {
      free_ordering_[of] = node;
      sizes_[of]         = size;
    }
    else
    {
      auto it = find_free_it(sizes_.data(), of, size);
      if (static_cast<unsigned>(it) != of)
      {
        std::size_t count = of - static_cast<unsigned>(it);
        {
          auto src  = sizes_.data() + it;
          auto dest = src + 1;
          std::memmove(dest, src, count * sizeof(size_type));
        }
        {
          auto src  = free_ordering_.data() + it;
          auto dest = src + 1;
          std::memmove(dest, src, count * sizeof(std::uint32_t));
        }

        free_ordering_[it] = node;
        sizes_[it]         = size;
      }
      else
      {
        free_ordering_[of] = node;
        sizes_[of]         = size;
      }
    }
  }

  void reinsert_right([[maybe_unused]] block_bank& blocks, size_t of, size_type size, std::uint32_t node)
  {
    auto next = of + 1;
    if (next == sizes_.size())
    {
      free_ordering_[of] = node;
      sizes_[of]         = size;
    }
    else
    {
      auto it = find_free_it(sizes_.data() + next, sizes_.size() - next, size);
      if (it)
      {
        std::size_t count = it;
        {
          auto dest = sizes_.data() + of;
          auto src  = dest + 1;
          std::memmove(dest, src, count * sizeof(size_type));
          auto ptr = (dest + count);
          *ptr     = size;
        }

        {
          auto* dest = free_ordering_.data() + of;
          auto* src  = dest + 1;
          std::memmove(dest, src, count * sizeof(uint32_t));
          auto* ptr = (dest + count);
          *ptr      = node;
        }
      }
      else
      {
        free_ordering_[of] = node;
        sizes_[of]         = size;
      }
    }
  }

private:
  size_list               sizes_;
  ouly::detail::free_list free_ordering_;
};

/**
 * alloc_strategy::best_fit Impl
 */
#undef OULY_BINARY_SEARCH_STEP
} // namespace ouly::strat
