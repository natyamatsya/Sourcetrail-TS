// Owned (heap) mirror of the intermediate_storage.fbs tables the indexer
// emits. Grows with the engine: SW1 populates files + errors; SW2 adds nodes,
// edges, symbols, locations, occurrences.

package struct OwnedStorageFile {
	package let id: Int64
	package let filePath: String
	package let languageIdentifier: String
	package let indexed: Bool
	package let complete: Bool

	package init(
		id: Int64,
		filePath: String,
		languageIdentifier: String = "swift",
		indexed: Bool = true,
		complete: Bool
	) {
		self.id = id
		self.filePath = filePath
		self.languageIdentifier = languageIdentifier
		self.indexed = indexed
		self.complete = complete
	}
}

package struct OwnedStorageError {
	package let id: Int64
	package let message: String
	package let translationUnit: String
	package let fatal: Bool
	package let indexed: Bool

	package init(
		id: Int64,
		message: String,
		translationUnit: String,
		fatal: Bool,
		indexed: Bool = true
	) {
		self.id = id
		self.message = message
		self.translationUnit = translationUnit
		self.fatal = fatal
		self.indexed = indexed
	}
}

package struct OwnedStorageNode {
	package let id: Int64
	package let type: Int32
	package let serializedName: String
}

package struct OwnedStorageEdge {
	package let id: Int64
	package let type: Int32
	package let sourceNodeId: Int64
	package let targetNodeId: Int64
}

package struct OwnedStorageSymbol {
	package let id: Int64
	package let definitionKind: Int32
}

package struct OwnedStorageSourceLocation {
	package let id: Int64
	package let fileNodeId: Int64
	package let startLine: UInt32
	package let startCol: UInt32
	package let endLine: UInt32
	package let endCol: UInt32
	package let type: Int32
}

package struct OwnedStorageLocalSymbol {
	package let id: Int64
	package let name: String
}

package struct OwnedStorageOccurrence {
	package let elementId: Int64
	package let sourceLocationId: Int64
}

package struct OwnedIntermediateStorage {
	package var nextId: Int64 = 1
	package var nodes: [OwnedStorageNode] = []
	package var files: [OwnedStorageFile] = []
	package var edges: [OwnedStorageEdge] = []
	package var symbols: [OwnedStorageSymbol] = []
	package var sourceLocations: [OwnedStorageSourceLocation] = []
	package var localSymbols: [OwnedStorageLocalSymbol] = []
	package var occurrences: [OwnedStorageOccurrence] = []
	package var errors: [OwnedStorageError] = []

	package init() {}

	package mutating func allocateId() -> Int64 {
		defer { nextId += 1 }
		return nextId
	}
}
