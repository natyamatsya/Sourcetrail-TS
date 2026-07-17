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
		let stdoutHandle = stdoutPipe.fileHandleForReading
		let stderrHandle = stderrPipe.fileHandleForReading
		let group = DispatchGroup()
		var stdoutData = Data()
		var stderrData = Data()
		group.enter()
		DispatchQueue.global().async {
			stdoutData = stdoutHandle.readDataToEndOfFile()
			group.leave()
		}
		group.enter()
		DispatchQueue.global().async {
			stderrData = stderrHandle.readDataToEndOfFile()
			group.leave()
		}

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
			stdout: String(data: stdoutData, encoding: .utf8) ?? "",
			stderr: String(data: stderrData, encoding: .utf8) ?? "",
			timedOut: timedOut
		)
	}
}
