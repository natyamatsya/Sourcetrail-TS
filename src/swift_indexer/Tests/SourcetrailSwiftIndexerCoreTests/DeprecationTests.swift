import Foundation
import Testing

@testable import SourcetrailSwiftIndexerCore

// Deprecation is the cross-axis fact (DESIGN_NODE_MODIFIERS.md): the boolean drives
// the NODE_MODIFIER_DEPRECATED bit, the message rides node_attribute under the
// DEPRECATED key. Swift expresses it as `@available(*, deprecated[, message:])`.

@Suite struct DeprecationTests {
	private func decodeParts(_ serializedName: String?) -> [String] {
		guard let serializedName, serializedName.hasPrefix("::\tm") else { return [] }
		return serializedName.dropFirst(4)
			.components(separatedBy: "\tn")
			.map { $0.replacingOccurrences(of: "\ts\tp", with: "") }
	}

	private func names(_ storage: OwnedIntermediateStorage) -> [Int64: String] {
		Dictionary(
			uniqueKeysWithValues: storage.nodes.map {
				($0.id, decodeParts($0.serializedName).joined(separator: "::"))
			})
	}

	// node qualified name → modifiers bitmask.
	private func modifiers(in storage: OwnedIntermediateStorage) -> [String: Int32] {
		let nameById = names(storage)
		var out: [String: Int32] = [:]
		for node in storage.nodes {
			if let name = nameById[node.id] { out[name] = node.modifiers }
		}
		return out
	}

	// node qualified name → its DEPRECATED node_attribute message.
	private func deprecationMessage(in storage: OwnedIntermediateStorage) -> [String: String] {
		let nameById = names(storage)
		var out: [String: String] = [:]
		for attribute in storage.nodeAttributes where attribute.key == NodeAttributeKind.deprecated {
			if let name = nameById[attribute.nodeId] { out[name] = attribute.value }
		}
		return out
	}

	@Test func syntacticDeprecation() throws {
		let tmp = FileManager.default.temporaryDirectory
			.appendingPathComponent("depr-syn-\(UUID().uuidString).swift")
		defer { try? FileManager.default.removeItem(at: tmp) }
		try """
			@available(*, deprecated, message: "use Modern")
			public struct Old {}

			@available(*, deprecated)
			public struct Bare {}

			@available(macOS 14.0, *)
			public struct Modern {}

			public struct Plain {}
			""".write(to: tmp, atomically: true, encoding: .utf8)

		let builder = StorageBuilder()
		SyntacticIndexer.indexFile(path: tmp.path, moduleName: "M", builder: builder)
		let mods = modifiers(in: builder.storage)
		let message = deprecationMessage(in: builder.storage)

		// Old: deprecated bit + message.
		#expect(mods["M::Old"]! & NodeModifier.deprecated != 0)
		#expect(message["M::Old"] == "use Modern")
		// Bare: deprecated bit, but no message row (bit conveys the boolean).
		#expect(mods["M::Bare"]! & NodeModifier.deprecated != 0)
		#expect(message["M::Bare"] == nil)
		// Modern: available-gated but NOT deprecated.
		#expect(mods["M::Modern"]! & NodeModifier.deprecated == 0)
		// Plain: nothing.
		#expect(mods["M::Plain"] == NodeModifier.none)
		#expect(message["M::Plain"] == nil)
	}

	@Test func semanticDeprecation() throws {
		let root = FileManager.default.temporaryDirectory
			.appendingPathComponent("depr-sem-\(UUID().uuidString)")
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
			@available(*, deprecated, message: "use next()")
			public func old() {}
			public func fresh() {}
			""".write(to: sources.appendingPathComponent("Api.swift"), atomically: true, encoding: .utf8)

		let storage = PackageIndexer.index(workingDirectory: root.path) { _ in }
		#expect(storage.errors.isEmpty)
		let mods = modifiers(in: storage)
		let message = deprecationMessage(in: storage)

		#expect(mods["Demo::old()"]! & NodeModifier.deprecated != 0)
		#expect(message["Demo::old()"] == "use next()")
		#expect((mods["Demo::fresh()"] ?? 0) & NodeModifier.deprecated == 0)
	}
}
