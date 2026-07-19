// Dual-build scaffolding for the flag-gated C++20-modules migration.
// See context/DESIGN_INDEXER_MODULARIZATION.md.
//
// Deliberately NOT include-guarded: SRCTRL_EXPORT is re-evaluated on every inclusion so it reflects
// whether the header is currently being pulled into a module purview.
//
//   SRCTRL_EXPORT          `export` when the including translation unit is a module wrapper that has
//                          #defined SRCTRL_MODULE_PURVIEW right before including our headers; nothing
//                          otherwise. Gated on the *purview*, not on SRCTRL_MODULE_BUILD, because a
//                          library's own .cpp files include its headers as ordinary (non-module)
//                          translation units even in a module build -- and `export` there is
//                          ill-formed.
//
//   SRCTRL_MODULE_PURVIEW  #defined by a module wrapper (.cppm) immediately before it #includes the
//                          first-party headers, so those headers (a) get `export` and (b) SKIP the
//                          third-party / std #includes already hoisted into the wrapper's global
//                          module fragment.
//
//   SRCTRL_MODULE_BUILD    #defined (per target, by CMake) for a consumer that has been converted to
//                          `import` the module instead of #including its header. Only the module-
//                          declaration lines (`module;`, `export module`) may never sit behind an
//                          #ifdef; an `import` may.

#undef SRCTRL_EXPORT
#ifdef SRCTRL_MODULE_PURVIEW
	#define SRCTRL_EXPORT export
#else
	#define SRCTRL_EXPORT
#endif
