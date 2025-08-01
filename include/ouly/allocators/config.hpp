// SPDX-License-Identifier: MIT

#pragma once

#include "ouly/utility/common.hpp"
#include "ouly/utility/utils.hpp"
#include <cstddef>

namespace ouly::cfg
{
inline static constexpr bool coalescing_allocator_large_size = false;
inline static constexpr bool prefetch_next_allocation        = false; // TODO make it a config per allocator

struct track_memory
{
  static constexpr bool track_memory_v = true;
};

template <typename T>
struct debug_tracer
{
  using debug_tracer_t = T;
};

template <std::size_t N>
struct min_alignment
{
  static constexpr std::size_t min_alignment_v = N;
};

template <typename T>
struct underlying_allocator
{
  using underlying_allocator_t = T;
};

template <typename T>
struct allocator_type
{
  using allocator_t = T;
};

template <std::size_t N>
struct atom_count
{
  static constexpr std::size_t atom_count_v = N;
};

template <std::size_t N>
struct atom_size
{
  static constexpr std::size_t atom_size_v = 1 << ouly::detail::log2(N);
};

template <std::size_t N>
struct atom_size_npt
{
  static constexpr std::size_t atom_size_v = N;
};

template <std::size_t Value>
struct granularity
{
  static constexpr std::size_t granularity_v = Value;
};

template <std::size_t Value>
struct max_bucket
{
  static constexpr std::size_t max_bucket_v = Value;
};

template <std::size_t Value>
struct search_window
{
  static constexpr std::size_t search_window_v = Value;
};

template <typename T>
struct fallback_start
{
  using fallback_strat_t = T;
};

template <std::size_t Value>
struct fixed_max_per_slot
{
  static constexpr std::size_t fixed_max_per_slot_v = Value;
};

template <typename T>
struct extension
{
  using extension_t = T;
};

template <typename T>
struct manager
{
  using manager_t = T;
};

template <typename T>
struct strategy
{
  using strategy_t = T;
};

/**
 * @brief Advise the kernel about memory usage patterns
 * @param ptr Pointer to memory region
 * @param size Size of the region
 * @param advice Memory usage advice
 * @return true on success, false on failure
 */
enum class advice : std::uint8_t
{
  normal,     // No special treatment
  random,     // Random access pattern
  sequential, // Sequential access pattern
  will_need,  // Will need this memory soon
  dont_need   // Don't need this memory soon
};

enum class protection : std::uint8_t
{
  none  = 0,
  read  = 1,
  write = 2,
  // execute            = 4,
  read_write = read | write,
  // read_execute       = read | execute,
  // write_execute      = write | execute,
  // read_write_execute = read | write | execute
};

enum class memory_stat_type : uint8_t
{
  e_none,
  e_compute,
  e_compute_atomic
};

template <typename T>
struct base_stats
{
  using base_stat_type = T;
};

struct compute_stats
{
  static constexpr ouly::cfg::memory_stat_type compute_stats_v = ouly::cfg::memory_stat_type::e_compute;
};

struct compute_atomic_stats
{
  static constexpr ouly::cfg::memory_stat_type compute_stats_v = ouly::cfg::memory_stat_type::e_compute_atomic;
};

struct bsearch_min0
{
  static constexpr int bsearch_algo = 0;
};
struct bsearch_min1
{
  static constexpr int bsearch_algo = 1;
};
struct bsearch_min2
{
  static constexpr int bsearch_algo = 2;
};

} // namespace ouly::cfg
