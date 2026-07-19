// `srctrl.qt` -- the primary interface for the Qt import-module wrapper. Partitions mirror Qt's own module
// names (`:core` = QtCore; `:meta` = the QMetaType system; future `:gui`, `:widgets` for lib_gui). A
// consumer writes a single `import srctrl.qt;`. Standalone module (Qt lives in its GMF); consumers that
// also `import std;` coexist cleanly -- that's the whole reason to wrap Qt.

export module srctrl.qt;

export import :core;
export import :meta;
