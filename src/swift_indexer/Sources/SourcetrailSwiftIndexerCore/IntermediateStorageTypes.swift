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

package struct OwnedIntermediateStorage {
	package var nextId: Int64 = 1
	package var files: [OwnedStorageFile] = []
	package var errors: [OwnedStorageError] = []

	package init() {}

	package mutating func allocateId() -> Int64 {
		defer { nextId += 1 }
		return nextId
	}
}
