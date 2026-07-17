import Testing

@testable import SourcetrailSwiftIndexerCore

// The start/update/finish contract (mirrors the Rust status tests): only a
// re-START without finish marks the previous file as crashed; UPDATE replaces
// the process's current file with no crash bookkeeping.

@Suite struct IndexingStatusBookkeepingTests {
	@Test func updateReplacesCurrentFileWithoutCrashMarking() {
		var status = OwnedIndexingStatus()
		status.applyStartIndexing(processId: 7, filePath: "pkg")
		status.applyUpdateIndexing(processId: 7, filePath: "a.swift")
		status.applyUpdateIndexing(processId: 7, filePath: "b.swift")

		#expect(status.crashedFilePaths.isEmpty)
		#expect(status.currentFiles.count == 1)
		#expect(status.currentFiles[0].filePath == "b.swift")
		#expect(status.indexingFilePaths == ["pkg", "a.swift", "b.swift"])

		status.applyFinishIndexing(processId: 7)
		#expect(status.currentFiles.isEmpty)
		#expect(status.finishedProcessIds == [7])
	}

	@Test func restartWithoutFinishMarksPreviousFileCrashed() {
		var status = OwnedIndexingStatus()
		status.applyStartIndexing(processId: 7, filePath: "first")
		status.applyStartIndexing(processId: 7, filePath: "second")

		#expect(status.crashedFilePaths == ["first"])

		// Finishing "second" clears only that file's crash mark; "first"
		// stays recorded as crashed.
		status.applyFinishIndexing(processId: 7)
		#expect(status.crashedFilePaths == ["first"])
	}
}
