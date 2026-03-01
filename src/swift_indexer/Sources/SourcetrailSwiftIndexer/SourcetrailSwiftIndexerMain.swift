import Foundation

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
			let commandChannel = try await SwiftIndexerCommandChannel.open(instanceUuid: instanceUuid)
			let statusChannel = try await SwiftIndexerStatusChannel.open(
				instanceUuid: instanceUuid,
				processId: processId
			)
			let storageChannel = try await SwiftIndexerStorageChannel.open(
				instanceUuid: instanceUuid,
				processId: processId
			)

			while true {
				if (try? statusChannel.isInterrupted()) == true {
					break
				}

				guard let command = try commandChannel.popSwiftCommand() else {
					if (try? statusChannel.isQueueStopped()) == true {
						break
					}
					try? await Task.sleep(nanoseconds: 100_000_000)
					continue
				}

				while true {
					let currentStorageCount = (try? storageChannel.storageCount()) ?? 0
					if currentStorageCount < 2 {
						break
					}

					if (try? statusChannel.isInterrupted()) == true {
						return
					}

					try? await Task.sleep(nanoseconds: 200_000_000)
				}

				do {
					try? statusChannel.startIndexing(filePath: command.sourceFilePath)
					defer {
						try? statusChannel.finishIndexing()
					}

					try storageChannel.pushEmptyStorage()
				} catch {
					writeStderr(
						"sourcetrail_swift_indexer: failed to process command for \(command.sourceFilePath): \(error)\n"
					)
				}
			}
		} catch {
			writeStderr("sourcetrail_swift_indexer: IPC bootstrap failed: \(error)\n")
			exit(1)
		}

		print("sourcetrail_swift_indexer shutting down")
	}
}
