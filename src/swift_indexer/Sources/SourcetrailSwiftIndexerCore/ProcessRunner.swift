import Foundation

struct ProcessOutput {
	let exitCode: Int32
	let stdout: String
	let stderr: String
	let timedOut: Bool
}

enum ProcessRunner {
	// Run `executable` with `arguments` in `currentDirectory`, capturing both
	// streams. A nil timeout waits indefinitely (builds); with a timeout the
	// process is killed and `timedOut` set.
	static func run(
		executable: String,
		arguments: [String],
		currentDirectory: URL,
		timeout: TimeInterval? = nil
	) throws -> ProcessOutput {
		let process = Process()
		process.executableURL = URL(fileURLWithPath: executable)
		process.arguments = arguments
		process.currentDirectoryURL = currentDirectory

		let stdoutPipe = Pipe()
		let stderrPipe = Pipe()
		process.standardOutput = stdoutPipe
		process.standardError = stderrPipe

		try process.run()

		// Drain concurrently: a pipe buffer filling up would deadlock the
		// child before termination.
		//
		// The readers run on dedicated Threads, not on DispatchQueue.global().
		// This caller blocks in `group.wait()` below until they finish, so
		// scheduling them onto a width-limited pool makes the drain depend on a
		// thread the waiters may already occupy. Under a concurrent test runner
		// that deadlocks for real rather than in theory: swift-testing runs
		// cases in parallel, thirteen of them sat in `group.wait()` at once,
		// and with every pool thread held by a waiter not one of the queued
		// drains could start. A Thread is always schedulable, so a drain can
		// never be starved by the wait that depends on it.
		let stdoutHandle = stdoutPipe.fileHandleForReading
		let stderrHandle = stderrPipe.fileHandleForReading
		let group = DispatchGroup()
		let stdoutBox = DrainBox()
		let stderrBox = DrainBox()
		Self.drain(stdoutHandle, into: stdoutBox, group: group)
		Self.drain(stderrHandle, into: stderrBox, group: group)

		var timedOut = false
		if let timeout {
			let deadline = Date(timeIntervalSinceNow: timeout)
			while process.isRunning && Date() < deadline {
				Thread.sleep(forTimeInterval: 0.05)
			}
			if process.isRunning {
				timedOut = true
				process.terminate()
			}
		}
		process.waitUntilExit()
		group.wait()

		return ProcessOutput(
			exitCode: process.terminationStatus,
			stdout: String(data: stdoutBox.take(), encoding: .utf8) ?? "",
			stderr: String(data: stderrBox.take(), encoding: .utf8) ?? "",
			timedOut: timedOut
		)
	}

	// Reads `handle` to EOF on a thread of its own, parking the result in `box`
	// and leaving `group` when done.
	private static func drain(_ handle: FileHandle, into box: DrainBox, group: DispatchGroup) {
		group.enter()
		let thread = Thread {
			box.put(handle.readDataToEndOfFile())
			group.leave()
		}
		thread.name = "ProcessRunner.drain"
		// The default 512 KiB is ample for a read loop, and keeps the cost of a
		// thread per stream small enough that concurrent callers stay cheap.
		thread.stackSize = 512 * 1024
		thread.start()
	}
}

// Hands one stream's bytes from its drain thread back to the waiter. The
// DispatchGroup is the ordering guarantee -- `take()` is only ever called after
// `group.wait()` returns, so the lock guards the handoff rather than any
// contended access.
private final class DrainBox: @unchecked Sendable {
	private let lock = NSLock()
	private var data = Data()

	func put(_ value: Data) {
		lock.lock()
		defer { lock.unlock() }
		data = value
	}

	func take() -> Data {
		lock.lock()
		defer { lock.unlock() }
		return data
	}
}
