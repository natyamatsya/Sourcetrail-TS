import Foundation
import LibIPC

@main
struct SourcetrailSwiftIndexer {
	private static func writeStderr(_ message: String) {
		if let data = message.data(using: .utf8) {
			FileHandle.standardError.write(data)
		}
	}

	static func main() async {
		let args = CommandLine.arguments
		let processId = args.count > 1 ? (UInt64(args[1]) ?? 0) : 0
		let instanceUuid = args.count > 2 ? args[2] : ""
		let appPath = args.count > 3 ? args[3] : ""
		let userDataPath = args.count > 4 ? args[4] : ""
		let logFilePath = args.count > 5 ? args[5] : ""

		guard !instanceUuid.isEmpty else {
			writeStderr("sourcetrail_swift_indexer: missing instanceUuid argument\n")
			exit(1)
		}

		print("sourcetrail_swift_indexer starting process_id=\(processId) uuid=\(instanceUuid)")
		_ = appPath
		_ = userDataPath
		_ = logFilePath

		do {
			let probeMutexName = "swift_indexer_probe_\(instanceUuid)_\(processId)"
			let probeMutex = try await IpcMutex.open(name: probeMutexName)
			try probeMutex.lock()
			try probeMutex.unlock()
		} catch {
			writeStderr("sourcetrail_swift_indexer: IPC bootstrap failed: \(error)\n")
			exit(1)
		}

		print("sourcetrail_swift_indexer initialized (command processing to be implemented)")
	}
}
