import Foundation
import Testing

@testable import SourcetrailSwiftIndexerCore

@Suite struct DiagnosticParsingTests {
	@Test func parsesErrorAndWarningLines() {
		let output = """
			Building for debugging...
			/pkg/Sources/App/main.swift:12:7: error: cannot find 'foo' in scope
			/pkg/Sources/App/main.swift:20:1: warning: variable 'x' was never used
			                  let y = foo
			                          ^
			error: fatalError
			"""
		let diagnostics = BuildDriver.parseDiagnostics(output)
		#expect(diagnostics.count == 2)
		#expect(diagnostics[0].filePath == "/pkg/Sources/App/main.swift")
		#expect(diagnostics[0].line == 12)
		#expect(diagnostics[0].column == 7)
		#expect(diagnostics[0].severity == .error)
		#expect(diagnostics[0].message == "cannot find 'foo' in scope")
		#expect(diagnostics[1].severity == .warning)
	}

	@Test func deduplicatesRepeatedDiagnostics() {
		// swiftc repeats diagnostics across primary-file compilations.
		let line = "/pkg/A.swift:1:1: error: broken"
		let diagnostics = BuildDriver.parseDiagnostics(line + "\n" + line)
		#expect(diagnostics.count == 1)
	}

	@Test func ignoresNonDiagnosticColonLines() {
		let output = """
			warning: 'app': dependency requires a minimum platform
			remark: build planning
			relative/path.swift:1:2: error: not absolute so not a compiler diagnostic
			"""
		#expect(BuildDriver.parseDiagnostics(output).isEmpty)
	}

	@Test func messagesMayContainColons() {
		let diagnostics = BuildDriver.parseDiagnostics(
			"/pkg/A.swift:3:5: error: expected ':' after label"
		)
		#expect(diagnostics.count == 1)
		#expect(diagnostics[0].message == "expected ':' after label")
	}
}

@Suite struct PackageDescribeParsingTests {
	@Test func parsesTargetsIntoModulesWithAbsolutePaths() throws {
		let json = """
			{
			  "name": "demo",
			  "targets": [
			    {
			      "name": "DemoCore",
			      "path": "Sources/DemoCore",
			      "sources": ["A.swift", "Sub/B.swift", "note.md"]
			    },
			    {
			      "name": "DemoTests",
			      "path": "Tests/DemoTests",
			      "sources": ["T.swift"]
			    }
			  ]
			}
			"""
		let model = try #require(
			PackageModelLoader.parseDescribeJson(
				json,
				packageRoot: .init(fileURLWithPath: "/pkg")
			)
		)
		#expect(model.packageName == "demo")
		#expect(model.isFallback == false)
		#expect(model.modules.count == 2)
		#expect(model.modules[0].sourceFiles == [
			"/pkg/Sources/DemoCore/A.swift",
			"/pkg/Sources/DemoCore/Sub/B.swift",
		])
		#expect(model.moduleName(forSourceFile: "/pkg/Tests/DemoTests/T.swift") == "DemoTests")
		#expect(model.moduleName(forSourceFile: "/pkg/Unknown.swift") == nil)
	}

	@Test func malformedJsonYieldsNil() {
		#expect(
			PackageModelLoader.parseDescribeJson(
				"not json",
				packageRoot: .init(fileURLWithPath: "/pkg")
			) == nil
		)
	}
}

@Suite struct SwiftBuildOptionsTests {
	@Test func indexStoreOverrideSkipsTheBuild() {
		// A supplied index store means "don't build" — the result points at that
		// store and succeeds without running `swift build` (so this is instant,
		// and works against a path that isn't even a package).
		let options = SwiftBuildOptions(indexStorePath: "/prebuilt/index/store")
		let result = BuildDriver.build(
			packageRoot: URL(fileURLWithPath: "/nonexistent"), options: options)
		#expect(result.succeeded)
		#expect(result.diagnostics.isEmpty)
		#expect(result.indexStorePath.path == "/prebuilt/index/store")
	}

	@Test func toolchainSelectsItsOwnSwiftAndLibIndexStore() {
		let dflt = SwiftBuildOptions()
		#expect(dflt.swiftInvocation.executable == "/usr/bin/env")
		#expect(dflt.swiftInvocation.prefixArgs == ["swift"])

		let toolchain = SwiftBuildOptions(toolchainPath: "/opt/swift-6.1")
		#expect(toolchain.swiftInvocation.executable == "/opt/swift-6.1/usr/bin/swift")
		#expect(toolchain.swiftInvocation.prefixArgs.isEmpty)

		// libIndexStore comes from the same toolchain root.
		let lib = try? Toolchain.libIndexStorePath(toolchainPath: "/opt/swift-6.1")
		#expect(lib == "/opt/swift-6.1/usr/lib/libIndexStore.dylib")
	}
}
