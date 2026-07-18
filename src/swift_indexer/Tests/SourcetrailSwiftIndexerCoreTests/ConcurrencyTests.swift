import Foundation
import Testing

@testable import SourcetrailSwiftIndexerCore

// SW13 — concurrency model. The representable, schema-free relations: global-actor
// isolation is an annotation usage from the isolated declaration to the actor
// type (function- AND type-level), and `Sendable` is an ordinary protocol
// conformance. Actor identity and `async`/`nonisolated` have no storage slot and
// are deferred (see ROADMAP_SWIFT_INDEXER.md).

@Suite struct ConcurrencyTests {
	private func decodeParts(_ serializedName: String?) -> [String] {
		guard let serializedName, serializedName.hasPrefix("::\tm") else { return [] }
		return serializedName.dropFirst(4)
			.components(separatedBy: "\tn")
			.map { $0.replacingOccurrences(of: "\ts\tp", with: "") }
	}

	private func node(
		named qualified: String, in storage: OwnedIntermediateStorage
	) -> OwnedStorageNode? {
		storage.nodes.first { decodeParts($0.serializedName).joined(separator: "::") == qualified }
	}

	private func edgeTargets(
		ofType type: Int32, from sourceId: Int64, in storage: OwnedIntermediateStorage
	) -> [String] {
		storage.edges
			.filter { $0.type == type && $0.sourceNodeId == sourceId }
			.compactMap { edge in
				storage.nodes.first { $0.id == edge.targetNodeId }
					.map { decodeParts($0.serializedName).joined(separator: "::") }
			}
	}

	@Test func globalActorIsolationAndSendableConformance() throws {
		let root = FileManager.default.temporaryDirectory
			.appendingPathComponent("sw13-\(UUID().uuidString)")
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
			public struct Token: Sendable {
				public let value: Int
			}

			@MainActor
			public final class ViewModel {
				public func refresh() {}
			}

			@MainActor public func run() {}
			""".write(to: sources.appendingPathComponent("Conc.swift"), atomically: true, encoding: .utf8)

		let storage = PackageIndexer.index(workingDirectory: root.path) { _ in }
		#expect(storage.errors.isEmpty)

		// Global-actor isolation as annotation usage — both a type-level and a
		// function-level `@MainActor` (the type-level one needs SwiftSyntax to
		// supply the source, which the store's relation omits).
		let viewModel = try #require(node(named: "Demo::ViewModel", in: storage))
		let run = try #require(node(named: "Demo::run()", in: storage))
		#expect(edgeTargets(ofType: EdgeKind.annotationUsage, from: viewModel.id, in: storage)
			.contains { $0.hasSuffix("MainActor") })
		#expect(edgeTargets(ofType: EdgeKind.annotationUsage, from: run.id, in: storage)
			.contains { $0.hasSuffix("MainActor") })

		// Sendable is a plain protocol conformance.
		let token = try #require(node(named: "Demo::Token", in: storage))
		#expect(edgeTargets(ofType: EdgeKind.inheritance, from: token.id, in: storage)
			.contains { $0.hasSuffix("Sendable") })
	}
}
