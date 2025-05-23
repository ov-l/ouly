// SPDX-License-Identifier: MIT

#pragma once
#include <compare>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

namespace ouly
{

template <typename SizeType>
constexpr SizeType k_null_int_max = std::numeric_limits<SizeType>::max();

template <typename SizeType>
constexpr std::uint32_t k_null_int_min = std::numeric_limits<SizeType>::min();

/**
 * @brief A lightweight optional value_type that uses a sentinel value to represent emptiness
 *
 * @tparam Nullv The sentinel value that represents the empty/null state
 *
 * This class provides a simple optional value_type that uses a specific value to represent
 * the empty state, rather than maintaining a separate boolean flag like std::optional.
 * Useful for types that have a natural "null" value, like -1 for indices or nullptr for pointers.
 *
 * Examples:
 * ```
 * optional_val<-1> opt_index;      // Uses -1 as null value
 * optional_val<nullptr> opt_ptr;    // Uses nullptr as null value
 * ```
 *
 * Key features:
 * - More space efficient than std::optional (no extra bool flag)
 * - Implicit conversion to bool to check for value presence
 * - Access value through operator* or get()
 * - Comparison operators
 * - Reset and release operations
 *
 * @note The template parameter Nullv must be of a value_type that supports comparison
 * @note The stored value value_type is automatically deduced from Nullv's value_type
 */
template <auto Nullv>
struct optional_val
{
  using value_type = std::decay_t<decltype(Nullv)>;

  constexpr optional_val() = default;
  constexpr optional_val(value_type iv) noexcept : value_(iv) {}
  constexpr optional_val(std::nullopt_t /*value*/) noexcept {}

  constexpr operator bool() const noexcept
  {
    return value_ != Nullv;
  }

  constexpr auto operator*() const noexcept -> value_type
  {
    return value_;
  }

  [[nodiscard]] constexpr auto get() const noexcept -> value_type
  {
    return value_;
  }

  constexpr explicit operator value_type() const noexcept
  {
    return value_;
  }

  [[nodiscard]] constexpr auto has_value() const noexcept -> bool
  {
    return value_ != Nullv;
  }

  constexpr void reset() const noexcept
  {
    value_ = Nullv;
  }

  [[nodiscard]] constexpr auto release() const noexcept -> value_type
  {
    auto r = value_;
    value_ = Nullv;
    return r;
  }

  [[nodiscard]] constexpr auto value() const -> value_type
  {
    return value_;
  }

  [[nodiscard]] constexpr auto value_or(value_type default_value) const -> value_type
  {
    return value_ != Nullv ? value_ : default_value;
  }

  constexpr auto operator<=>(const optional_val& other) const noexcept = default;

  value_type value_ = Nullv;
};
} // namespace ouly