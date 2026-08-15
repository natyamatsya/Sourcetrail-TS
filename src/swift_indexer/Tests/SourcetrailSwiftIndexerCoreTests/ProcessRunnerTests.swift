import Foundation
import Testing

@testable import SourcetrailSwiftIndexerCore

// ProcessRunner's contract is that a caller may block on it, from as many
// threads at once as it likes, without the drains it depends on being starved
// of a thread to run on. Both tests here failed as hangs rather than as
// assertion failures before the drains moved off DispatchQueue.global(), and a
// regression would present the same way: a suite that stops making progress
// instead of one that reports.

@Suite struct ProcessRunnerTests {
	// The shape that deadlocked the whole suite: many synchronous callers
	// blocking pool threads while the work unblocking them sits queued behind
	// those same threads. Oversubscribed deliberately -- the bug needed every
	// pool thread taken before it appeared, so a count at or below the core
	// count would let a broken implementation pass.
	@Test func concurrentCallersAllCompleteWithoutStarvingTheDrains() async throws {
		let count = max(16, ProcessInfo.processInfo.activeProcessorCount * 2)
		let directory = URL(fileURLWithPath: NSTemporaryDirectory())

		let outputs = await withTaskGroup(of: (Int, String)?.self) { group in
			for index in 0..<count {
				group.addTask {
					// Synchronous on purpose: this is how BuildDriver calls it,
					// and blocking the calling thread is the precondition for
					// the deadlock being tested.
					guard
						let output = try? ProcessRunner.run(
							executable: "/bin/echo",
							arguments: ["marker-\(index)"],
							currentDirectory: directory
						)
					else {
						return nil
					}
					return (index, output.stdout.trimmingCharacters(in: .whitespacesAndNewlines))
				}
			}
			var collected: [Int: String] = [:]
			for await result in group {
				if let result {
					collected[result.0] = result.1
				}
			}
			return collected
		}

		#expect(outputs.count == count)
		// Each caller got its own child's output, not another's -- the boxes
		// are per-call and nothing is shared across the drain threads.
		for index in 0..<count {
			#expect(outputs[index] == "marker-\(index)")
		}
	}

	// The reason the drains are concurrent at all: a child that writes more
	// than the pipe buffer holds blocks in write() until someone reads, so a
	// runner that drained only after waitUntilExit() would hang here.
	@Test func outputLargerThanThePipeBufferIsDrainedNotDeadlocked() throws {
		let output = try ProcessRunner.run(
			executable: "/usr/bin/seq",
			arguments: ["1", "200000"],
			currentDirectory: URL(fileURLWithPath: NSTemporaryDirectory())
		)

		#expect(output.exitCode == 0)
		#expect(output.stdout.utf8.count > 1_000_000)
		#expect(output.stdout.hasPrefix("1\n2\n"))
		#expect(output.stdout.hasSuffix("200000\n"))
	}
}
