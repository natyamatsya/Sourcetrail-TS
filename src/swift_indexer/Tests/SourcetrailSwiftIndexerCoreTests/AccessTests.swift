import Foundation
import Testing

@testable import SourcetrailSwiftIndexerCore

// SW16 — declared access level as a StorageComponentAccess per node. Swift's six
// levels collapse onto Sourcetrail's four: open/public → PUBLIC, package/internal
// (and the implicit default) → DEFAULT, fileprivate/private → PRIVATE.

@Suite struct AccessTests {
	private func decodeParts(_ serializedName: String) -> [String] {
		guard serializedName.hasPrefix("::\tm") else { return [] }
		return serializedName.dropFirst(4)
			.components(separatedBy: "\tn")
			.map { $0.replacingOccurrences(of: "\ts\tp", with: "") }
	}

	// node qualified name → its recorded AccessKind.
	private func accesses(in storage: OwnedIntermediateStorage) -> [String: Int32] {
		let nameById = Dictionary(
			uniqueKeysWithValues: storage.nodes.map {
				($0.id, decodeParts($0.serializedName).joined(separator: "::"))
			})
		var out: [String: Int32] = [:]
		for access in storage.componentAccesses {
			if let name = nameById[access.nodeId] { out[name] = access.type }
		}
		return out
	}

	@Test func syntacticAccessLevels() throws {
		let tmp = FileManager.default.temporaryDirectory
			.appendingPathComponent("sw16-syn-\(UUID().uuidString).swift")
		defer { try? FileManager.default.removeItem(at: tmp) }
		try """
			public struct Api {
				public var pub: Int = 0
				private var priv: Int = 0
				var plain: Int = 0
			}
			open class Base {}
			package struct Pkg {}
			private enum Hidden { case a }
			""".write(to: tmp, atomically: true, encoding: .utf8)

		let builder = StorageBuilder()
		SyntacticIndexer.indexFile(path: tmp.path, moduleName: "M", builder: builder)
		let access = accesses(in: builder.storage)

		#expect(access["M::Api"] == AccessKind.public_)
		#expect(access["M::Api::pub"] == AccessKind.public_)
		#expect(access["M::Api::priv"] == AccessKind.private_)
		#expect(access["M::Api::plain"] == AccessKind.default_)  // implicit internal
		#expect(access["M::Base"] == AccessKind.public_)  // open → PUBLIC
		#expect(access["M::Pkg"] == AccessKind.package)  // package → PACKAGE
		#expect(access["M::Hidden"] == AccessKind.private_)
	}

	@Test func semanticAccessLevels() throws {
		let root = FileManager.default.temporaryDirectory
			.appendingPathComponent("sw16-sem-\(UUID().uuidString)")
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
			public struct Api {
				public func open() {}
				package func shared() {}
				private func shut() {}
			}
			internal func hidden() {}
			""".write(to: sources.appendingPathComponent("Api.swift"), atomically: true, encoding: .utf8)

		let storage = PackageIndexer.index(workingDirectory: root.path) { _ in }
		#expect(storage.errors.isEmpty)
		let access = accesses(in: storage)

		#expect(access["Demo::Api"] == AccessKind.public_)
		#expect(access["Demo::Api::open()"] == AccessKind.public_)
		#expect(access["Demo::Api::shared()"] == AccessKind.package)
		#expect(access["Demo::Api::shut()"] == AccessKind.private_)
		#expect(access["Demo::hidden()"] == AccessKind.default_)
	}
}
