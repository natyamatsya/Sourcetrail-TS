import Foundation

// SPM project model for one commanded package root: module names and the
// file→module map, from `swift package describe --type json`. The analog of
// the C++ side's cargo-metadata enumeration — and like it, enumeration
// failure degrades gracefully: the whole directory becomes one synthetic
// module so indexing still proceeds on broken manifests.

package struct PackageModule {
	package let name: String
	package let sourceFiles: [String]

	package init(name: String, sourceFiles: [String]) {
		self.name = name
		self.sourceFiles = sourceFiles
	}
}

package struct PackageModel {
	package let packageName: String
	package let modules: [PackageModule]
	// Set when `swift package describe` failed and the model is the
	// one-synthetic-module fallback.
	package let isFallback: Bool
	package let fallbackReason: String?

	package func moduleName(forSourceFile path: String) -> String? {
		for module in modules where module.sourceFiles.contains(path) {
			return module.name
		}
		return nil
	}

	package var allSourceFiles: [String] {
		modules.flatMap(\.sourceFiles)
	}
}

package enum PackageModelLoader {
	static let describeTimeout: TimeInterval = 60

	package static func load(packageRoot: URL) -> PackageModel {
		let output: ProcessOutput
		do {
			output = try ProcessRunner.run(
				executable: "/usr/bin/env",
				arguments: ["swift", "package", "describe", "--type", "json"],
				currentDirectory: packageRoot,
				timeout: describeTimeout
			)
		} catch {
			return fallbackModel(packageRoot: packageRoot, reason: "swift package describe failed to launch: \(error)")
		}
		if output.timedOut {
			return fallbackModel(packageRoot: packageRoot, reason: "swift package describe timed out after \(Int(describeTimeout))s")
		}
		if output.exitCode != 0 {
			return fallbackModel(
				packageRoot: packageRoot,
				reason: "swift package describe exited \(output.exitCode): \(output.stderr.prefix(500))"
			)
		}
		guard let model = parseDescribeJson(output.stdout, packageRoot: packageRoot) else {
			return fallbackModel(packageRoot: packageRoot, reason: "swift package describe output not parseable")
		}
		return model
	}

	// `describe --type json` emits `targets[]` with `path` relative to the
	// package root and `sources[]` relative to the target path.
	static func parseDescribeJson(_ json: String, packageRoot: URL) -> PackageModel? {
		struct DescribeTarget: Decodable {
			let name: String
			let path: String
			let sources: [String]
		}
		struct DescribePackage: Decodable {
			let name: String
			let targets: [DescribeTarget]
		}

		guard let data = json.data(using: .utf8),
			let package_ = try? JSONDecoder().decode(DescribePackage.self, from: data)
		else {
			return nil
		}

		let modules = package_.targets.map { target in
			let targetRoot = packageRoot.appendingPathComponent(target.path)
			let files = target.sources
				.filter { $0.hasSuffix(".swift") }
				.map { targetRoot.appendingPathComponent($0).standardizedFileURL.path }
			return PackageModule(name: target.name, sourceFiles: files)
		}
		return PackageModel(
			packageName: package_.name,
			modules: modules,
			isFallback: false,
			fallbackReason: nil
		)
	}

	// One synthetic module named after the directory, covering every .swift
	// file under the root (build dirs excluded).
	static func fallbackModel(packageRoot: URL, reason: String) -> PackageModel {
		var files: [String] = []
		let fileManager = FileManager.default
		if let enumerator = fileManager.enumerator(
			at: packageRoot,
			includingPropertiesForKeys: [.isRegularFileKey],
			options: [.skipsHiddenFiles]
		) {
			for case let url as URL in enumerator {
				if url.lastPathComponent == ".build" || url.lastPathComponent == ".git" {
					enumerator.skipDescendants()
					continue
				}
				if url.pathExtension == "swift" {
					files.append(url.standardizedFileURL.path)
				}
			}
		}
		return PackageModel(
			packageName: packageRoot.lastPathComponent,
			modules: [PackageModule(name: packageRoot.lastPathComponent, sourceFiles: files.sorted())],
			isFallback: true,
			fallbackReason: reason
		)
	}
}
