import Foundation
import Testing

@testable import SourcetrailSwiftIndexerCore

// SW3: the hybrid merge. One package, built healthy once, then one file
// breaks — the unchanged file keeps exact semantic data (stale-build
// survivor), the broken file degrades to syntactic declarations with
// complete=false, and fixing it upgrades it back to semantic.

@Suite struct HybridIndexingTests {
	private func decodeParts(_ serializedName: String) -> [String] {
		guard serializedName.hasPrefix("::\tm") else {
			return []
		}
		return serializedName.dropFirst(4)
			.components(separatedBy: "\tn")
			.map { $0.replacingOccurrences(of: "\ts\tp", with: "") }
	}

	private func qualifiedNames(_ storage: OwnedIntermediateStorage) -> Set<String> {
		Set(
			storage.nodes.map { decodeParts($0.serializedName) }
				.filter { !$0.isEmpty }
				.map { $0.joined(separator: "::") }
		)
	}

	@Test func brokenFileFallsBackSyntacticallyAndUpgradesWhenFixed() throws {
		let root = FileManager.default.temporaryDirectory
			.appendingPathComponent("sourcetrail-swift-hybrid-\(UUID().uuidString)")
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
		let stableFile = sources.appendingPathComponent("Stable.swift")
		let volatileFile = sources.appendingPathComponent("Volatile.swift")
		try "public struct Stable { public func keep() {} }\n"
			.write(to: stableFile, atomically: true, encoding: .utf8)
		let healthyVolatile = "public enum Volatile { case ok }\n"
		try healthyVolatile.write(to: volatileFile, atomically: true, encoding: .utf8)

		// 1. Healthy build: everything semantic.
		var storage = PackageIndexer.index(workingDirectory: root.path) { _ in }
		var allComplete = storage.files.allSatisfy { $0.complete }
		#expect(allComplete)
		#expect(qualifiedNames(storage).contains("Demo::Stable::keep()"))
		#expect(qualifiedNames(storage).contains("Demo::Volatile"))

		// 2. Break Volatile.swift with a PARSE error (mtime must pass the
		// unit's timestamp). A parse error — unlike a type error — stops
		// swiftc before it emits a fresh index unit, so the file's unit stays
		// stale and the hybrid engine falls back to the syntactic walk.
		// SwiftParser still error-recovers the valid declarations above it.
		Thread.sleep(forTimeInterval: 1.1)
		try """
			public struct Replacement {
				public func added(label x: Int) {}
			}
			func stillBroken( {
			""".write(to: volatileFile, atomically: true, encoding: .utf8)

		storage = PackageIndexer.index(workingDirectory: root.path) { _ in }
		let filesByPath = Dictionary(
			uniqueKeysWithValues: storage.files.map { ($0.filePath, $0) })

		// Stable survives semantically off the previous build's unit.
		#expect(filesByPath[stableFile.path]?.complete == true)
		#expect(qualifiedNames(storage).contains("Demo::Stable::keep()"))

		// Volatile degrades to syntactic declarations, incomplete, with a
		// fallback error row naming it.
		#expect(filesByPath[volatileFile.path]?.complete == false)
		#expect(qualifiedNames(storage).contains("Demo::Replacement"))
		#expect(qualifiedNames(storage).contains("Demo::Replacement::added(label:)"))
		#expect(
			storage.errors.contains {
				$0.translationUnit == volatileFile.path && !$0.fatal
			}
		)

		// 3. Fix the file: it upgrades back to semantic.
		Thread.sleep(forTimeInterval: 1.1)
		try "public struct Replacement { public func added(label x: Int) {} }\n"
			.write(to: volatileFile, atomically: true, encoding: .utf8)
		storage = PackageIndexer.index(workingDirectory: root.path) { _ in }
		allComplete = storage.files.allSatisfy { $0.complete }
		#expect(allComplete)
		#expect(storage.errors.isEmpty)
		#expect(qualifiedNames(storage).contains("Demo::Replacement::added(label:)"))
	}

	// The two engines must spell names identically or nodes fork — the
	// serialized-name dedup in PersistentStorage is the merge point.
	@Test func syntacticNamesMatchSemanticSpelling() throws {
		let root = FileManager.default.temporaryDirectory
			.appendingPathComponent("sourcetrail-swift-names-\(UUID().uuidString)")
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
		let file = sources.appendingPathComponent("Api.swift")
		try """
			public struct Box {
				public let stored: Int
				public init(stored: Int) { self.stored = stored }
				public func combine(with other: Box, scale: Int) -> Box { other }
				public subscript(_ index: Int) -> Int { index }
			}
			public enum Direction { case north, south }
			public func free(_ x: Int, named: String) {}
			extension Box {
				public func extra() {}
			}
			""".write(to: file, atomically: true, encoding: .utf8)

		// Semantic names from a healthy build…
		let semantic = qualifiedNames(PackageIndexer.index(workingDirectory: root.path) { _ in })

		// …must contain every syntactic name for the same file.
		let builder = StorageBuilder()
		SyntacticIndexer.indexFile(path: file.path, moduleName: "Demo", builder: builder)
		let syntactic = qualifiedNames(builder.storage)

		let missing = syntactic.subtracting(semantic)
		#expect(
			missing.isEmpty,
			"syntactic-only spellings (would fork nodes): \(missing.sorted())"
		)
	}
}
