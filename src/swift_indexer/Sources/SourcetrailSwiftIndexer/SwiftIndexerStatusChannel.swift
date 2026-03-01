import FlatBuffers
import Foundation
import LibIPC

final class SwiftIndexerStatusChannel {
	private static let shmSize = 1024 * 1024

	private let processId: UInt64
	private let shm: IpcSharedMemoryRaw

	static func open(instanceUuid: String, processId: UInt64) async throws(IpcError) -> SwiftIndexerStatusChannel {
		let shm = try await IpcSharedMemoryRaw.open(
			name: "ists_ipc_\(instanceUuid)",
			size: shmSize
		)
		return SwiftIndexerStatusChannel(processId: processId, shm: shm)
	}

	private init(processId: UInt64, shm: IpcSharedMemoryRaw) {
		self.processId = processId
		self.shm = shm
	}

	func isInterrupted() throws -> Bool {
		try shm.readLocked { bytes in
			Self.decodeStatus(from: bytes).indexingInterrupted
		}
	}

	func isQueueStopped() throws -> Bool {
		try shm.readLocked { bytes in
			Self.decodeStatus(from: bytes).queueStopped
		}
	}

	func startIndexing(filePath: String) throws {
		_ = try shm.readModifyWrite { bytes in
			var status = Self.decodeStatus(from: bytes)
			status.applyStartIndexing(processId: processId, filePath: filePath)
			return (replacement: Self.serializeStatus(status), result: ())
		}
	}

	func finishIndexing() throws {
		_ = try shm.readModifyWrite { bytes in
			var status = Self.decodeStatus(from: bytes)
			status.applyFinishIndexing(processId: processId)
			return (replacement: Self.serializeStatus(status), result: ())
		}
	}

	private static func decodeStatus(from bytes: [UInt8]) -> OwnedIndexingStatus {
		if bytes.count < 4 {
			return OwnedIndexingStatus()
		}
		if bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0 {
			return OwnedIndexingStatus()
		}

		var buffer = ByteBuffer(bytes: bytes)
		guard let status: Sourcetrail_Ipc_IndexingStatus = try? getCheckedRoot(byteBuffer: &buffer) else {
			return OwnedIndexingStatus()
		}
		return OwnedIndexingStatus(from: status)
	}

	private static func serializeStatus(_ status: OwnedIndexingStatus) -> [UInt8] {
		var builder = FlatBufferBuilder(initialSize: 4096)

		let indexingFilePathsOffset = builder.createVector(ofStrings: status.indexingFilePaths)

		let currentFileOffsets = status.currentFiles.map { processFile in
			let filePathOffset = builder.create(string: processFile.filePath)
			return Sourcetrail_Ipc_ProcessFile.createProcessFile(
				&builder,
				processId: processFile.processId,
				filePathOffset: filePathOffset
			)
		}
		let currentFilesOffset = builder.createVector(ofOffsets: currentFileOffsets)

		let crashedFilePathsOffset = builder.createVector(ofStrings: status.crashedFilePaths)
		let finishedProcessIdsOffset = builder.createVector(status.finishedProcessIds)

		let statusOffset = Sourcetrail_Ipc_IndexingStatus.createIndexingStatus(
			&builder,
			indexingFilePathsVectorOffset: indexingFilePathsOffset,
			currentFilesVectorOffset: currentFilesOffset,
			crashedFilePathsVectorOffset: crashedFilePathsOffset,
			finishedProcessIdsVectorOffset: finishedProcessIdsOffset,
			indexingInterrupted: status.indexingInterrupted,
			queueStopped: status.queueStopped
		)
		builder.finish(offset: statusOffset)
		return builder.sizedByteArray
	}
}

private struct OwnedIndexingStatus {
	struct ProcessFileRecord {
		let processId: UInt64
		let filePath: String
	}

	var indexingFilePaths: [String] = []
	var currentFiles: [ProcessFileRecord] = []
	var crashedFilePaths: [String] = []
	var finishedProcessIds: [UInt64] = []
	var indexingInterrupted = false
	var queueStopped = false

	init() {}

	init(from status: Sourcetrail_Ipc_IndexingStatus) {
		indexingFilePaths = Self.readStrings(
			count: status.indexingFilePathsCount,
			getter: status.indexingFilePaths
		)

		currentFiles.reserveCapacity(Int(status.currentFilesCount))
		for index in 0..<status.currentFilesCount {
			guard let processFile = status.currentFiles(at: index) else {
				continue
			}
			currentFiles.append(
				ProcessFileRecord(
					processId: processFile.processId,
					filePath: processFile.filePath ?? ""
				)
			)
		}

		crashedFilePaths = Self.readStrings(
			count: status.crashedFilePathsCount,
			getter: status.crashedFilePaths
		)

		finishedProcessIds.reserveCapacity(Int(status.finishedProcessIdsCount))
		for index in 0..<status.finishedProcessIdsCount {
			finishedProcessIds.append(status.finishedProcessIds(at: index))
		}

		indexingInterrupted = status.indexingInterrupted
		queueStopped = status.queueStopped
	}

	mutating func applyStartIndexing(processId: UInt64, filePath: String) {
		indexingFilePaths.append(filePath)

		if let currentIndex = currentFiles.firstIndex(where: { $0.processId == processId }) {
			let previousFilePath = currentFiles.remove(at: currentIndex).filePath
			if !crashedFilePaths.contains(previousFilePath) {
				crashedFilePaths.append(previousFilePath)
			}
		}

		currentFiles.append(ProcessFileRecord(processId: processId, filePath: filePath))
	}

	mutating func applyFinishIndexing(processId: UInt64) {
		var finishedFilePath = ""
		if let currentIndex = currentFiles.firstIndex(where: { $0.processId == processId }) {
			finishedFilePath = currentFiles.remove(at: currentIndex).filePath
		}

		finishedProcessIds.append(processId)
		if finishedFilePath.isEmpty {
			return
		}

		crashedFilePaths.removeAll { $0 == finishedFilePath }
	}

	private static func readStrings(
		count: Int32,
		getter: (Int32) -> String?
	) -> [String] {
		if count <= 0 {
			return []
		}

		var strings: [String] = []
		strings.reserveCapacity(Int(count))
		for index in 0..<count {
			strings.append(getter(index) ?? "")
		}
		return strings
	}
}
