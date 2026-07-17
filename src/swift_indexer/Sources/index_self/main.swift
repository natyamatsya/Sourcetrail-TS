// index_self — runs the hybrid indexer on a Swift package (default: this
// package) and prints a summary. The Swift analog of the Rust index_self bin.
// Usage: swift run index_self [package-path]

import Foundation
import SourcetrailSwiftIndexerCore

let arguments = CommandLine.arguments.dropFirst()
let packagePath =
	arguments.first
	// Default to the swift_indexer package root (…/Sources/index_self → up 3).
	?? URL(fileURLWithPath: #filePath)
		.deletingLastPathComponent()
		.deletingLastPathComponent()
		.deletingLastPathComponent()
		.path

print("Indexing: \(packagePath)")

let storage = PackageIndexer.index(workingDirectory: packagePath) { file in
	print("  \(URL(fileURLWithPath: file).lastPathComponent)")
}

print(
	"""

	Results:
	  files:       \(storage.files.count)
	  nodes:       \(storage.nodes.count)
	  symbols:     \(storage.symbols.count)
	  edges:       \(storage.edges.count)
	  locations:   \(storage.sourceLocations.count)
	  occurrences: \(storage.occurrences.count)
	  errors:      \(storage.errors.count)
	"""
)

let completeFiles = storage.files.filter { $0.complete }.count
print("  (\(completeFiles)/\(storage.files.count) files indexed semantically)")

if !storage.errors.isEmpty {
	print("\nErrors:")
	for error in storage.errors.prefix(20) {
		print("  [\(error.translationUnit)] \(error.message)")
	}
}
