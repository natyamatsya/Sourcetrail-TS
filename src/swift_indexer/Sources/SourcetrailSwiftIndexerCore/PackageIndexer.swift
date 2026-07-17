import Foundation

// SW1 pipeline for one command: project model → build → files + errors.
// The semantic (IndexStoreDB) and syntactic (SwiftSyntax) passes of the
// hybrid engine slot in behind this in SW2/SW3; until then every source file
// is reported incomplete so a refresh re-indexes it once the engine lands.

package enum PackageIndexer {
	package static func index(
		workingDirectory: String,
		onFile: (String) -> Void
	) -> OwnedIntermediateStorage {
		let packageRoot = URL(fileURLWithPath: workingDirectory)
		var storage = OwnedIntermediateStorage()

		let model = PackageModelLoader.load(packageRoot: packageRoot)
		if let reason = model.fallbackReason {
			storage.errors.append(
				OwnedStorageError(
					id: storage.allocateId(),
					message: "package model fallback (single synthetic module): \(reason)",
					translationUnit: workingDirectory,
					fatal: false
				)
			)
		}

		let build = BuildDriver.build(packageRoot: packageRoot)
		for diagnostic in build.diagnostics where diagnostic.severity == .error {
			storage.errors.append(
				OwnedStorageError(
					id: storage.allocateId(),
					message: diagnostic.message
						+ " [\(diagnostic.filePath):\(diagnostic.line):\(diagnostic.column)]",
					translationUnit: diagnostic.filePath,
					fatal: false
				)
			)
		}
		if !build.succeeded && build.diagnostics.isEmpty {
			storage.errors.append(
				OwnedStorageError(
					id: storage.allocateId(),
					message: "swift build failed: \(build.rawOutputTail.suffix(500))",
					translationUnit: workingDirectory,
					fatal: false
				)
			)
		}

		for filePath in model.allSourceFiles {
			onFile(filePath)
			storage.files.append(
				OwnedStorageFile(
					id: storage.allocateId(),
					filePath: filePath,
					// complete=false until the hybrid engine emits symbols
					// (SW2/SW3): the app's refresh then re-indexes these files
					// instead of trusting an empty result forever.
					complete: false
				)
			)
		}

		return storage
	}
}
