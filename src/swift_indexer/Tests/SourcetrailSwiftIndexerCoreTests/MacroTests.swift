import Foundation
import Testing

@testable import SourcetrailSwiftIndexerCore

// SW15 — macros. A macro reference (an attached macro like `@Observable` or a
// freestanding one; both resolve to a `.macro` symbol) becomes an
// EDGE_MACRO_USAGE from the file node to the macro node, mirroring the C++/Rust
// convention. The macro node is NODE_MACRO, definition-less (it lives outside the
// indexed sources).

@Suite struct MacroTests {
	private func decodeParts(_ serializedName: String?) -> [String] {
		guard let serializedName, serializedName.hasPrefix("::\tm") else { return [] }
		return serializedName.dropFirst(4)
			.components(separatedBy: "\tn")
			.map { $0.replacingOccurrences(of: "\ts\tp", with: "") }
	}

	@Test func attachedMacroEmitsMacroUsageFromFile() throws {
		let root = FileManager.default.temporaryDirectory
			.appendingPathComponent("sw15-\(UUID().uuidString)")
		let sources = root.appendingPathComponent("Sources/Demo")
		try FileManager.default.createDirectory(at: sources, withIntermediateDirectories: true)
		defer { try? FileManager.default.removeItem(at: root) }

		try """
			// swift-tools-version: 6.0
			import PackageDescription
			let package = Package(
				name: "demo",
				platforms: [.macOS(.v14)],
				targets: [.target(name: "Demo", path: "Sources/Demo")]
			)
			""".write(to: root.appendingPathComponent("Package.swift"), atomically: true, encoding: .utf8)
		try """
			import Observation

			@Observable
			public final class Model {
				public var count: Int = 0
			}
			""".write(to: sources.appendingPathComponent("M.swift"), atomically: true, encoding: .utf8)

		let storage = PackageIndexer.index(workingDirectory: root.path) { _ in }

		let fileIds = Set(storage.nodes.filter { $0.type == NodeKind.file }.map(\.id))
		let macroNodes = storage.nodes.filter { $0.type == NodeKind.macro }
		let macroIds = Set(macroNodes.map(\.id))

		// The @Observable macro (and the members it expands) are NODE_MACRO nodes.
		#expect(macroNodes.contains {
			decodeParts($0.serializedName).last?.hasSuffix("Observable()") == true
		})

		// Its use is an EDGE_MACRO_USAGE from the file node to the macro node…
		#expect(storage.edges.contains {
			$0.type == EdgeKind.macroUsage && fileIds.contains($0.sourceNodeId)
				&& macroIds.contains($0.targetNodeId)
		})
		// …every macro-usage edge starts at a file and ends at a macro…
		#expect(storage.edges.filter { $0.type == EdgeKind.macroUsage }.allSatisfy {
			fileIds.contains($0.sourceNodeId) && macroIds.contains($0.targetNodeId)
		})
		// …and a macro is never left as a plain USAGE / TYPE_USAGE target.
		#expect(!storage.edges.contains {
			($0.type == EdgeKind.usage || $0.type == EdgeKind.typeUsage)
				&& macroIds.contains($0.targetNodeId)
		})
	}
}
