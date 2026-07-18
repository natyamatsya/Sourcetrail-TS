import Darwin.POSIX
import ThothIPC

enum SwiftIndexerIpcError: Error {
	case sharedMemoryWriteTooLarge(requested: Int, available: Int)
	case invalidStorageQueue(String)
	// A non-empty IndexingStatus segment that failed flatbuffers verification —
	// surfaced rather than silently treated as an empty status, which would drop
	// app->indexer flags (indexing_interrupted, queue_stopped).
	case invalidIndexingStatus(String)
}

final class IpcSharedMemoryRaw {
	private static let memoryNamePrefix = "srctrl_ipc_mem_"
	private static let mutexNamePrefix = "srctrl_ipc_mtx_"

	private let size: Int
	private let shm: ShmHandle
	private let mutex: IpcMutex

	// Names pass through at full length, matching C++ IpcSharedMemory: thoth-ipc
	// maps any name exceeding the OS limit (THOTH_IPC_SHM_NAME_MAX) to a
	// hash-shortened form, identically on every frontend. (An earlier 18-char
	// truncation mirrored from C++ silently collided names sharing a prefix.)
	static func open(name: String, size: Int) async throws(IpcError) -> IpcSharedMemoryRaw {
		let memoryName = memoryNamePrefix + name
		let mutexName = mutexNamePrefix + name

		let shm = try ShmHandle.acquire(name: memoryName, size: size, mode: .createOrOpen)
		if shm.previousRefCount == 0 {
			memset(shm.ptr, 0, size)
		}

		let mutex = try await IpcMutex.open(name: mutexName)
		return IpcSharedMemoryRaw(size: size, shm: consume shm, mutex: consume mutex)
	}

	private init(size: Int, shm: consuming ShmHandle, mutex: consuming IpcMutex) {
		self.size = size
		self.shm = shm
		self.mutex = mutex
	}

	func readLocked<Result>(_ body: ([UInt8]) throws -> Result) throws -> Result {
		try mutex.lock()
		defer {
			try? mutex.unlock()
		}

		return try body(readAllBytes())
	}

	func readModifyWrite<Result>(
		_ body: ([UInt8]) throws -> (replacement: [UInt8]?, result: Result)
	) throws -> Result {
		try mutex.lock()
		defer {
			try? mutex.unlock()
		}

		let operation = try body(readAllBytes())
		if let replacement = operation.replacement {
			try writeAllBytes(replacement)
		}
		return operation.result
	}

	private func readAllBytes() -> [UInt8] {
		let pointer = shm.ptr.assumingMemoryBound(to: UInt8.self)
		let buffer = UnsafeBufferPointer(start: pointer, count: size)
		return Array(buffer)
	}

	private func writeAllBytes(_ bytes: [UInt8]) throws {
		if bytes.count > size {
			throw SwiftIndexerIpcError.sharedMemoryWriteTooLarge(requested: bytes.count, available: size)
		}
		if bytes.isEmpty {
			return
		}

		bytes.withUnsafeBytes { source in
			guard let baseAddress = source.baseAddress else {
				return
			}
			memcpy(shm.ptr, baseAddress, bytes.count)
		}
	}
}
