import Foundation
import Testing

@testable import SourcetrailSwiftIndexerCore

// Axis-3 metadata (DESIGN_NODE_MODIFIERS.md): a declaration's `@available`
// specification is emitted into the node_attribute table under
// NodeAttributeKind.availability — the deferred @available point from SW16, now
// riding the sparse metadata substrate. Purely syntactic, so both engines carry
// it and it survives broken builds.

@Suite struct AvailabilityTests {
	private func decodeParts(_ serializedName: String?) -> [String] {
		guard let serializedName, serializedName.hasPrefix("::\tm") else { return [] }
		return serializedName.dropFirst(4)
			.components(separatedBy: "\tn")
			.map { $0.replacingOccurrences(of: "\ts\tp", with: "") }
	}

	// node qualified name → its AVAILABILITY node_attribute value.
	private func availability(in storage: OwnedIntermediateStorage) -> [String: String] {
		let nameById = Dictionary(
			uniqueKeysWithValues: storage.nodes.map {
				($0.id, decodeParts($0.serializedName).joined(separator: "::"))
			})
		var out: [String: String] = [:]
		for attribute in storage.nodeAttributes
		where attribute.key == NodeAttributeKind.availability {
			if let name = nameById[attribute.nodeId] { out[name] = attribute.value }
		}
		return out
	}

	@Test func syntacticAvailability() throws {
		let tmp = FileManager.default.temporaryDirectory
			.appendingPathComponent("avail-syn-\(UUID().uuidString).swift")
		defer { try? FileManager.default.removeItem(at: tmp) }
		try """
			@available(macOS 14.0, iOS 17.0, *)
			public struct Modern {}

			public struct Plain {}

			@available(*, deprecated, message: "use Modern")
			public enum Old { case a }
			""".write(to: tmp, atomically: true, encoding: .utf8)

		let builder = StorageBuilder()
		SyntacticIndexer.indexFile(path: tmp.path, moduleName: "M", builder: builder)
		let avail = availability(in: builder.storage)

		#expect(avail["M::Modern"] == "macOS 14.0, iOS 17.0, *")
		#expect(avail["M::Plain"] == nil)  // no @available → no attribute row
		#expect(avail["M::Old"]?.contains("deprecated") == true)
	}

	@Test func semanticAvailability() throws {
		let root = FileManager.default.temporaryDirectory
			.appendingPathComponent("avail-sem-\(UUID().uuidString)")
		let sources = root.appendingPathComponent("Sources/Demo")
		try FileManager.default.createDirectory(at: sources, withIntermediateDirectories: true)
		defer { try? FileManager.default.removeItem(at: root) }

		try """
			// swift-tools-version: 6.0
			import PackageDescription
			let package = Package(
				name: "demo",
				platforms: [.macOS(.v13)],
				targets: [.target(name: "Demo", path: "Sources/Demo")]
			)
			""".write(to: root.appendingPathComponent("Package.swift"), atomically: true, encoding: .utf8)
		try """
			@available(macOS 14.0, *)
			public struct Modern {
				public func recent() {}
			}
			public struct Plain {}
			""".write(to: sources.appendingPathComponent("Api.swift"), atomically: true, encoding: .utf8)

		let storage = PackageIndexer.index(workingDirectory: root.path) { _ in }
		#expect(storage.errors.isEmpty)
		let avail = availability(in: storage)

		#expect(avail["Demo::Modern"] == "macOS 14.0, *")
		#expect(avail["Demo::Plain"] == nil)
	}
}
