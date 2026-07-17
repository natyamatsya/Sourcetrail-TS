import FlatBuffers
import Foundation
import LibIPC

package struct SwiftIndexerCommand {
	package let sourceFilePath: String
	package let workingDirectory: String
	// SW5 project-model options; empty = defaults.
	package let buildArgs: [String]
	package let toolchainPath: String
	package let indexStorePath: String

	package init(
		sourceFilePath: String,
		workingDirectory: String,
		buildArgs: [String] = [],
		toolchainPath: String = "",
		indexStorePath: String = ""
	) {
		self.sourceFilePath = sourceFilePath
		self.workingDirectory = workingDirectory
		self.buildArgs = buildArgs
		self.toolchainPath = toolchainPath
		self.indexStorePath = indexStorePath
	}
}

package final class SwiftIndexerCommandChannel {
	private static let shmSize = 64 * 1024 * 1024

	private let shm: IpcSharedMemoryRaw

	package static func open(instanceUuid: String) async throws(IpcError) -> SwiftIndexerCommandChannel {
		let shm = try await IpcSharedMemoryRaw.open(
			name: "icmd_ipc_\(instanceUuid)",
			size: shmSize
		)
		return SwiftIndexerCommandChannel(shm: shm)
	}

	private init(shm: IpcSharedMemoryRaw) {
		self.shm = shm
	}

	package func popSwiftCommand() throws -> SwiftIndexerCommand? {
		try shm.readModifyWrite { queueBytes in
			Self.popFirstSwiftCommand(fromQueueBytes: queueBytes)
		}
	}

	// Pure pop-rewrite logic, separated from the shared-memory transaction so
	// tests can exercise it on raw queue bytes (mirrors the Rust side's
	// pop_first_rust_from_queue_bytes).
	static func popFirstSwiftCommand(
		fromQueueBytes queueBytes: [UInt8]
	) -> (replacement: [UInt8]?, result: SwiftIndexerCommand?) {
		if isEmpty(queueBytes) {
			return (replacement: nil, result: nil)
		}

		guard let queue = decodeQueue(queueBytes) else {
			return (replacement: nil, result: nil)
		}

		var allCommands: [OwnedIndexerCommand] = []
		allCommands.reserveCapacity(queue.commands.count)
		var swiftCommandIndex: Int?

		for command in queue.commands {
			let owned = OwnedIndexerCommand(from: command)
			if swiftCommandIndex == nil, owned.type == .swift {
				swiftCommandIndex = allCommands.count
			}
			allCommands.append(owned)
		}

		guard let commandIndex = swiftCommandIndex, commandIndex < allCommands.count else {
			return (replacement: nil, result: nil)
		}

		let selected = allCommands.remove(at: commandIndex)
		let rewrittenQueue = allCommands.isEmpty
			? [UInt8](repeating: 0, count: 4)
			: serializeQueue(allCommands)

		let swiftCommand = SwiftIndexerCommand(
			sourceFilePath: selected.sourceFilePath,
			workingDirectory: selected.workingDirectory,
			buildArgs: selected.swiftBuildArgs,
			toolchainPath: selected.swiftToolchainPath,
			indexStorePath: selected.swiftIndexStorePath
		)
		return (replacement: rewrittenQueue, result: swiftCommand)
	}

	static func isEmpty(_ bytes: [UInt8]) -> Bool {
		if bytes.count < 4 {
			return true
		}
		return bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0
	}

	static func decodeQueue(_ bytes: [UInt8]) -> Sourcetrail_Ipc_IndexerCommandQueue? {
		var byteBuffer = ByteBuffer(bytes: bytes)
		let queue: Sourcetrail_Ipc_IndexerCommandQueue = (try? getCheckedRoot(byteBuffer: &byteBuffer)) ?? getRoot(byteBuffer: &byteBuffer)
		return queue
	}

	static func serializeQueue(_ commands: [OwnedIndexerCommand]) -> [UInt8] {
		var builder = FlatBufferBuilder(initialSize: 4096)

		let commandOffsets = commands.map { command in
			let sourceFilePathOffset = builder.create(string: command.sourceFilePath)
			let indexedPathsOffset = builder.createVector(ofStrings: command.indexedPaths)
			let excludeFiltersOffset = builder.createVector(ofStrings: command.excludeFilters)
			let includeFiltersOffset = builder.createVector(ofStrings: command.includeFilters)
			let workingDirectoryOffset = builder.create(string: command.workingDirectory)
			let compilerFlagsOffset = builder.createVector(ofStrings: command.compilerFlags)
			let compilerPathOffset = command.compilerPath.isEmpty
				? Offset()
				: builder.create(string: command.compilerPath)
			let featuresOffset = builder.createVector(ofStrings: command.features)
			let targetTripleOffset = command.targetTriple.isEmpty
				? Offset()
				: builder.create(string: command.targetTriple)
			let specializationScopeOffset = command.specializationScope.isEmpty
				? Offset()
				: builder.create(string: command.specializationScope)
			let sourceGroupIdOffset = command.sourceGroupId.isEmpty
				? Offset()
				: builder.create(string: command.sourceGroupId)
			let swiftBuildArgsOffset = builder.createVector(ofStrings: command.swiftBuildArgs)
			let swiftToolchainPathOffset = command.swiftToolchainPath.isEmpty
				? Offset()
				: builder.create(string: command.swiftToolchainPath)
			let swiftIndexStorePathOffset = command.swiftIndexStorePath.isEmpty
				? Offset()
				: builder.create(string: command.swiftIndexStorePath)

			return Sourcetrail_Ipc_IndexerCommand.createIndexerCommand(
				&builder,
				type: command.type,
				sourceFilePathOffset: sourceFilePathOffset,
				indexedPathsVectorOffset: indexedPathsOffset,
				excludeFiltersVectorOffset: excludeFiltersOffset,
				includeFiltersVectorOffset: includeFiltersOffset,
				workingDirectoryOffset: workingDirectoryOffset,
				compilerFlagsVectorOffset: compilerFlagsOffset,
				compilerPathOffset: compilerPathOffset,
				featuresVectorOffset: featuresOffset,
				allFeatures: command.allFeatures,
				noDefaultFeatures: command.noDefaultFeatures,
				targetTripleOffset: targetTripleOffset,
				specializationScopeOffset: specializationScopeOffset,
				sourceGroupIdOffset: sourceGroupIdOffset,
				restrictToPackage: command.restrictToPackage,
				swiftBuildArgsVectorOffset: swiftBuildArgsOffset,
				swiftToolchainPathOffset: swiftToolchainPathOffset,
				swiftIndexStorePathOffset: swiftIndexStorePathOffset
			)
		}

		let commandsVectorOffset = builder.createVector(ofOffsets: commandOffsets)
		let queueOffset = Sourcetrail_Ipc_IndexerCommandQueue.createIndexerCommandQueue(
			&builder,
			commandsVectorOffset: commandsVectorOffset
		)
		builder.finish(offset: queueOffset)
		return builder.sizedByteArray
	}
}

