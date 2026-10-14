#pragma once

#include <string_view>

namespace wlw::core {

// T's name as the compiler spells it, without "class " / "struct ": "wlw::app::System", "MyGame". Made at
// compile time from the function's signature (no RTTI, readable on every compiler, unlike typeid().name()).
// For names people read (profiler zones, logs); not stable across compilers for templates, so don't save it.
template <typename T>
constexpr std::string_view TypeName() {
#if defined(_MSC_VER) && !defined(__clang__)
  // "class std::basic_string_view<...> __cdecl wlw::core::TypeName<class MyGame>(void)"
  constexpr std::string_view signature = __FUNCSIG__;
  constexpr std::string_view open = "TypeName<";
  constexpr std::string_view close = ">(void)";
  std::string_view name = signature.substr(signature.find(open) + open.size());
  name = name.substr(0, name.rfind(close));
  for (std::string_view prefix : {"class ", "struct ", "enum ", "union "}) {
    if (name.substr(0, prefix.size()) == prefix) name.remove_prefix(prefix.size());
  }
  return name;
#else
  // clang: "std::string_view wlw::core::TypeName() [T = MyGame]"
  // gcc:   "constexpr std::string_view wlw::core::TypeName() [with T = MyGame; std::string_view = ...]"
  constexpr std::string_view signature = __PRETTY_FUNCTION__;
  constexpr std::string_view open = "T = ";
  const size_t start = signature.find(open) + open.size();
  const size_t end = signature.find_first_of(";]", start);
  return signature.substr(start, end - start);
#endif
}

}  // namespace wlw::core
