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

package enum BuildDriver {
	package static func build(packageRoot: URL) -> BuildResult {
		let buildPath = packageRoot.appendingPathComponent(".build")
		let output: ProcessOutput
		do {
			output = try ProcessRunner.run(
				executable: "/usr/bin/env",
				arguments: [
					"swift", "build",
					"--package-path", packageRoot.path,
					"--enable-index-store",
				],
				currentDirectory: packageRoot
			)
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

	// SwiftPM writes the store under .build/<triple>/<config>/index/store —
	// resolved lazily by globbing, so we neither hardcode the triple nor the
	// configuration.
	package static func indexStorePath(buildPath: URL) -> URL {
		let fileManager = FileManager.default
		if let tripleDirs = try? fileManager.contentsOfDirectory(
			at: buildPath,
			includingPropertiesForKeys: nil
		) {
			for tripleDir in tripleDirs {
				for config in ["debug", "release"] {
					let candidate = tripleDir
						.appendingPathComponent(config)
						.appendingPathComponent("index/store")
					if fileManager.fileExists(atPath: candidate.path) {
						return candidate
					}
				}
			}
		}
		return buildPath.appendingPathComponent("debug/index/store")
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
