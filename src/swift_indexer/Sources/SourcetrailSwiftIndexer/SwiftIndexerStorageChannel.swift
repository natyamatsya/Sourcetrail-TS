import FlatBuffers
import Foundation
import LibIPC

final class SwiftIndexerStorageChannel {
	private static let shmSize = 16 * 1024 * 1024
	private static let neededCapacityFieldSize = 8
	private static let countFieldSize = 4
	private static let headerSize = neededCapacityFieldSize + countFieldSize

	private let shm: IpcSharedMemoryRaw

	static func open(instanceUuid: String, processId: UInt64) async throws(IpcError) -> SwiftIndexerStorageChannel {
		let shm = try await IpcSharedMemoryRaw.open(
			name: "iist_ipc_\(processId)_\(instanceUuid)",
			size: shmSize
		)
		return SwiftIndexerStorageChannel(shm: shm)
	}

	private init(shm: IpcSharedMemoryRaw) {
		self.shm = shm
	}

	func storageCount() throws -> Int {
		try shm.readLocked { queueBytes in
			Int(Self.readCount(queueBytes))
		}
	}

	func pushEmptyStorage() throws {
		let entryBytes = Self.serializeEmptyStorage()
		while true {
			let outcome: PushOutcome = try shm.readModifyWrite { queueBytes in
				let appendResult = try Self.rewriteQueueByAppendingEntry(
					queueBytes: queueBytes,
					entryBytes: entryBytes
				)
				return (replacement: appendResult.rewrittenQueue, result: appendResult.outcome)
			}

			switch outcome {
			case .written:
				return
			case .needsGrow(let requestedCapacity):
				throw SwiftIndexerIpcError.sharedMemoryWriteTooLarge(
					requested: requestedCapacity,
					available: Self.shmSize
				)
			}
		}
	}

	private enum PushOutcome {
		case written
		case needsGrow(Int)
	}

	private struct AppendResult {
		let rewrittenQueue: [UInt8]
		let outcome: PushOutcome
	}

	private static func rewriteQueueByAppendingEntry(
		queueBytes: [UInt8],
		entryBytes: [UInt8]
	) throws -> AppendResult {
		let count = readCount(queueBytes)
		let payloadEnd = try queuePayloadEnd(queueBytes: queueBytes, count: count)
		let existingPayloadSize = payloadEnd - headerSize
		let requiredSize = try requiredQueueSizeForAppend(
			existingPayloadSize: existingPayloadSize,
			entrySize: entryBytes.count
		)

		if requiredSize > queueBytes.count {
			let growTo = growthTarget(requiredSize: requiredSize)
			var signaledQueue = queueBytes
			writeUInt64(&signaledQueue, offset: 0, value: UInt64(growTo))
			return AppendResult(rewrittenQueue: signaledQueue, outcome: .needsGrow(growTo))
		}

		let entrySize = UInt32(entryBytes.count)
		var rewrittenQueue = [UInt8](repeating: 0, count: requiredSize)
		writeUInt64(&rewrittenQueue, offset: 0, value: readUInt64(queueBytes, offset: 0))
		writeUInt32(&rewrittenQueue, offset: neededCapacityFieldSize, value: count + 1)

		if existingPayloadSize > 0 {
			rewrittenQueue[headerSize..<headerSize + existingPayloadSize] = queueBytes[headerSize..<payloadEnd]
		}

		let entrySizeOffset = headerSize + existingPayloadSize
		writeUInt32(&rewrittenQueue, offset: entrySizeOffset, value: entrySize)
		let entryPayloadOffset = entrySizeOffset + countFieldSize
		rewrittenQueue[entryPayloadOffset..<entryPayloadOffset + entryBytes.count] = entryBytes[0..<entryBytes.count]

		return AppendResult(rewrittenQueue: rewrittenQueue, outcome: .written)
	}

	private static func requiredQueueSizeForAppend(
		existingPayloadSize: Int,
		entrySize: Int
	) throws -> Int {
		let partial = headerSize + existingPayloadSize + countFieldSize
		let total = partial + entrySize
		if total < partial {
			throw SwiftIndexerIpcError.invalidStorageQueue("storage queue size overflow")
		}
		return total
	}

	private static func queuePayloadEnd(queueBytes: [UInt8], count: UInt32) throws -> Int {
		var cursor = headerSize
		for _ in 0..<count {
			if cursor + countFieldSize > queueBytes.count {
				throw SwiftIndexerIpcError.invalidStorageQueue("truncated storage entry size")
			}

			let size = Int(readUInt32(queueBytes, offset: cursor))
			cursor += countFieldSize

			if cursor + size > queueBytes.count {
				throw SwiftIndexerIpcError.invalidStorageQueue("truncated storage entry payload")
			}
			cursor += size
		}
		return cursor
	}

