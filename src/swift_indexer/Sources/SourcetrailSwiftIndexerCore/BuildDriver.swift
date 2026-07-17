import Foundation

// Runs `swift build` for the commanded package with the index store enabled
// (SW2 reads it via IndexStoreDB) and turns compiler diagnostics into
// non-fatal storage errors — the same posture the Rust indexer takes for
// panics: a broken build must degrade the result, never kill the run.

package struct BuildDiagnostic {
	package enum Severity: String {
		case error
		case warning
	}

	package let filePath: String
	package let line: Int
	package let column: Int
	package let severity: Severity
	package let message: String
}

package struct BuildResult {
	package let succeeded: Bool
	package let diagnostics: [BuildDiagnostic]
	package let indexStorePath: URL
	package let rawOutputTail: String
}

// Swift project-model options (SW5) that shape the build. Carried per command
// from the source-group settings; empty everywhere reproduces the zero-config
// default (a plain `swift build` on the host toolchain).
package struct SwiftBuildOptions {
	/// Extra args appended to `swift build` (e.g. `--configuration release`).
	package let buildArgs: [String]
	/// Toolchain root to build with; empty = the default `swift` on PATH.
	/// libIndexStore is taken from this toolchain too (see Toolchain).
	package let toolchainPath: String
	/// An existing index store to read; when set the build is SKIPPED entirely
	/// (index a read-only or already-built checkout).
	package let indexStorePath: String

	package init(buildArgs: [String] = [], toolchainPath: String = "", indexStorePath: String = "") {
		self.buildArgs = buildArgs
		self.toolchainPath = toolchainPath
		self.indexStorePath = indexStorePath
	}

	/// How to invoke `swift`: the default `/usr/bin/env swift …`, or a specific
	/// toolchain's binary directly when a toolchain path is configured.
	var swiftInvocation: (executable: String, prefixArgs: [String]) {
		toolchainPath.isEmpty
			? ("/usr/bin/env", ["swift"])
			: ((toolchainPath as NSString).appendingPathComponent("usr/bin/swift"), [])
	}
}

package enum BuildDriver {
	package static func build(packageRoot: URL, options: SwiftBuildOptions) -> BuildResult {
		let buildPath = packageRoot.appendingPathComponent(".build")

		// Index-store override (SW5): a store was supplied (e.g. Xcode's, or a
		// prebuilt read-only checkout), so skip `swift build` and read it as-is.
		if !options.indexStorePath.isEmpty {
			return BuildResult(
				succeeded: true,
				diagnostics: [],
				indexStorePath: URL(fileURLWithPath: options.indexStorePath),
				rawOutputTail: "swift_index_store_path set; `swift build` skipped"
			)
		}

		let (executable, prefix) = options.swiftInvocation
		var arguments =
			prefix + [
				"build",
				"--package-path", packageRoot.path,
				"--enable-index-store",
				// Build test targets too, so their sources get index units
				// and index semantically instead of falling back to the
				// syntactic pass. In real library packages the tests are a
				// large share of the code (e.g. ~45% of swift-syntax), and
				// users navigate test code as much as source.
				"--build-tests",
			]
		arguments += options.buildArgs

		let output: ProcessOutput
		do {
			output = try ProcessRunner.run(
				executable: executable, arguments: arguments, currentDirectory: packageRoot)
		} catch {
			return BuildResult(
				succeeded: false,
				diagnostics: [],
				indexStorePath: Self.indexStorePath(buildPath: buildPath),
				rawOutputTail: "swift build failed to launch: \(error)"
			)
		}

		// Diagnostics land on stderr; SwiftPM's progress lines on stdout.
		let diagnostics = parseDiagnostics(output.stderr + "\n" + output.stdout)
		return BuildResult(
			succeeded: output.exitCode == 0,
			diagnostics: diagnostics,
			indexStorePath: Self.indexStorePath(buildPath: buildPath),
			rawOutputTail: String((output.stderr + output.stdout).suffix(2000))
		)
	}

	// The index store's location varies by build system: the native SwiftPM
	// layout puts it at .build/<triple>/<config>/index/store, the SwiftBuild
	// (Xcode) layout at .build/out/Products/<Config>/index/store. Rather than
	// hardcode either, walk .build for an `index/store` directory that holds a
	// versioned data store (`v5/units`), picking the most recently written.
	package static func indexStorePath(buildPath: URL) -> URL {
		let fileManager = FileManager.default
		let fallback = buildPath.appendingPathComponent("index/store")
		guard
			let enumerator = fileManager.enumerator(
				at: buildPath,
				includingPropertiesForKeys: [.contentModificationDateKey],
				options: [.skipsHiddenFiles]
			)
		else {
			return fallback
		}

		var best: (url: URL, date: Date)?
		for case let url as URL in enumerator {
			// A store directory contains `<version>/units`; match on that so a
			// stray empty `index/store` dir never wins.
			guard url.lastPathComponent == "units",
				url.deletingLastPathComponent().lastPathComponent.hasPrefix("v")
			else {
				continue
			}
			let storeDir = url.deletingLastPathComponent().deletingLastPathComponent()
			let date =
				(try? url.resourceValues(forKeys: [.contentModificationDateKey])
					.contentModificationDate) ?? Date.distantPast
			if best == nil || date > best!.date {
				best = (storeDir, date)
			}
		}
		return best?.url ?? fallback
	}

	// `path:line:col: error|warning: message` — one diagnostic per line;
	// continuation/snippet lines are ignored.
	static func parseDiagnostics(_ output: String) -> [BuildDiagnostic] {
		var diagnostics: [BuildDiagnostic] = []
		var seen = Set<String>()
		for line in output.split(separator: "\n") {
			guard let diagnostic = parseDiagnosticLine(String(line)) else {
				continue
			}
			let key = "\(diagnostic.filePath):\(diagnostic.line):\(diagnostic.column):\(diagnostic.severity):\(diagnostic.message)"
			if seen.insert(key).inserted {
				diagnostics.append(diagnostic)
			}
		}
		return diagnostics
	}

	static func parseDiagnosticLine(_ line: String) -> BuildDiagnostic? {
		// <path>:<line>:<col>: <severity>: <message>
		let parts = line.split(separator: ":", maxSplits: 4, omittingEmptySubsequences: false)
		guard parts.count == 5 else {
			return nil
		}
		let path = String(parts[0])
		guard path.hasPrefix("/"),
			let lineNumber = Int(parts[1]),
			let column = Int(parts[2]),
			let severity = BuildDiagnostic.Severity(
				rawValue: parts[3].trimmingCharacters(in: .whitespaces))
		else {
			return nil
		}
		return BuildDiagnostic(
			filePath: path,
			line: lineNumber,
			column: column,
			severity: severity,
			message: String(parts[4]).trimmingCharacters(in: .whitespaces)
		)
	}
}
