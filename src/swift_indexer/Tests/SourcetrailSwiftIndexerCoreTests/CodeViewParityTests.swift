import Foundation
import Testing

@testable import SourcetrailSwiftIndexerCore

// SW10: definitions carry a precise name TOKEN plus a SCOPE spanning the whole
// declaration, so the code view can show/navigate full bodies as it does for
// C++. Verified on both engines: the syntactic pass (deterministic, no build)
// and the semantic pass (real IndexStoreDB).

@Suite struct CodeViewParityTests {
	private func decodeParts(_ serializedName: String?) -> [String] {
		guard let serializedName, serializedName.hasPrefix("::\tm") else { return [] }
		return serializedName.dropFirst(4)
			.components(separatedBy: "\tn")
			.map { $0.replacingOccurrences(of: "\ts\tp", with: "") }
	}

	private func nodeId(named qualified: String, in storage: OwnedIntermediateStorage) -> Int64? {
		for node in storage.nodes {
			let parts = decodeParts(node.serializedName)
			if !parts.isEmpty, parts.joined(separator: "::") == qualified {
				return node.id
			}
		}
		return nil
	}

	private func locations(
		ofType type: Int32, forElement id: Int64, in storage: OwnedIntermediateStorage
	) -> [OwnedStorageSourceLocation] {
		let locIds = Set(storage.occurrences.filter { $0.elementId == id }.map(\.sourceLocationId))
		return storage.sourceLocations.filter { locIds.contains($0.id) && $0.type == type }
	}

	@Test func syntacticEmitsScopeAndPreciseNameExtents() throws {
		let tmp = FileManager.default.temporaryDirectory
			.appendingPathComponent("sw10-\(UUID().uuidString).swift")
		defer { try? FileManager.default.removeItem(at: tmp) }
		// Line 1: struct opens; line 2: one-line method; line 3: struct closes.
		try """
			public struct Box {
				public func unwrap() -> Int { 0 }
			}
			""".write(to: tmp, atomically: true, encoding: .utf8)

		let builder = StorageBuilder()
		SyntacticIndexer.indexFile(path: tmp.path, moduleName: "M", builder: builder)
		let storage = builder.storage

		// The struct's SCOPE spans its whole body (line 1 → line 3).
		let box = try #require(nodeId(named: "M::Box", in: storage))
		let boxScope = locations(ofType: LocationKind.scope, forElement: box, in: storage)
		#expect(boxScope.count == 1)
		#expect(boxScope[0].startLine == 1)
		#expect(boxScope[0].endLine == 3)

		// Its name TOKEN is exact: "Box" is three bytes.
		let boxToken = locations(ofType: LocationKind.token, forElement: box, in: storage)
		#expect(boxToken.count == 1)
		#expect(boxToken[0].startLine == 1)
		#expect(boxToken[0].endCol - boxToken[0].startCol + 1 == 3)

		// The one-line method's scope stays on its line, with a token too.
		let unwrap = try #require(nodeId(named: "M::Box::unwrap()", in: storage))
		let unwrapScope = locations(ofType: LocationKind.scope, forElement: unwrap, in: storage)
		#expect(unwrapScope.count == 1)
		#expect(unwrapScope[0].startLine == 2 && unwrapScope[0].endLine == 2)
		#expect(!locations(ofType: LocationKind.token, forElement: unwrap, in: storage).isEmpty)
	}

	@Test func semanticEmitsScopeLocations() throws {
		let root = FileManager.default.temporaryDirectory
			.appendingPathComponent("sw10-sem-\(UUID().uuidString)")
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
			public final class Widget {
				public func render() -> String {
					"x"
				}
			}
			""".write(to: sources.appendingPathComponent("Widget.swift"), atomically: true, encoding: .utf8)

		let storage = PackageIndexer.index(workingDirectory: root.path) { _ in }
		#expect(storage.errors.isEmpty)

		// The class got a scope that spans more than its name line.
		let widget = try #require(nodeId(named: "Demo::Widget", in: storage))
		let widgetScope = locations(ofType: LocationKind.scope, forElement: widget, in: storage)
		#expect(!widgetScope.isEmpty)
		#expect(widgetScope.contains { $0.endLine > $0.startLine })

		// And the multi-line method too.
		let render = try #require(nodeId(named: "Demo::Widget::render()", in: storage))
		let renderScope = locations(ofType: LocationKind.scope, forElement: render, in: storage)
		#expect(!renderScope.isEmpty)
		#expect(renderScope.contains { $0.endLine > $0.startLine })
	}
}
