import Foundation
import Testing

@testable import SourcetrailSwiftIndexerCore

// SW11 — generic-parameter tier + constraints. Every generic parameter is a
// NODE_TYPE_PARAMETER member of its owner (both engines); its bounds are edges
// from the parameter to the resolved constraint type (conformance/class →
// INHERITANCE, same-type → TYPE_USAGE), emitted by the semantic pass which can
// resolve the target through the index.

@Suite struct GenericsTests {
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

	// Edges of `type` leaving `sourceId`, paired with the target node's qualified
	// name so bounds can be asserted without knowing the target's module.
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

	private func hasOccurrence(forElement id: Int64, in storage: OwnedIntermediateStorage) -> Bool {
		storage.occurrences.contains { $0.elementId == id }
	}

	@Test func syntacticEmitsTypeParameterMembers() throws {
		let tmp = FileManager.default.temporaryDirectory
			.appendingPathComponent("sw11-syn-\(UUID().uuidString).swift")
		defer { try? FileManager.default.removeItem(at: tmp) }
		try """
			struct Box<Element> {
				var value: Element
			}
			""".write(to: tmp, atomically: true, encoding: .utf8)

		let builder = StorageBuilder()
		SyntacticIndexer.indexFile(path: tmp.path, moduleName: "M", builder: builder)
		let storage = builder.storage

		let box = try #require(node(named: "M::Box", in: storage))
		let element = try #require(node(named: "M::Box::Element", in: storage))
		#expect(element.type == NodeKind.typeParameter)
		// Member edge Box ⟶ Element, and the parameter has a clickable name token.
		#expect(edgeTargets(ofType: EdgeKind.member, from: box.id, in: storage).contains("M::Box::Element"))
		#expect(hasOccurrence(forElement: element.id, in: storage))
	}

	@Test func semanticEmitsParametersAndConstraints() throws {
		let root = FileManager.default.temporaryDirectory
			.appendingPathComponent("sw11-sem-\(UUID().uuidString)")
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
		// First: inline conformance bound; Second: where conformance bound.
		// onlyInts: inline bound + a same-type where clause against a concrete type.
		try """
			public struct Pair<First: Equatable, Second> where Second: Comparable {
				public let first: First
				public let second: Second
			}

			public func onlyInts<S: Sequence>(_ s: S) where S.Element == Int {
				_ = s
			}
			""".write(to: sources.appendingPathComponent("Generics.swift"), atomically: true, encoding: .utf8)

		let storage = PackageIndexer.index(workingDirectory: root.path) { _ in }
		#expect(storage.errors.isEmpty)

		// Parameters exist as type-parameter members of their owners.
		let pair = try #require(node(named: "Demo::Pair", in: storage))
		let first = try #require(node(named: "Demo::Pair::First", in: storage))
		let second = try #require(node(named: "Demo::Pair::Second", in: storage))
		#expect(first.type == NodeKind.typeParameter)
		#expect(second.type == NodeKind.typeParameter)
		let pairMembers = edgeTargets(ofType: EdgeKind.member, from: pair.id, in: storage)
		#expect(pairMembers.contains("Demo::Pair::First"))
		#expect(pairMembers.contains("Demo::Pair::Second"))

		// Conformance bounds → INHERITANCE from the parameter to the protocol.
		#expect(
			edgeTargets(ofType: EdgeKind.inheritance, from: first.id, in: storage)
				.contains { $0.hasSuffix("Equatable") })
		#expect(
			edgeTargets(ofType: EdgeKind.inheritance, from: second.id, in: storage)
				.contains { $0.hasSuffix("Comparable") })

		// Generic function parameter + its inline bound and same-type constraint.
		let s = try #require(node(named: "Demo::onlyInts(_:)::S", in: storage))
		#expect(s.type == NodeKind.typeParameter)
		#expect(
			edgeTargets(ofType: EdgeKind.inheritance, from: s.id, in: storage)
				.contains { $0.hasSuffix("Sequence") })
		// where S.Element == Int → TYPE_USAGE from S to Int.
		#expect(
			edgeTargets(ofType: EdgeKind.typeUsage, from: s.id, in: storage)
				.contains { $0.hasSuffix("Int") })
	}

	// SW11 (type arguments): `Base<Arg>` use sites become EDGE_TYPE_ARGUMENT,
	// gated by specialization scope. `local` keeps only applications whose base
	// type is defined in the package (Box), suppressing stdlib containers (Array).
	private func typeArgFixture() throws -> URL {
		let root = FileManager.default.temporaryDirectory
			.appendingPathComponent("sw11-arg-\(UUID().uuidString)")
		let sources = root.appendingPathComponent("Sources/Demo")
		try FileManager.default.createDirectory(at: sources, withIntermediateDirectories: true)
		try """
			// swift-tools-version: 6.0
			import PackageDescription
			let package = Package(
				name: "demo",
				targets: [.target(name: "Demo", path: "Sources/Demo")]
			)
			""".write(to: root.appendingPathComponent("Package.swift"), atomically: true, encoding: .utf8)
		try """
			public struct Box<Wrapped> {
				public let wrapped: Wrapped
			}

			public struct Holder {
				public let localBox: Box<Int>
				public let stdlib: Array<Int>
			}
			""".write(to: sources.appendingPathComponent("Args.swift"), atomically: true, encoding: .utf8)
		return root
	}

	@Test func typeArgumentsLocalScopeKeepsPackageBaseOnly() throws {
		let root = try typeArgFixture()
		defer { try? FileManager.default.removeItem(at: root) }

		let storage = PackageIndexer.index(
			workingDirectory: root.path, specializationScope: .local) { _ in }
		#expect(storage.errors.isEmpty)

		let holder = try #require(node(named: "Demo::Holder", in: storage))
		let localBox = try #require(node(named: "Demo::Holder::localBox", in: storage))
		let stdlib = try #require(node(named: "Demo::Holder::stdlib", in: storage))
		_ = holder

		// Box<Int>: base Box is local → Int becomes a TYPE_ARGUMENT of `localBox`.
		#expect(
			edgeTargets(ofType: EdgeKind.typeArgument, from: localBox.id, in: storage)
				.contains { $0.hasSuffix("Int") })
		// Array<Int>: base Array is stdlib → no type-argument edge under `local`;
		// Int stays a plain type usage.
		#expect(edgeTargets(ofType: EdgeKind.typeArgument, from: stdlib.id, in: storage).isEmpty)
	}

	@Test func typeArgumentsAllScopeIncludesStdlibBase() throws {
		let root = try typeArgFixture()
		defer { try? FileManager.default.removeItem(at: root) }

		let storage = PackageIndexer.index(
			workingDirectory: root.path, specializationScope: .all) { _ in }
		#expect(storage.errors.isEmpty)

		let stdlib = try #require(node(named: "Demo::Holder::stdlib", in: storage))
		// Under `all`, even Array<Int> emits the type-argument edge.
		#expect(
			edgeTargets(ofType: EdgeKind.typeArgument, from: stdlib.id, in: storage)
				.contains { $0.hasSuffix("Int") })
	}

	@Test func typeArgumentsOffScopeEmitsNone() throws {
		let root = try typeArgFixture()
		defer { try? FileManager.default.removeItem(at: root) }

		let storage = PackageIndexer.index(
			workingDirectory: root.path, specializationScope: .off) { _ in }
		#expect(storage.errors.isEmpty)

		#expect(storage.edges.allSatisfy { $0.type != EdgeKind.typeArgument })
	}
}
