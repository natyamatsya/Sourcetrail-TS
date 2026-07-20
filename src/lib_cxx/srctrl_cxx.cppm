// `srctrl.cxx` primary module interface -- the C++ language package's module (Phase 3 of
// context/DESIGN_INDEXER_MODULARIZATION.md). Partitions are added as more of lib_cxx is converted;
// the Clang-facing layers come last (they carry the Clang-header BMI cost).

export module srctrl.cxx;

export import :name;
export import :context;
export import :tooling;
export import :parser;
