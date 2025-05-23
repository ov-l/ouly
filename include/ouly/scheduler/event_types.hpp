// SPDX-License-Identifier: MIT

#pragma once

#include "ouly/scheduler/worker_context.hpp"
#include "ouly/utility/config.hpp"
#include <semaphore>

namespace ouly
{

class blocking_event
{

public:
  blocking_event(bool set) noexcept : semaphore_(static_cast<std::ptrdiff_t>(set)) {}
  blocking_event() noexcept : semaphore_(0) {}

  void wait()
  {
    semaphore_.acquire();
  }

  void notify()
  {
    semaphore_.release();
  }

private:
  std::binary_semaphore semaphore_;
};

class scheduler;
class busywork_event
{
public:
  busywork_event(bool set) noexcept : semaphore_(static_cast<std::ptrdiff_t>(set)) {}
  busywork_event() noexcept : semaphore_(0) {}

  OULY_API void wait(worker_id worker, scheduler& s);

  void notify()
  {
    semaphore_.release();
  }

private:
  std::binary_semaphore semaphore_;
};

} // namespace ouly
