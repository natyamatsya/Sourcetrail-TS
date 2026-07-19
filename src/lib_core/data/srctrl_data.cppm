// `srctrl.data` primary module interface -- re-exports its partitions so a consumer writes a single
// `import srctrl.data;`. The second first-party module (after srctrl.utility); partitions are added
// here as more of lib/data is converted.

export module srctrl.data;

export import :types;
export import :name;
export import :location;
export import :graph;
export import :search;
export import :tooltip;
