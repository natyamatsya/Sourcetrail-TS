import Foundation
import Testing

@testable import SourcetrailSwiftIndexerCore

// SW2 end-to-end: build a real package with the index store on, then assert
// the semantic pass emits the expected nodes, edges, and occurrences.

@Suite struct SemanticIndexerIntegrationTests {
	private func decodeParts(_ serializedName: String?) -> [String] {
		// "::\tm" + parts joined "\tn", each part suffixed "\ts\tp"
		guard let serializedName, serializedName.hasPrefix("::\tm") else {
			return []
		}
		return serializedName.dropFirst(4)
			.components(separatedBy: "\tn")
			.map { $0.replacingOccurrences(of: "\ts\tp", with: "") }
	}

	@Test func healthyFixtureEmitsNodesEdgesAndOccurrences() throws {
		let root = FileManager.default.temporaryDirectory
			.appendingPathComponent("sourcetrail-swift-semantic-\(UUID().uuidString)")
		let sources = root.appendingPathComponent("Sources/Demo")
		try FileManager.default.createDirectory(at: sources, withIntermediateDirectories: true)
		defer { try? FileManager.default.removeItem(at: root) }

		try """
			// swift-tools-version: 6.0
			import PackageDescription
			let package = Package(
				name: "demo",
				targets: [.target(name: "Demo", path: "Sources/Demo")]
			)
			""".write(to: root.appendingPathComponent("Package.swift"), atomically: true, encoding: .utf8)
		try """
			public protocol Greeter {
				func greet() -> String
			}

			open class Base {
				public init() {}
			}

			public final class Child: Base, Greeter {
				public func greet() -> String {
					helper()
				}

				func helper() -> String {
					"hi"
				}
			}

			public func topLevel() -> String {
				Child().greet()
			}
			""".write(to: sources.appendingPathComponent("Demo.swift"), atomically: true, encoding: .utf8)

		let storage = PackageIndexer.index(workingDirectory: root.path) { _ in }

		#expect(storage.errors.isEmpty, "healthy build should carry no errors: \(storage.errors)")

		// The file is semantically covered → complete=true.
		#expect(storage.files.count == 1)
		#expect(storage.files[0].complete == true)

		let names = Dictionary(
			uniqueKeysWithValues: storage.nodes.map { (decodeParts($0.serializedName), $0) }
				.filter { !$0.0.isEmpty }
				.map { (parts, node) in (parts.joined(separator: "::"), node) }
		)

		let child = try #require(names["Demo::Child"])
		#expect(child.type == NodeKind.class)
		let base = try #require(names["Demo::Base"])
		#expect(base.type == NodeKind.class)
		let greeter = try #require(names["Demo::Greeter"])
		#expect(greeter.type == NodeKind.interface)
		let greet = try #require(names["Demo::Child::greet()"])
		#expect(greet.type == NodeKind.method)
		let topLevel = try #require(names["Demo::topLevel()"])
		#expect(topLevel.type == NodeKind.function)

		func hasEdge(_ type: Int32, _ source: String, _ target: String) -> Bool {
			guard let s = names[source]?.id, let t = names[target]?.id else {
				return false
			}
			return storage.edges.contains {
				$0.type == type && $0.sourceNodeId == s && $0.targetNodeId == t
			}
		}

		#expect(hasEdge(EdgeKind.inheritance, "Demo::Child", "Demo::Base"))
		#expect(hasEdge(EdgeKind.inheritance, "Demo::Child", "Demo::Greeter"))
		#expect(hasEdge(EdgeKind.call, "Demo::Child::greet()", "Demo::Child::helper()"))
		#expect(hasEdge(EdgeKind.call, "Demo::topLevel()", "Demo::Child::greet()"))
		#expect(hasEdge(EdgeKind.member, "Demo::Child", "Demo::Child::greet()"))
		#expect(hasEdge(EdgeKind.member, "Demo", "Demo::Child"))

		// Every definition has a symbol row and at least one occurrence with
		// a plausible location.
		#expect(storage.symbols.contains { $0.id == child.id })
		let childOccurrences = storage.occurrences.filter { $0.elementId == child.id }
		#expect(!childOccurrences.isEmpty)
		let locationIds = Set(childOccurrences.map(\.sourceLocationId))
		let locations = storage.sourceLocations.filter { locationIds.contains($0.id) }
		#expect(locations.allSatisfy { $0.startLine >= 1 && $0.startCol >= 1 })
	}
}
