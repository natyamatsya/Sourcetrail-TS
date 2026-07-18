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
	static let annotationUsage: Int32 = 1 << 12
}

enum DefinitionKind {
	static let none: Int32 = 0
	static let implicit: Int32 = 1
	static let explicit: Int32 = 2
}

enum LocationKind {
	static let token: Int32 = 0
	static let scope: Int32 = 1
	static let localSymbol: Int32 = 3
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
}
