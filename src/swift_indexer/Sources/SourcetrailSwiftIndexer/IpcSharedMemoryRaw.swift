import Darwin.POSIX
import LibIPC

enum SwiftIndexerIpcError: Error {
	case sharedMemoryWriteTooLarge(requested: Int, available: Int)
	case invalidStorageQueue(String)
}

final class IpcSharedMemoryRaw {
	private static let memoryNamePrefix = "srctrl_ipc_mem_"
	private static let mutexNamePrefix = "srctrl_ipc_mtx_"
	private static let maxLogicalNameLength = 18

	private let size: Int
	private let shm: ShmHandle
	private let mutex: IpcMutex

	static func open(name: String, size: Int) async throws(IpcError) -> IpcSharedMemoryRaw {
		let logicalName = truncatedName(name)
		let memoryName = memoryNamePrefix + logicalName
		let mutexName = mutexNamePrefix + logicalName

		let shm = try ShmHandle.acquire(name: memoryName, size: size, mode: .createOrOpen)
		if shm.previousRefCount == 0 {
			memset(shm.ptr, 0, size)
		}

		let mutex = try await IpcMutex.open(name: mutexName)
		return IpcSharedMemoryRaw(size: size, shm: consume shm, mutex: consume mutex)
	}

	private static func truncatedName(_ name: String) -> String {
		let bytes = Array(name.utf8)
		if bytes.count <= maxLogicalNameLength {
			return name
		}
		return String(decoding: bytes.prefix(maxLogicalNameLength), as: UTF8.self)
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
