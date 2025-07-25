// SPDX-License-Identifier: MIT

#pragma once

#include "ouly/allocators/default_allocator.hpp"
#include "ouly/containers/basic_queue.hpp"
#include "ouly/scheduler/detail/cache_optimized_data.hpp"
#include "ouly/scheduler/detail/mpmc_ring.hpp"
#include "ouly/scheduler/spin_lock.hpp"
#include "ouly/scheduler/task.hpp"
#include "ouly/scheduler/worker_context.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <semaphore>
#include <thread>
#include <type_traits>
#include <utility>
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324) // structure was padded due to alignment specifier
#endif

namespace ouly::detail
{

static constexpr uint32_t max_worker_groups         = 32;
static constexpr uint32_t max_local_work_item       = 32; // 1/2 cache lines
static constexpr size_t   max_work_items_per_worker = 64; // Maximum items per worker to prevent excessive memory usage
static constexpr uint32_t max_steal_workers         = 64; // Maximum workers that can be tracked in steal mask
using work_item                                     = task_delegate;

struct aligned_atomic
{
  alignas(cache_line_size) std::atomic<uint32_t> value_{0};
  detail::cache_aligned_padding<std::atomic<uint32_t>> padding_;
};

struct work_queue_traits
{
  static constexpr uint32_t pool_size_v = 2048;
  using allocator_t                     = ouly::default_allocator<>;
};

using basic_work_queue = ouly::basic_queue<work_item, work_queue_traits>;
using async_work_queue = std::pair<ouly::spin_lock, basic_work_queue>;
using mpmc_work_ring   = mpmc_ring<work_item, max_work_items_per_worker>;

// Optimized workgroup structure with per-worker queues
struct workgroup
{
  // Per-worker queues within this workgroup - one queue per worker thread
  std::unique_ptr<mpmc_work_ring[]> per_worker_queues_;

  // Hot data cache line: Most frequently accessed during work stealing and submission
  uint32_t thread_count_     = 0; // Read frequently during work stealing
  uint32_t start_thread_idx_ = 0; // Read frequently during queue indexing
  uint32_t end_thread_idx_   = 0; // Read frequently during queue indexing

  // Cold data: Configuration set once during initialization
  uint32_t priority_ = 0;

  auto create_group(uint32_t start, uint32_t count, uint32_t priority) noexcept -> uint32_t
  {
    thread_count_     = count;
    start_thread_idx_ = start;
    end_thread_idx_   = start + count;
    this->priority_   = priority;

    // Allocate per-worker queues for this workgroup
    per_worker_queues_ = std::make_unique<mpmc_work_ring[]>(count);

    return start + count;
  }

  ~workgroup() noexcept = default;

  // Move semantics for workgroup management
  workgroup(workgroup&& other) noexcept
      : per_worker_queues_(std::move(other.per_worker_queues_)), thread_count_(other.thread_count_),
        start_thread_idx_(other.start_thread_idx_), end_thread_idx_(other.end_thread_idx_), priority_(other.priority_)
  {
    other.thread_count_     = 0;
    other.start_thread_idx_ = 0;
    other.end_thread_idx_   = 0;
    other.priority_         = 0;
  }

  auto operator=(workgroup&& other) noexcept -> workgroup&
  {
    if (this != &other)
    {
      // Move all data members
      per_worker_queues_ = std::move(other.per_worker_queues_);
      thread_count_      = other.thread_count_;
      start_thread_idx_  = other.start_thread_idx_;
      end_thread_idx_    = other.end_thread_idx_;
      priority_          = other.priority_;

      // Reset the source object
      other.thread_count_     = 0;
      other.start_thread_idx_ = 0;
      other.end_thread_idx_   = 0;
      other.priority_         = 0;
    }
    return *this;
  }

  workgroup() noexcept                           = default;
  workgroup(const workgroup&)                    = delete;
  auto operator=(const workgroup&) -> workgroup& = delete;

  // Push item to a specific worker's queue within this workgroup
  [[nodiscard]] auto push_item_to_worker(uint32_t worker_offset, work_item const& item) const -> bool
  {
    if (worker_offset >= thread_count_)
    {
      return false;
    }
    return per_worker_queues_[worker_offset].emplace(item);
  }

  // Pop item from a specific worker's queue within this workgroup
  [[nodiscard]] auto pop_item_from_worker(uint32_t worker_offset, work_item& item) const -> bool
  {
    if (worker_offset >= thread_count_)
    {
      return false;
    }
    return per_worker_queues_[worker_offset].pop(item);
  }
};

struct wake_event
{
  wake_event() noexcept : semaphore_(0) {}

  void wait() noexcept
  {
    semaphore_.acquire();
  }

  void notify() noexcept
  {
    semaphore_.release();
  }

  std::binary_semaphore semaphore_;
};

struct local_queue
{
  std::atomic_uint32_t                       head_;
  std::atomic_uint32_t                       tail_;
  std::array<work_item, max_local_work_item> queue_;
};

struct group_range
{
  std::array<uint8_t, max_worker_groups> priority_order_{};
  uint32_t                               count_ = 0;
  uint32_t                               mask_  = 0;

  // Store the range of threads this worker can steal from
  // This represents threads that belong to at least one shared workgroup
  uint32_t steal_range_start_ = 0;
  uint32_t steal_range_end_   = 0;

  // Bitset of threads this worker can steal from (for precise control)
  // Bit i is set if this worker can steal from worker i
  uint64_t steal_mask_ = 0;

  group_range() noexcept
  {
    priority_order_.fill(std::numeric_limits<uint8_t>::max());
  }
};

// Cache-aligned worker structure optimized for memory access patterns
struct worker
{
  // Context per work group - accessed during work execution
  // Pointer is stable, actual contexts allocated separately for better locality
  std::unique_ptr<worker_context[]> contexts_;

  worker_id id_                  = worker_id{0};
  worker_id min_steal_friend_id_ = worker_id{0};
  worker_id max_steal_friend_id_ = worker_id{0};

  int64_t tally_ = 0;

  // No local queues needed - work is organized per workgroup per worker
  // This eliminates the complexity of multiple queue types and work validation
};

} // namespace ouly::detail

#ifdef _MSC_VER
#pragma warning(pop)
#endif