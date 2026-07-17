import Foundation

// Hybrid pipeline for one command: project model → build → semantic pass
// (IndexStoreDB) over files with fresh index units → remaining files fall
// back (SW3: SwiftSyntax decl walk; until then: incomplete file rows).

package enum PackageIndexer {
	package static func index(
		workingDirectory: String,
		options: SwiftBuildOptions = SwiftBuildOptions(),
		specializationScope: SpecializationScope = .local,
		onFile: (String) -> Void
	) -> OwnedIntermediateStorage {
		let packageRoot = URL(fileURLWithPath: workingDirectory)
		let builder = StorageBuilder()

		let model = PackageModelLoader.load(packageRoot: packageRoot, options: options)
		if let reason = model.fallbackReason {
			builder.recordError(
				message: "package model fallback (single synthetic module): \(reason)",
				translationUnit: workingDirectory
			)
		}

		let build = BuildDriver.build(packageRoot: packageRoot, options: options)
		for diagnostic in build.diagnostics where diagnostic.severity == .error {
			builder.recordError(
				message: diagnostic.message
					+ " [\(diagnostic.filePath):\(diagnostic.line):\(diagnostic.column)]",
				translationUnit: diagnostic.filePath
			)
		}
		if !build.succeeded && build.diagnostics.isEmpty {
			builder.recordError(
				message: "swift build failed: \(build.rawOutputTail.suffix(500))",
				translationUnit: workingDirectory
			)
		}

		// Semantic pass over every file with an up-to-date unit — including
		// stale-build survivors: units from an older successful build of
		// UNCHANGED files are still exact.
		let allFiles = model.allSourceFiles
		var covered: Set<String> = []
		do {
			let semantic = try SemanticIndexer(
				storePath: build.indexStorePath,
				databasePath: packageRoot
					.appendingPathComponent(".build/sourcetrail-indexstore-db"),
				builder: builder,
				toolchainPath: options.toolchainPath,
				specializationScope: specializationScope
			)
			for path in semantic.coveredFiles(of: allFiles) {
				onFile(path)
				semantic.indexFile(path: path)
				covered.insert(path)
			}
		} catch {
			builder.recordError(
				message: "semantic index unavailable: \(error)",
				translationUnit: workingDirectory
			)
		}

		// Hybrid merge: everything not covered semantically gets the
		// syntactic declaration walk — strictly exclusive per file, so no
		// duplicate occurrences. complete=false keeps refresh upgrading these
		// files once the build heals.
		for path in allFiles where !covered.contains(path) {
			onFile(path)
			SyntacticIndexer.indexFile(
				path: path,
				moduleName: model.moduleName(forSourceFile: path) ?? model.packageName,
				builder: builder
			)
		}

		return builder.storage
	}
}
