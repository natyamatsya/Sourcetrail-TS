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

	@Test func filesAndErrorsRoundTripThroughSerialization() throws {
		var storage = OwnedIntermediateStorage()
		storage.errors.append(
			OwnedStorageError(
				id: storage.allocateId(),
				message: "cannot find 'foo' in scope [/pkg/A.swift:12:7]",
				translationUnit: "/pkg/A.swift",
				fatal: false
			)
		)
		storage.files.append(
			OwnedStorageFile(id: storage.allocateId(), filePath: "/pkg/A.swift", complete: false)
		)

		let bytes = SwiftIndexerStorageChannel.serializeStorage(storage)
		var buffer = ByteBuffer(bytes: bytes)
		let queue: Sourcetrail_Ipc_IntermediateStorageQueue = try getCheckedRoot(byteBuffer: &buffer)
		#expect(queue.storages.count == 1)
		let decoded = queue.storages[0]
		#expect(decoded.nextId == 3)
		#expect(decoded.files.count == 1)
		#expect(decoded.files[0].filePath == "/pkg/A.swift")
		#expect(decoded.files[0].languageIdentifier == "swift")
		#expect(decoded.files[0].indexed == true)
		#expect(decoded.files[0].complete == false)
		#expect(decoded.errors.count == 1)
		#expect(decoded.errors[0].message == "cannot find 'foo' in scope [/pkg/A.swift:12:7]")
		#expect(decoded.errors[0].translationUnit == "/pkg/A.swift")
		#expect(decoded.errors[0].fatal == false)
		#expect(decoded.errors[0].indexed == true)
	}

	@Test func nodeAttributesRoundTripThroughSerialization() throws {
		let builder = StorageBuilder()
		let nodeId = builder.nodeId(parts: ["Demo", "Model"], kind: NodeKind.class)
		builder.recordNodeAttribute(
			nodeId: nodeId, key: NodeAttributeKind.availability, value: "@available(macOS 14, *)")
		builder.recordNodeAttribute(
			nodeId: nodeId, key: NodeAttributeKind.deprecated, value: "use NewModel")
		// The (node, key, value) triple deduplicates — a repeat is dropped.
		builder.recordNodeAttribute(
			nodeId: nodeId, key: NodeAttributeKind.availability, value: "@available(macOS 14, *)")

		let bytes = SwiftIndexerStorageChannel.serializeStorage(builder.storage)
		var buffer = ByteBuffer(bytes: bytes)
		let queue: Sourcetrail_Ipc_IntermediateStorageQueue = try getCheckedRoot(byteBuffer: &buffer)
		let decoded = queue.storages[0]
		#expect(decoded.nodeAttributes.count == 2)
		#expect(decoded.nodeAttributes.contains {
			$0.nodeId == nodeId && $0.key == NodeAttributeKind.availability
				&& $0.value == "@available(macOS 14, *)"
		})
		#expect(decoded.nodeAttributes.contains {
			$0.nodeId == nodeId && $0.key == NodeAttributeKind.deprecated && $0.value == "use NewModel"
		})
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
