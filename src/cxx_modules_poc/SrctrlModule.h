#ifndef SRCTRL_MODULE_H
#define SRCTRL_MODULE_H

// Dual-build scaffolding for the flag-gated C++20-modules migration.
// See context/DESIGN_INDEXER_MODULARIZATION.md.
//
// Two macros drive the header/module duality:
//
//   SRCTRL_EXPORT          expands to `export` in a module build, nothing otherwise. Put it on the
//                          declarations a module should export.
//
//   SRCTRL_MODULE_PURVIEW  defined by a module *wrapper* (the .cppm) right before it #includes the
//                          first-party headers into the module purview. A header checks it to SKIP the
//                          third-party / std #includes it would otherwise make -- those are hoisted
//                          into the wrapper's global module fragment instead (a `#include` in the
//                          purview would wrongly attach the included entities to our module, and a
//                          re-include after the GMF is redundant).
//
// SRCTRL_MODULE_BUILD is defined by CMake (per target) when compiling in module mode.
//
// NB: `module;` / `export module` must appear *literally* in the .cppm wrapper -- never behind a
// macro or an `#ifdef` (the compiler rejects "'module;' can appear only at the start of the
// translation unit"). That is exactly why the dual build uses a header + a separate module wrapper,
// not one file toggled by `#ifdef`.

#ifdef SRCTRL_MODULE_BUILD
	#define SRCTRL_EXPORT export
#else
	#define SRCTRL_EXPORT
#endif

#endif	  // SRCTRL_MODULE_H