	private static func growthTarget(requiredSize: Int) -> Int {
		let doubled = requiredSize * 2
		if doubled >= requiredSize {
			return doubled
		}
		return requiredSize
	}

	private static func readCount(_ queueBytes: [UInt8]) -> UInt32 {
		if queueBytes.count < headerSize {
			return 0
		}
		return readUInt32(queueBytes, offset: neededCapacityFieldSize)
	}

	private static func readUInt32(_ bytes: [UInt8], offset: Int) -> UInt32 {
		if offset + 4 > bytes.count {
			return 0
		}

		let value = UInt32(bytes[offset])
			| (UInt32(bytes[offset + 1]) << 8)
			| (UInt32(bytes[offset + 2]) << 16)
			| (UInt32(bytes[offset + 3]) << 24)
		return value
	}

	private static func readUInt64(_ bytes: [UInt8], offset: Int) -> UInt64 {
		if offset + 8 > bytes.count {
			return 0
		}

		let value = UInt64(bytes[offset])
			| (UInt64(bytes[offset + 1]) << 8)
			| (UInt64(bytes[offset + 2]) << 16)
			| (UInt64(bytes[offset + 3]) << 24)
			| (UInt64(bytes[offset + 4]) << 32)
			| (UInt64(bytes[offset + 5]) << 40)
			| (UInt64(bytes[offset + 6]) << 48)
			| (UInt64(bytes[offset + 7]) << 56)
		return value
	}

	private static func writeUInt32(_ bytes: inout [UInt8], offset: Int, value: UInt32) {
		if offset + 4 > bytes.count {
			return
		}

		let littleEndian = value.littleEndian
		bytes[offset] = UInt8(truncatingIfNeeded: littleEndian)
		bytes[offset + 1] = UInt8(truncatingIfNeeded: littleEndian >> 8)
		bytes[offset + 2] = UInt8(truncatingIfNeeded: littleEndian >> 16)
		bytes[offset + 3] = UInt8(truncatingIfNeeded: littleEndian >> 24)
	}

	private static func writeUInt64(_ bytes: inout [UInt8], offset: Int, value: UInt64) {
		if offset + 8 > bytes.count {
			return
		}

		let littleEndian = value.littleEndian
		bytes[offset] = UInt8(truncatingIfNeeded: littleEndian)
		bytes[offset + 1] = UInt8(truncatingIfNeeded: littleEndian >> 8)
		bytes[offset + 2] = UInt8(truncatingIfNeeded: littleEndian >> 16)
		bytes[offset + 3] = UInt8(truncatingIfNeeded: littleEndian >> 24)
		bytes[offset + 4] = UInt8(truncatingIfNeeded: littleEndian >> 32)
		bytes[offset + 5] = UInt8(truncatingIfNeeded: littleEndian >> 40)
		bytes[offset + 6] = UInt8(truncatingIfNeeded: littleEndian >> 48)
		bytes[offset + 7] = UInt8(truncatingIfNeeded: littleEndian >> 56)
	}

	private static func serializeEmptyStorage() -> [UInt8] {
		var builder = FlatBufferBuilder(initialSize: 1024)

		let nodesOffset = builder.createVector(ofOffsets: [Offset]())
		let filesOffset = builder.createVector(ofOffsets: [Offset]())
		let edgesOffset = builder.createVector(ofOffsets: [Offset]())
		let symbolsOffset = builder.createVector(ofOffsets: [Offset]())
		let sourceLocationsOffset = builder.createVector(ofOffsets: [Offset]())
		let localSymbolsOffset = builder.createVector(ofOffsets: [Offset]())
		let occurrencesOffset = builder.createVector(ofOffsets: [Offset]())
		let componentAccessesOffset = builder.createVector(ofOffsets: [Offset]())
		let errorsOffset = builder.createVector(ofOffsets: [Offset]())

		let storageOffset = Sourcetrail_Ipc_IntermediateStorage.createIntermediateStorage(
			&builder,
			nextId: 1,
			nodesVectorOffset: nodesOffset,
			filesVectorOffset: filesOffset,
			edgesVectorOffset: edgesOffset,
			symbolsVectorOffset: symbolsOffset,
			sourceLocationsVectorOffset: sourceLocationsOffset,
			localSymbolsVectorOffset: localSymbolsOffset,
			occurrencesVectorOffset: occurrencesOffset,
			componentAccessesVectorOffset: componentAccessesOffset,
			errorsVectorOffset: errorsOffset
		)

		let storagesOffset = builder.createVector(ofOffsets: [storageOffset])
		let queueOffset = Sourcetrail_Ipc_IntermediateStorageQueue.createIntermediateStorageQueue(
			&builder,
			storagesVectorOffset: storagesOffset
		)
		builder.finish(offset: queueOffset)
		return builder.sizedByteArray
	}
}
