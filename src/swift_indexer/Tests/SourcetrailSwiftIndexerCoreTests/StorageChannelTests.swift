import FlatBuffers
import Testing

@testable import SourcetrailSwiftIndexerCore

// Storage push framing: 8-byte needed-capacity + 4-byte entry count, then
// length-prefixed entries — byte-for-byte the Rust storage channel's layout.

@Suite struct StorageChannelFramingTests {
	private static let headerSize = 12

	@Test func appendToEmptyQueueWritesHeaderAndEntry() throws {
		let entry = SwiftIndexerStorageChannel.serializeEmptyStorage()
		let empty = [UInt8](repeating: 0, count: 16 * 1024 * 1024)

		let result = try SwiftIndexerStorageChannel.rewriteQueueByAppendingEntry(
			queueBytes: empty,
			entryBytes: entry
		)
		guard case .written = result.outcome else {
			Issue.record("expected .written, got \(result.outcome)")
			return
		}

		let queue = result.rewrittenQueue
		#expect(SwiftIndexerStorageChannel.readCount(queue) == 1)
		let payloadEnd = try SwiftIndexerStorageChannel.queuePayloadEnd(
			queueBytes: queue,
			count: 1
		)
		#expect(payloadEnd == Self.headerSize + 4 + entry.count)

		// The entry parses back as an IntermediateStorageQueue with one
		// empty storage.
		let entryStart = Self.headerSize + 4
		var buffer = ByteBuffer(bytes: [UInt8](queue[entryStart..<payloadEnd]))
		let storageQueue: Sourcetrail_Ipc_IntermediateStorageQueue =
			try getCheckedRoot(byteBuffer: &buffer)
		#expect(storageQueue.storages.count == 1)
		#expect(storageQueue.storages[0].nodes.isEmpty)
		#expect(storageQueue.storages[0].nextId == 1)
	}

	@Test func appendKeepsExistingEntriesIntact() throws {
		let entry = SwiftIndexerStorageChannel.serializeEmptyStorage()
		let empty = [UInt8](repeating: 0, count: 16 * 1024 * 1024)

		let first = try SwiftIndexerStorageChannel.rewriteQueueByAppendingEntry(
			queueBytes: empty,
			entryBytes: entry
		)
		// Re-pad to segment size: the SHM transaction always hands the full
		// segment to the rewrite.
		var segment = first.rewrittenQueue
		segment.append(contentsOf: [UInt8](repeating: 0, count: empty.count - segment.count))

		let second = try SwiftIndexerStorageChannel.rewriteQueueByAppendingEntry(
			queueBytes: segment,
			entryBytes: entry
		)
		guard case .written = second.outcome else {
			Issue.record("expected .written, got \(second.outcome)")
			return
		}
		#expect(SwiftIndexerStorageChannel.readCount(second.rewrittenQueue) == 2)
		let payloadEnd = try SwiftIndexerStorageChannel.queuePayloadEnd(
			queueBytes: second.rewrittenQueue,
			count: 2
		)
		#expect(payloadEnd == Self.headerSize + 2 * (4 + entry.count))
	}

	@Test func oversizedEntrySignalsNeededCapacityInsteadOfWriting() throws {
		let entry = SwiftIndexerStorageChannel.serializeEmptyStorage()
		// A segment too small for the entry: header fits, entry does not.
		let tiny = [UInt8](repeating: 0, count: Self.headerSize + 8)

		let result = try SwiftIndexerStorageChannel.rewriteQueueByAppendingEntry(
			queueBytes: tiny,
			entryBytes: entry
		)
		guard case .needsGrow(let requested) = result.outcome else {
			Issue.record("expected .needsGrow, got \(result.outcome)")
			return
		}
		#expect(requested > tiny.count)
		// The rewrite only stamps the needed capacity into the header; the
		// count stays untouched so no reader sees a phantom entry.
		#expect(SwiftIndexerStorageChannel.readCount(result.rewrittenQueue) == 0)
	}
}
