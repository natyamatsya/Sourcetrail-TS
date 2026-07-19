// `srctrl.storage` primary module interface -- the persistence layer's first-party module. Currently
// just the :types partition (the storage record structs); the sqlite impl / access layer are later
// phases gated on the sqlpp23-module spike (see context/DESIGN_STORAGE_MODULARIZATION.md). It imports
// srctrl.data (classification enums + graph types) but is a separate module from it.

export module srctrl.storage;

export import :types;
export import :interface;
export import :error;
export import :access;
