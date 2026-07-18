import Foundation
import SourcetrailSwiftIndexerCore

@main
struct SourcetrailSwiftIndexer {
	private static func writeStderr(_ message: String) {
		if let data = message.data(using: .utf8) {
			FileHandle.standardError.write(data)
		}
	}

	// Status-channel reads/writes must never fail silently: a swallowed error once
	// hid a malformed IndexingStatus and made the indexer miss a user interrupt.
	// These surface the failure on stderr (captured by the C++ supervisor's log).

	/// Read the interrupt flag; on a read/verification failure log it and report
	/// "not interrupted" — the run continues (the supervisor still stops it via
	/// queue drain / kill), but the fault is no longer invisible.
	private static func isInterrupted(_ statusChannel: SwiftIndexerStatusChannel) -> Bool {
		do {
			return try statusChannel.isInterrupted()
		} catch {
			writeStderr("sourcetrail_swift_indexer: reading interrupt status failed: \(error)\n")
			return false
		}
	}

	/// Run a best-effort status update (progress reporting), logging any failure
	/// instead of dropping it silently.
	private static func reportingStatus(_ label: String, _ op: () throws -> Void) {
		do {
			try op()
		} catch {
			writeStderr("sourcetrail_swift_indexer: \(label) failed: \(error)\n")
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
				if Self.isInterrupted(statusChannel) {
					break
				}

				guard let command = try commandChannel.popSwiftCommand() else {
					// Queue empty: drain-and-exit, matching the Rust indexer
					// (main.rs) — SW4 queue-exit alignment. The C++ supervisor
					// (TaskBuildIndex::runExternalIndexerProcess) owns the
					// queue-stopped decision: it relaunches this process while
					// commands remain and stops once the queue is stopped, so the
					// indexer must not poll queueStopped itself (doing so hangs
					// until the supervisor's kill when the flag is never observed).
					break
				}

				do {
					Self.reportingStatus("startIndexing") {
						try statusChannel.startIndexing(filePath: command.sourceFilePath)
					}
					defer {
						Self.reportingStatus("finishIndexing") { try statusChannel.finishIndexing() }
					}

					let storage = PackageIndexer.index(
						workingDirectory: command.workingDirectory,
						options: SwiftBuildOptions(
							buildArgs: command.buildArgs,
							toolchainPath: command.toolchainPath,
							indexStorePath: command.indexStorePath
						),
						specializationScope: SpecializationScope.parse(command.specializationScope)
					) { filePath in
						Self.reportingStatus("updateIndexing") {
							try statusChannel.updateIndexing(filePath: filePath)
						}
					}

					// Split large results so one queue entry never outgrows the
					// fixed 16 MiB SHM segment (ADR-0002); the app merges the
					// chunks via PersistentStorage inject. Back-pressure applies
					// between pushes.
					var interruptedDuringPush = false
					for chunk in StorageChunker.chunks(storage) {
						while ((try? storageChannel.storageCount()) ?? 0) >= 2 {
							if Self.isInterrupted(statusChannel) {
								interruptedDuringPush = true
								break
							}
							try? await Task.sleep(nanoseconds: 200_000_000)
						}
						if interruptedDuringPush {
							break
						}
						try storageChannel.push(storage: chunk)
					}
					if interruptedDuringPush {
						return
					}
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
