#pragma once
// ---------------------------------------------------------------------------
// Build-time feature detection for the rule serialization layer.
//
// The RETE core never includes this header. Everything under rules/ and
// serialization/ is opt-in, so a target that only wants the engine keeps the
// exact footprint it has today.
//
//   RETE_ENABLE_JSON   JSON rule loading            (default: on)
//   RETE_ENABLE_YAML   YAML rule loading            (default: off; host only)
//   RETE_EMBEDDED      constrained-MCU profile      (default: off)
//
// RETE_EMBEDDED is a profile, not a platform test: it forces JSON-only, drops
// <iostream>, and turns off exceptions in this layer. Nothing here branches on
// a device name.
// ---------------------------------------------------------------------------

#if !defined(RETE_ENABLE_JSON) && !defined(RETE_DISABLE_JSON)
#  define RETE_ENABLE_JSON 1
#endif

#if defined(RETE_EMBEDDED) && RETE_EMBEDDED
#  if defined(RETE_ENABLE_YAML) && RETE_ENABLE_YAML
#    error "RETE_ENABLE_YAML is not supported together with RETE_EMBEDDED. \
Embedded targets use JSON; see docs/rules.md."
#  endif
#  undef  RETE_ENABLE_YAML
#  define RETE_ENABLE_YAML 0
#  ifndef RETE_NO_IOSTREAM
#    define RETE_NO_IOSTREAM 1
#  endif
#endif

#ifndef RETE_ENABLE_YAML
#  define RETE_ENABLE_YAML 0
#endif
#ifndef RETE_EMBEDDED
#  define RETE_EMBEDDED 0
#endif
#ifndef RETE_NO_IOSTREAM
#  define RETE_NO_IOSTREAM 0
#endif

// Exceptions: honour the compiler first, then let the profile override.
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#  ifndef RETE_HAS_EXCEPTIONS
#    define RETE_HAS_EXCEPTIONS 1
#  endif
#else
#  undef  RETE_HAS_EXCEPTIONS
#  define RETE_HAS_EXCEPTIONS 0
#endif

#if RETE_EMBEDDED
#  undef  RETE_HAS_EXCEPTIONS
#  define RETE_HAS_EXCEPTIONS 0
#endif

namespace rete {
struct BuildFeatures {
    static constexpr bool json       = RETE_ENABLE_JSON;
    static constexpr bool yaml       = RETE_ENABLE_YAML;
    static constexpr bool embedded   = RETE_EMBEDDED;
    static constexpr bool exceptions = RETE_HAS_EXCEPTIONS;
};
} // namespace rete
