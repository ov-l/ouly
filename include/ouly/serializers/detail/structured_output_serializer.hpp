// SPDX-License-Identifier: MIT

//
// Created by obhi on 9/18/20.
//
#pragma once

#include "ouly/reflection/detail/container_utils.hpp"
#include "ouly/reflection/detail/derived_concepts.hpp"
#include "ouly/reflection/detail/visitor_helpers.hpp"
#include "ouly/reflection/reflection.hpp"
#include "ouly/reflection/type_name.hpp"
#include "ouly/reflection/visitor_impl.hpp"
#include "ouly/utility/detail/concepts.hpp"

#include "ouly/utility/user_config.hpp"

namespace ouly::detail
{

// Given an input serializer, load
// a bound class
template <typename Stream, typename Config = ouly::config<>>
class structured_output_serializer
{

private:
  enum class type : uint8_t
  {
    none,
    object,
    array,
    field
  };

  Stream* serializer_ = nullptr;
  type    type_       = type::none;
  bool    first_      = true;

public:
  using serializer_type              = Stream;
  using serializer_tag               = writer_tag;
  using transform_type               = transform_t<Config>;
  using config_type                  = Config;
  static constexpr bool mutate_enums = requires { typename Config::mutate_enums_type; };

  auto operator=(const structured_output_serializer&) -> structured_output_serializer&     = default;
  auto operator=(structured_output_serializer&&) noexcept -> structured_output_serializer& = default;
  structured_output_serializer(structured_output_serializer const&)                        = default;
  structured_output_serializer(structured_output_serializer&& i_other) noexcept : serializer_(i_other.serializer_) {}
  structured_output_serializer(Stream& ser) : serializer_(&ser) {}
  ~structured_output_serializer() noexcept
  {
    if (serializer_)
    {
      switch (type_)
      {
      case type::object:
        serializer_->end_object();
        break;
      case type::array:
        serializer_->end_array();
        break;
      case type::none:
      case type::field:
        break;
      }
    }
  }

  structured_output_serializer(ouly::detail::field_visitor_tag /*unused*/, structured_output_serializer& ser,
                               std::string_view key)
      : serializer_{ser.serializer_}, type_{type::field}
  {
    if (ser.first_)
    {
      ser.first_ = false;
    }
    else
    {
      serializer_->next_map_entry();
    }

    if (serializer_)
    {
      serializer_->key(key);
    }
  }

  structured_output_serializer(ouly::detail::field_visitor_tag /*unused*/, structured_output_serializer& ser,
                               [[maybe_unused]] size_t index)
      : serializer_{ser.serializer_}, type_{type::field}
  {
    if (ser.first_)
    {
      ser.first_ = false;
    }
    else
    {
      serializer_->next_array_entry();
    }
  }

  structured_output_serializer(ouly::detail::object_visitor_tag /*unused*/, structured_output_serializer& ser)
      : serializer_{ser.serializer_}, type_{type::object}
  {
    if (serializer_)
    {
      serializer_->begin_object();
    }
  }

  structured_output_serializer(ouly::detail::array_visitor_tag /*unused*/, structured_output_serializer& ser)
      : serializer_{ser.serializer_}, type_{type::array}
  {
    if (serializer_)
    {
      serializer_->begin_array();
    }
  }

  template <typename Class>
  auto can_visit([[maybe_unused]] Class const& obj) -> continue_token
  {
    return true;
  }

  template <ouly::detail::OutputSerializableClass<Stream> T>
  void visit(T& obj)
  {
    (*serializer_) << obj;
  }

  template <typename Class>
  void for_each_entry(Class const& obj, auto&& fn)
  {
    bool first = true;
    for (auto const& value : obj)
    {
      if (!first)
      {
        get().next_array_entry();
      }
      first = false;
      fn(value, *this);
    }
  }

  void visit(std::string_view str)
  {
    get().as_string(str);
  }

  template <ouly::detail::BoolLike Class>
  void visit(Class const& obj)
  {
    get().as_bool(obj);
  }

  template <typename Class>
    requires(ouly::detail::IntegerLike<Class> || ouly::detail::EnumLike<Class>)
  void visit(Class const& obj)
  {
    if constexpr (std::is_unsigned_v<Class>)
    {
      get().as_uint64(static_cast<uint64_t>(obj));
    }
    else
    {
      get().as_int64(static_cast<int64_t>(obj));
    }
  }

  template <ouly::detail::FloatLike Class>
  void visit(Class const& obj)
  {
    get().as_double(obj);
  }

  void set_null()
  {
    get().as_null();
  }

  void set_not_null() {}

private:
  auto get() -> Stream&
  {
    OULY_ASSERT(serializer_ != nullptr);
    return *serializer_;
  }
};

} // namespace ouly::detail
