//
// Copyright 2018 WebAssembly Community Group participants
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef WASP_BASE_FORMATTERS_H_
#define WASP_BASE_FORMATTERS_H_

#include <vector>

#include "wasp/base/at.h"
#include "wasp/base/format.h"
#include "wasp/base/span.h"
#include "wasp/base/string_view.h"
#include "wasp/base/v128.h"
#include "wasp/base/variant.h"
#include "wasp/base/wasm_types.h"

namespace fmt {

// Convert from a fmt::basic_memory_buffer to a fmt::string_view. Not sure why
// this conversion was omitted from the fmt library.
template <typename T, std::size_t SIZE, typename Allocator>
string_view to_string_view(const basic_memory_buffer<T, SIZE, Allocator>&);

template <typename T>
struct formatter<::wasp::At<T>> : formatter<T> {
  template <typename Ctx>
  typename Ctx::iterator format(const ::wasp::At<T>&, Ctx&);
};

template <>
struct formatter<::wasp::string_view> : formatter<string_view> {
  template <typename Ctx>
  typename Ctx::iterator format(const ::wasp::string_view&, Ctx&);
};

template <>
struct formatter<::wasp::SpanU8> : formatter<string_view> {
  template <typename Ctx>
  typename Ctx::iterator format(const ::wasp::SpanU8&, Ctx&);
};

template <typename T>
struct formatter<::wasp::span<const T>> : formatter<string_view> {
  template <typename Ctx>
  typename Ctx::iterator format(::wasp::span<const T>, Ctx&);
};

template <typename T>
struct formatter<std::vector<T>> : formatter<::wasp::span<const T>> {
  template <typename Ctx>
  typename Ctx::iterator format(const std::vector<T>&, Ctx&);
};

#define WASP_DEFINE_FORMATTER(Name)                           \
  template <>                                                 \
  struct formatter<::wasp::Name> : formatter<string_view> {   \
    template <typename Ctx>                                   \
    typename Ctx::iterator format(const ::wasp::Name&, Ctx&); \
  } /* No semicolon. */

WASP_DEFINE_FORMATTER(v128);
WASP_DEFINE_FORMATTER(Opcode);
WASP_DEFINE_FORMATTER(ValueType);
WASP_DEFINE_FORMATTER(ElementType);
WASP_DEFINE_FORMATTER(ExternalKind);
WASP_DEFINE_FORMATTER(EventAttribute);
WASP_DEFINE_FORMATTER(Mutability);
WASP_DEFINE_FORMATTER(SegmentType);
WASP_DEFINE_FORMATTER(Shared);
WASP_DEFINE_FORMATTER(Limits);

#undef WASP_DEFINE_FORMATTER

// Formatting variants.

template <typename... Ts>
struct formatter<::wasp::variant<Ts...>> : formatter<string_view> {
  template <typename Ctx>
  typename Ctx::iterator format(const ::wasp::variant<Ts...>&, Ctx&);
};


}  // namespace fmt

namespace wasp {

template <typename T>
struct VariantName { const char* GetName() const; };

#define WASP_DEFINE_VARIANT_NAME(Type, Name)     \
  template <>                                    \
  struct VariantName<Type> {                     \
    const char* GetName() const { return Name; } \
  };

WASP_DEFINE_VARIANT_NAME(u8, "u8")
WASP_DEFINE_VARIANT_NAME(u16, "u16")
WASP_DEFINE_VARIANT_NAME(u32, "u32")
WASP_DEFINE_VARIANT_NAME(u64, "u64")
WASP_DEFINE_VARIANT_NAME(s8, "s8")
WASP_DEFINE_VARIANT_NAME(s16, "s16")
WASP_DEFINE_VARIANT_NAME(s32, "s32")
WASP_DEFINE_VARIANT_NAME(s64, "s64")
WASP_DEFINE_VARIANT_NAME(f32, "f32")
WASP_DEFINE_VARIANT_NAME(f64, "f64")

}  // namespace wasp

#include "wasp/base/formatters-inl.h"

#endif // WASP_BASE_FORMATTERS_H_
