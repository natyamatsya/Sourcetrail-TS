import FlatBuffers
import Testing

@testable import SourcetrailSwiftIndexerCore

// Mirrors the Rust side's pop-rewrite tests (ipc/command.rs): the queue
// rewrite must carry EVERY schema field of the remaining commands, or a pop by
// one language silently strips fields from the others' commands.

private func makeCommand(
	type: Sourcetrail_Ipc_IndexerCommandType,
	sourceFilePath: String,
	compilerPath: String = ""
) -> OwnedIndexerCommand {
	OwnedIndexerCommand(
		type: type,
		sourceFilePath: sourceFilePath,
		indexedPaths: [sourceFilePath],
		workingDirectory: "/tmp/project",
		compilerFlags: ["-std=c++20"],
		compilerPath: compilerPath,
		features: ["feat-a"],
		allFeatures: false,
		noDefaultFeatures: true,
		targetTriple: "arm64-apple-macosx",
		specializationScope: "local",
		sourceGroupId: "group-of-\(sourceFilePath)",
		restrictToPackage: type == .rust
	)
}

@Suite struct CommandChannelPopRewriteTests {
	@Test func popFirstSwiftPreservesEveryFieldOnRemainingCommands() throws {
		let input = [
			makeCommand(type: .cxx, sourceFilePath: "a.cpp", compilerPath: "/usr/bin/clang++"),
			makeCommand(type: .swift, sourceFilePath: "pkg"),
			makeCommand(type: .rust, sourceFilePath: "crate"),
		]
		let bytes = SwiftIndexerCommandChannel.serializeQueue(input)

		let (replacement, popped) = SwiftIndexerCommandChannel.popFirstSwiftCommand(
			fromQueueBytes: bytes
		)
		let poppedCommand = try #require(popped)
		#expect(poppedCommand.sourceFilePath == "pkg")
		#expect(poppedCommand.workingDirectory == "/tmp/project")

		let rewritten = try #require(replacement)
		let queue = try #require(SwiftIndexerCommandChannel.decodeQueue(rewritten))
		#expect(queue.commands.count == 2)

		let cxx = queue.commands[0]
		#expect(cxx.type == .cxx)
		#expect(cxx.compilerPath == "/usr/bin/clang++")
		#expect(cxx.compilerFlags.map { $0 ?? "" } == ["-std=c++20"])
		#expect(cxx.sourceGroupId == "group-of-a.cpp")

		// The Rust command's cargo fields and the appended additive fields
		// (source_group_id from fan-out S1, restrict_to_package from crate
		// fan-out R1b) survive the Swift pop-rewrite losslessly.
		let rust = queue.commands[1]
		#expect(rust.type == .rust)
		#expect(rust.indexedPaths.map { $0 ?? "" } == ["crate"])
		#expect(rust.features.map { $0 ?? "" } == ["feat-a"])
		#expect(rust.allFeatures == false)
		#expect(rust.noDefaultFeatures == true)
		#expect(rust.targetTriple == "arm64-apple-macosx")
		#expect(rust.specializationScope == "local")
		#expect(rust.sourceGroupId == "group-of-crate")
		#expect(rust.restrictToPackage == true)
	}

	@Test func popReturnsNilAndDoesNotRewriteWithoutSwiftCommand() throws {
		let input = [
			makeCommand(type: .cxx, sourceFilePath: "only.cpp", compilerPath: "/usr/bin/clang++")
		]
		let bytes = SwiftIndexerCommandChannel.serializeQueue(input)

		let (replacement, popped) = SwiftIndexerCommandChannel.popFirstSwiftCommand(
			fromQueueBytes: bytes
		)
		#expect(replacement == nil)
		#expect(popped == nil)
	}

	@Test func popOfLastCommandRewritesToEmptySentinel() throws {
		let bytes = SwiftIndexerCommandChannel.serializeQueue([
			makeCommand(type: .swift, sourceFilePath: "pkg")
		])

		let (replacement, popped) = SwiftIndexerCommandChannel.popFirstSwiftCommand(
			fromQueueBytes: bytes
		)
		#expect(popped != nil)
		// Four zero bytes mark the queue as empty (matches the C++/Rust side).
		#expect(replacement == [0, 0, 0, 0])
		#expect(SwiftIndexerCommandChannel.isEmpty(replacement ?? []))
	}

	@Test func popOnEmptySentinelIsANoOp() {
		let (replacement, popped) = SwiftIndexerCommandChannel.popFirstSwiftCommand(
			fromQueueBytes: [0, 0, 0, 0]
		)
		#expect(replacement == nil)
		#expect(popped == nil)
	}
}
