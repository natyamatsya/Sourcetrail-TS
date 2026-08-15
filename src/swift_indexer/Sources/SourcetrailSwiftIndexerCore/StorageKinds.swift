// Wire constants mirroring src/lib/data/NodeKind.h, graph/Edge.h and the
// location/definition enums — the same values the Rust indexer emits
// (rust_indexer/indexer/src/parser/mod.rs).

enum NodeKind {
	static let symbol: Int32 = 1 << 0
	static let module: Int32 = 1 << 3
	static let `struct`: Int32 = 1 << 6
	static let `class`: Int32 = 1 << 7
	static let interface: Int32 = 1 << 8
	static let globalVariable: Int32 = 1 << 10
	static let field: Int32 = 1 << 11
	static let function: Int32 = 1 << 12
	static let method: Int32 = 1 << 13
	static let `enum`: Int32 = 1 << 14
	static let enumConstant: Int32 = 1 << 15
	static let typedef: Int32 = 1 << 16
	static let typeParameter: Int32 = 1 << 17
	static let file: Int32 = 1 << 18
	static let macro: Int32 = 1 << 19
	static let union: Int32 = 1 << 20
}

enum EdgeKind {
	static let member: Int32 = 1 << 0
	static let typeUsage: Int32 = 1 << 1
	static let usage: Int32 = 1 << 2
	static let call: Int32 = 1 << 3
	static let inheritance: Int32 = 1 << 4
	static let override_: Int32 = 1 << 5
	static let typeArgument: Int32 = 1 << 6
	static let import_: Int32 = 1 << 9
	static let macroUsage: Int32 = 1 << 11
	static let annotationUsage: Int32 = 1 << 12
	// A declaration binding to a contract atom; always declaration -> atom
	// (ADR-0009).
	static let binds: Int32 = 1 << 13
}

enum DefinitionKind {
	static let none: Int32 = 0
	static let implicit: Int32 = 1
	static let explicit: Int32 = 2
}

// Mirrors src/lib/data/parser/NodeModifier.h — orthogonal per-node flags stored
// as a bitmask on StorageNode. A Swift `actor` is a class node with `actor` set.
enum NodeModifier {
	static let none: Int32 = 0
	static let actor: Int32 = 1 << 0
	static let async: Int32 = 1 << 1
	static let nonisolated: Int32 = 1 << 2
	static let deprecated: Int32 = 1 << 3
}

// Mirrors src/lib/data/parser/AccessKind.h. Swift has no PROTECTED; `internal`
// (and the implicit default) maps to `default_`; `open`/`public` both map to
// `public_` (the subclassable distinction is lost). `package` has its own slot.
enum AccessKind {
	static let none: Int32 = 0
	static let public_: Int32 = 1
	static let protected: Int32 = 2
	static let private_: Int32 = 3
	static let default_: Int32 = 4
	static let package: Int32 = 7
}

// Mirrors src/lib/data/parser/NodeAttributeKind.h — the key of a sparse,
// display-only node_attribute(node_id, key, value) row. Append-only (persisted).
enum NodeAttributeKind {
	static let none: Int32 = 0
	static let availability: Int32 = 1  // @available / platform gating text
	static let deprecated: Int32 = 2  // deprecation message
	static let cfg: Int32 = 3  // configuration guard (Rust cfg, Swift #if)
	static let docBrief: Int32 = 4  // one-line documentation summary
}

enum LocationKind {
	static let token: Int32 = 0
	static let scope: Int32 = 1
	static let localSymbol: Int32 = 3
}

// (schema base, type name) when `path` is a generated mirror -- every FlatBuffers
// backend writes `<base>_generated.<ext>` -- else nil. Generator suffixes fold
// onto the table's own name, and flatc's Swift backend flattens the schema
// namespace into the type (`Sourcetrail_Ipc_StorageNode`), so the table's name is
// the last underscore segment. Shared by both indexing passes: generated files
// often have no up-to-date index unit and arrive through the syntactic fallback.
func schemaMirrorKey(path: String, typeName: String) -> (String, String)? {
	let file = path.split(separator: "/").last.map(String.init) ?? path
	guard let markerRange = file.range(of: "_generated."), !file.hasPrefix("_generated.")
	else {
		return nil
	}
	let schemaBase = String(file[file.startIndex..<markerRange.lowerBound])
	guard !schemaBase.isEmpty else { return nil }
	var name = typeName
	for suffix in ["Builder", "T"] where name.hasSuffix(suffix) && name.count > suffix.count {
		name = String(name.dropLast(suffix.count))
	}
	if name.contains("_"), let last = name.split(separator: "_").last {
		name = String(last)
	}
	return name.isEmpty ? nil : (schemaBase, name)
}

enum NameHierarchy {
	// `"::\tm" + parts joined by "\tn" with each part followed by "\ts\tp"`
	// — identical to the Rust side's serialize_name.
	static func serialize(parts: [String]) -> String {
		var out = "::\tm"
		for (index, part) in parts.enumerated() {
			if index > 0 {
				out += "\tn"
			}
			out += part + "\ts\tp"
		}
		return out
	}

	static func serializeFile(path: String) -> String {
		"/\tm" + path + "\ts\tp"
	}

	// A contract atom in a reserved namespace ("abi", "schema") -- the node two
	// languages meet at. Reserved means no ordinary declaration can be spelled
	// this way, so the storage's serialized-name merge joins the producers on
	// purpose. See context/DESIGN_XLANG_BOUNDARIES.md.
	static func serializeAtom(delimiter: String, parts: [String]) -> String {
		var out = delimiter + "\tm"
		for (index, part) in parts.enumerated() {
			if index > 0 {
				out += "\tn"
			}
			out += part + "\ts\tp"
		}
		return out
	}
}
