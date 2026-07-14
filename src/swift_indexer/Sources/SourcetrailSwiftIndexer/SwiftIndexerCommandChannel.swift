import FlatBuffers
import Foundation
import LibIPC

struct SwiftIndexerCommand {
	let sourceFilePath: String
	let workingDirectory: String
}

final class SwiftIndexerCommandChannel {
	private static let shmSize = 64 * 1024 * 1024

	private let shm: IpcSharedMemoryRaw

	static func open(instanceUuid: String) async throws(IpcError) -> SwiftIndexerCommandChannel {
		let shm = try await IpcSharedMemoryRaw.open(
			name: "icmd_ipc_\(instanceUuid)",
			size: shmSize
		)
		return SwiftIndexerCommandChannel(shm: shm)
	}

	private init(shm: IpcSharedMemoryRaw) {
		self.shm = shm
	}

	func popSwiftCommand() throws -> SwiftIndexerCommand? {
		try shm.readModifyWrite { queueBytes in
			if Self.isEmpty(queueBytes) {
				return (replacement: nil, result: nil)
			}

			guard let queue = Self.decodeQueue(queueBytes) else {
				return (replacement: nil, result: nil)
			}

			var allCommands: [OwnedIndexerCommand] = []
			allCommands.reserveCapacity(Int(queue.commandsCount))
			var swiftCommandIndex: Int?

			for index in 0..<queue.commandsCount {
				guard let command = queue.commands(at: index) else {
					continue
				}

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
				: Self.serializeQueue(allCommands)

			let swiftCommand = SwiftIndexerCommand(
				sourceFilePath: selected.sourceFilePath,
				workingDirectory: selected.workingDirectory
			)
			return (replacement: rewrittenQueue, result: swiftCommand)
		}
	}

	private static func isEmpty(_ bytes: [UInt8]) -> Bool {
		if bytes.count < 4 {
			return true
		}
		return bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0
	}

	private static func decodeQueue(_ bytes: [UInt8]) -> Sourcetrail_Ipc_IndexerCommandQueue? {
		var byteBuffer = ByteBuffer(bytes: bytes)
		let queue: Sourcetrail_Ipc_IndexerCommandQueue = (try? getCheckedRoot(byteBuffer: &byteBuffer)) ?? getRoot(byteBuffer: &byteBuffer)
		return queue
	}

	private static func serializeQueue(_ commands: [OwnedIndexerCommand]) -> [UInt8] {
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
				sourceGroupIdOffset: sourceGroupIdOffset
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
private struct OwnedIndexerCommand {
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

	init(from command: Sourcetrail_Ipc_IndexerCommand) {
		type = command.type
		sourceFilePath = command.sourceFilePath ?? ""
		indexedPaths = Self.readStrings(count: command.indexedPathsCount, getter: command.indexedPaths)
		excludeFilters = Self.readStrings(count: command.excludeFiltersCount, getter: command.excludeFilters)
		includeFilters = Self.readStrings(count: command.includeFiltersCount, getter: command.includeFilters)
		workingDirectory = command.workingDirectory ?? ""
		compilerFlags = Self.readStrings(count: command.compilerFlagsCount, getter: command.compilerFlags)
		compilerPath = command.compilerPath ?? ""
		features = Self.readStrings(count: command.featuresCount, getter: command.features)
		allFeatures = command.allFeatures
		noDefaultFeatures = command.noDefaultFeatures
		targetTriple = command.targetTriple ?? ""
		specializationScope = command.specializationScope ?? ""
		sourceGroupId = command.sourceGroupId ?? ""
	}

	private static func readStrings(
		count: Int32,
		getter: (Int32) -> String?
	) -> [String] {
		if count <= 0 {
			return []
		}

		var values: [String] = []
		values.reserveCapacity(Int(count))
		for index in 0..<count {
			values.append(getter(index) ?? "")
		}
		return values
	}
}
