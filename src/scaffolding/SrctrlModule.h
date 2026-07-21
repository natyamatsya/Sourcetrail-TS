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
//   SRCTRL_MODULE_BUILD    #defined PER SOURCE FILE (by CMake, alongside CXX_SCAN_FOR_MODULES) for
//                          each consumer TU that has been converted to `import` the modules instead
//                          of #including their headers. Headers may guard modularized includes and
//                          forward declarations on it: importing TUs see the module view, while
//                          non-importing TUs in the same target -- crucially including moc-generated
//                          TUs, which can never import -- keep the classic textual view. Only the
//                          module-declaration lines (`module;`, `export module`) may never sit
//                          behind an #ifdef; an `import` may.

// `export extern "C++"`, not bare `export`: the linkage-specification attaches the entity to
// the GLOBAL module ([module.unit]/7) while still exporting its name to importers. This is the
// include-or-import coexistence pattern (clang docs / P3034 discussions): a textual parse of the
// same header in ANY other TU -- an unconverted classic TU, a moc-generated TU (which can never
// import), another wrapper's GMF, or a fwd decl in a Qt header -- declares the SAME global-module
// entity with the SAME ordinary mangling, so include and import merge instead of clashing
// ([basic.link]/10) and classic out-of-line definitions link for importers too. Bare `export`
// gave every entity strong module ownership, which made textual/imported mixes ill-formed (the
// srctrl.messaging park) and split classic vs importer callers onto different mangled symbols.
#undef SRCTRL_EXPORT
#ifdef SRCTRL_MODULE_PURVIEW
	#define SRCTRL_EXPORT export extern "C++"
#else
	#define SRCTRL_EXPORT
#endif