// Owned copy of a command for the pop-rewrite. Must carry EVERY schema field:
// commands of other languages pass through this copy when the queue is
// rewritten, so a missing field here silently strips it from their commands.
struct OwnedIndexerCommand {
	let type: Sourcetrail_Ipc_IndexerCommandType
	let sourceFilePath: String
	let indexedPaths: [String]
	let excludeFilters: [String]
	let includeFilters: [String]
	let workingDirectory: String
	let compilerFlags: [String]
	let compilerPath: String
	let features: [String]
	let allFeatures: Bool
	let noDefaultFeatures: Bool
	let targetTriple: String
	let specializationScope: String
	let sourceGroupId: String
	let restrictToPackage: Bool
	let swiftBuildArgs: [String]
	let swiftToolchainPath: String
	let swiftIndexStorePath: String

	init(
		type: Sourcetrail_Ipc_IndexerCommandType,
		sourceFilePath: String,
		indexedPaths: [String] = [],
		excludeFilters: [String] = [],
		includeFilters: [String] = [],
		workingDirectory: String = "",
		compilerFlags: [String] = [],
		compilerPath: String = "",
		features: [String] = [],
		allFeatures: Bool = false,
		noDefaultFeatures: Bool = false,
		targetTriple: String = "",
		specializationScope: String = "",
		sourceGroupId: String = "",
		restrictToPackage: Bool = false,
		swiftBuildArgs: [String] = [],
		swiftToolchainPath: String = "",
		swiftIndexStorePath: String = ""
	) {
		self.type = type
		self.sourceFilePath = sourceFilePath
		self.indexedPaths = indexedPaths
		self.excludeFilters = excludeFilters
		self.includeFilters = includeFilters
		self.workingDirectory = workingDirectory
		self.compilerFlags = compilerFlags
		self.compilerPath = compilerPath
		self.features = features
		self.allFeatures = allFeatures
		self.noDefaultFeatures = noDefaultFeatures
		self.targetTriple = targetTriple
		self.specializationScope = specializationScope
		self.sourceGroupId = sourceGroupId
		self.restrictToPackage = restrictToPackage
		self.swiftBuildArgs = swiftBuildArgs
		self.swiftToolchainPath = swiftToolchainPath
		self.swiftIndexStorePath = swiftIndexStorePath
	}

	init(from command: Sourcetrail_Ipc_IndexerCommand) {
		type = command.type
		sourceFilePath = command.sourceFilePath ?? ""
		indexedPaths = command.indexedPaths.map { $0 ?? "" }
		excludeFilters = command.excludeFilters.map { $0 ?? "" }
		includeFilters = command.includeFilters.map { $0 ?? "" }
		workingDirectory = command.workingDirectory ?? ""
		compilerFlags = command.compilerFlags.map { $0 ?? "" }
		compilerPath = command.compilerPath ?? ""
		features = command.features.map { $0 ?? "" }
		allFeatures = command.allFeatures
		noDefaultFeatures = command.noDefaultFeatures
		targetTriple = command.targetTriple ?? ""
		specializationScope = command.specializationScope ?? ""
		sourceGroupId = command.sourceGroupId ?? ""
		restrictToPackage = command.restrictToPackage
		swiftBuildArgs = command.swiftBuildArgs.map { $0 ?? "" }
		swiftToolchainPath = command.swiftToolchainPath ?? ""
		swiftIndexStorePath = command.swiftIndexStorePath ?? ""
	}
}
