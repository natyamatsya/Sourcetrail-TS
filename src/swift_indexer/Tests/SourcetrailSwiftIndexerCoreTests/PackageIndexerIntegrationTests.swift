import Foundation
import Testing

@testable import SourcetrailSwiftIndexerCore

// End-to-end SW1: a real (tiny) SPM package with one broken file, run through
// describe + build + emission. Slow-ish (invokes swift build once); kept to a
// single test.

@Suite struct PackageIndexerIntegrationTests {
	@Test func brokenFixturePackageYieldsFilesAndNonFatalErrors() throws {
		let root = FileManager.default.temporaryDirectory
			.appendingPathComponent("sourcetrail-swift-fixture-\(UUID().uuidString)")
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
		try "public struct Good { public let x = 1 }\n"
			.write(to: sources.appendingPathComponent("Good.swift"), atomically: true, encoding: .utf8)
		try "let broken = thisDoesNotExist\n"
			.write(to: sources.appendingPathComponent("Broken.swift"), atomically: true, encoding: .utf8)

		var progressed: [String] = []
		let storage = PackageIndexer.index(workingDirectory: root.path) { file in
			progressed.append(file)
		}

		// The SPM model resolved both files into the Demo module and each got
		// a progress callback and a StorageFile row (incomplete until the
		// hybrid engine lands in SW2/SW3).
		#expect(storage.files.count == 2)
		#expect(progressed.count == 2)
		#expect(storage.files.allSatisfy { $0.complete == false && $0.indexed == true })
		#expect(storage.files.contains { ($0.filePath ?? "").hasSuffix("Sources/Demo/Good.swift") })

		// The broken file surfaced as a NON-fatal error naming it as the
		// translation unit — a broken build degrades the result, never the run.
		#expect(!storage.errors.isEmpty)
		let brokenError = try #require(
			storage.errors.first { ($0.translationUnit ?? "").hasSuffix("Broken.swift") }
		)
		#expect(brokenError.fatal == false)
		#expect((brokenError.message ?? "").contains("thisDoesNotExist"))
	}
}
