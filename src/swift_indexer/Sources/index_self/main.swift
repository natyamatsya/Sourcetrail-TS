// index_self — runs the hybrid indexer on a Swift package (default: this
// package) and prints a summary. The Swift analog of the Rust index_self bin.
// Usage: swift run index_self [package-path]

import Foundation
import SourcetrailSwiftIndexerCore

var arguments = Array(CommandLine.arguments.dropFirst())
// Optional: --grep <substr> prints every node whose decoded qualified name
// contains <substr> (e.g. a macro-synthesized member) and exits.
var grepNeedle: String?
if let i = arguments.firstIndex(of: "--grep"), i + 1 < arguments.count {
	grepNeedle = arguments[i + 1]
	arguments.removeSubrange(i...(i + 1))
}
let packagePath =
	arguments.first
	// Default to the swift_indexer package root (…/Sources/index_self → up 3).
	?? URL(fileURLWithPath: #filePath)
		.deletingLastPathComponent()
		.deletingLastPathComponent()
		.deletingLastPathComponent()
		.path

if grepNeedle == nil {
	print("Indexing: \(packagePath)")
}

let storage = PackageIndexer.index(workingDirectory: packagePath) { file in
	if grepNeedle == nil {
		print("  \(URL(fileURLWithPath: file).lastPathComponent)")
	}
}

func decodeQualifiedName(_ serializedName: String) -> String {
	guard serializedName.hasPrefix("::\tm") else { return serializedName }
	return serializedName.dropFirst(4)
		.components(separatedBy: "\tn")
		.map { $0.replacingOccurrences(of: "\ts\tp", with: "") }
		.joined(separator: "::")
}

if let needle = grepNeedle {
	let matches = storage.nodes
		.map { decodeQualifiedName($0.serializedName ?? "") }
		.filter { $0.contains(needle) }
		.sorted()
	print("nodes matching \"\(needle)\": \(matches.count)")
	for name in matches.prefix(40) {
		print("  \(name)")
	}
	exit(0)
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

// Module distribution + cross-module edges — a resolution-quality signal
// (cross-module references only appear if USRs resolve across the whole store).
func moduleOf(_ serializedName: String) -> String? {
	// "::\tm" + first part up to the first "\ts\tp".
	guard serializedName.hasPrefix("::\tm") else { return nil }
	let body = serializedName.dropFirst(4)
	guard let end = body.range(of: "\ts\tp") else { return nil }
	let first = String(body[body.startIndex..<end.lowerBound])
	return first.isEmpty ? nil : first
}

var moduleById: [Int64: String] = [:]
var moduleCounts: [String: Int] = [:]
for node in storage.nodes {
	guard let module = moduleOf(node.serializedName ?? "") else { continue }
	moduleById[node.id] = module
	moduleCounts[module, default: 0] += 1
}

var crossModuleEdges = 0
for edge in storage.edges {
	if let source = moduleById[edge.sourceNodeId],
		let target = moduleById[edge.targetNodeId],
		source != target
	{
		crossModuleEdges += 1
	}
}

print("\nModules (\(moduleCounts.count)), top 12 by node count:")
for (module, count) in moduleCounts.sorted(by: { $0.value > $1.value }).prefix(12) {
	print("  \(count)\t\(module)")
}
print("  cross-module edges: \(crossModuleEdges)")

if !storage.errors.isEmpty {
	print("\nErrors:")
	for error in storage.errors.prefix(20) {
		print("  [\(error.translationUnit)] \(error.message)")
	}
}
