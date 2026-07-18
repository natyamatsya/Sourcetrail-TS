import Foundation
import Testing

@testable import SourcetrailSwiftIndexerCore

// SW13 — actor identity. An `actor` is a class node (the index reports it as a
// class) carrying the NodeModifier.actor bit, supplied by SwiftSyntax. A plain
// class carries no modifiers.

@Suite struct ActorModifierTests {
	private func decodeParts(_ serializedName: String) -> [String] {
		guard serializedName.hasPrefix("::\tm") else { return [] }
		return serializedName.dropFirst(4)
			.components(separatedBy: "\tn")
			.map { $0.replacingOccurrences(of: "\ts\tp", with: "") }
	}

	private func node(
		named qualified: String, in storage: OwnedIntermediateStorage
	) -> OwnedStorageNode? {
		storage.nodes.first { decodeParts($0.serializedName).joined(separator: "::") == qualified }
	}

	@Test func syntacticMarksActors() throws {
		let tmp = FileManager.default.temporaryDirectory
			.appendingPathComponent("sw13-actor-syn-\(UUID().uuidString).swift")
		defer { try? FileManager.default.removeItem(at: tmp) }
		try """
			public actor Bank {
				public var balance: Int = 0
			}
			public class Plain {}
			""".write(to: tmp, atomically: true, encoding: .utf8)

		let builder = StorageBuilder()
		SyntacticIndexer.indexFile(path: tmp.path, moduleName: "M", builder: builder)
		let storage = builder.storage

		let bank = try #require(node(named: "M::Bank", in: storage))
		let plain = try #require(node(named: "M::Plain", in: storage))
		#expect(bank.type == NodeKind.class)  // actor is still a class node…
		#expect(bank.modifiers & NodeModifier.actor != 0)  // …with the actor bit
		#expect(plain.modifiers & NodeModifier.actor == 0)
	}

	@Test func semanticMarksActors() throws {
		let root = FileManager.default.temporaryDirectory
			.appendingPathComponent("sw13-actor-sem-\(UUID().uuidString)")
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
			public actor Bank {
				public func deposit() {}
			}
			public final class Plain {}
			""".write(to: sources.appendingPathComponent("A.swift"), atomically: true, encoding: .utf8)

		let storage = PackageIndexer.index(workingDirectory: root.path) { _ in }
		#expect(storage.errors.isEmpty)

		let bank = try #require(node(named: "Demo::Bank", in: storage))
		let plain = try #require(node(named: "Demo::Plain", in: storage))
		#expect(bank.type == NodeKind.class)
		#expect(bank.modifiers & NodeModifier.actor != 0)
		#expect(plain.modifiers & NodeModifier.actor == 0)
	}

	@Test func asyncAndNonisolatedModifiers() throws {
		let tmp = FileManager.default.temporaryDirectory
			.appendingPathComponent("sw13-async-\(UUID().uuidString).swift")
		defer { try? FileManager.default.removeItem(at: tmp) }
		try """
			public actor Service {
				public func fetch() async {}
				public nonisolated func id() -> Int { 0 }
				public nonisolated func reload() async {}
				public func plain() {}
			}
			""".write(to: tmp, atomically: true, encoding: .utf8)

		let builder = StorageBuilder()
		SyntacticIndexer.indexFile(path: tmp.path, moduleName: "M", builder: builder)
		let storage = builder.storage

		let fetch = try #require(node(named: "M::Service::fetch()", in: storage))
		let id = try #require(node(named: "M::Service::id()", in: storage))
		let reload = try #require(node(named: "M::Service::reload()", in: storage))
		let plain = try #require(node(named: "M::Service::plain()", in: storage))

		#expect(fetch.modifiers & NodeModifier.async != 0)
		#expect(fetch.modifiers & NodeModifier.nonisolated == 0)
		#expect(id.modifiers & NodeModifier.nonisolated != 0)
		#expect(id.modifiers & NodeModifier.async == 0)
		#expect(reload.modifiers & NodeModifier.async != 0)
		#expect(reload.modifiers & NodeModifier.nonisolated != 0)
		#expect(plain.modifiers == NodeModifier.none)
	}
}
