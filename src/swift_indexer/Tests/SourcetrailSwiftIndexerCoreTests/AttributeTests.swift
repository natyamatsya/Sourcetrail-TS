import Foundation
import Testing

@testable import SourcetrailSwiftIndexerCore

// SW14 — attribute-driven relations. A custom attribute application (property
// wrapper, result builder, global actor) becomes an EDGE_ANNOTATION_USAGE from
// the annotated declaration to the attribute's type, distinct from an ordinary
// type usage in the same declaration.

@Suite struct AttributeTests {
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

	@Test func propertyWrapperAndResultBuilderBecomeAnnotationUsage() throws {
		let root = FileManager.default.temporaryDirectory
			.appendingPathComponent("sw14-\(UUID().uuidString)")
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
			@propertyWrapper
			public struct Clamped {
				public var wrappedValue: Int
				public init(wrappedValue: Int) { self.wrappedValue = wrappedValue }
			}

			@resultBuilder
			public enum StringMaker {
				public static func buildBlock(_ parts: String...) -> String { parts.joined() }
			}

			public struct Widget {
				@Clamped public var level: Int = 0
				@StringMaker public func makeText() -> String { "x" }
			}
			""".write(to: sources.appendingPathComponent("Attr.swift"), atomically: true, encoding: .utf8)

		let storage = PackageIndexer.index(workingDirectory: root.path) { _ in }
		#expect(storage.errors.isEmpty)

		let level = try #require(node(named: "Demo::Widget::level", in: storage))
		let makeText = try #require(node(named: "Demo::Widget::makeText()", in: storage))

		// The property wrapper and the result builder are annotation usages.
		#expect(edgeTargets(ofType: EdgeKind.annotationUsage, from: level.id, in: storage)
			.contains("Demo::Clamped"))
		#expect(edgeTargets(ofType: EdgeKind.annotationUsage, from: makeText.id, in: storage)
			.contains("Demo::StringMaker"))

		// The ordinary type in each declaration stays a plain type usage — the
		// override is scoped to the attribute position, not the whole declaration.
		#expect(edgeTargets(ofType: EdgeKind.typeUsage, from: level.id, in: storage)
			.contains { $0.hasSuffix("Int") })
		#expect(edgeTargets(ofType: EdgeKind.typeUsage, from: makeText.id, in: storage)
			.contains { $0.hasSuffix("String") })
		// …and the attribute is not double-counted as a type usage.
		#expect(!edgeTargets(ofType: EdgeKind.typeUsage, from: level.id, in: storage)
			.contains("Demo::Clamped"))
	}
}
