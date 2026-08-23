import Foundation
import Testing

@testable import SourcetrailSwiftIndexerCore

// The channel-mediated boundary (context/DESIGN_XLANG_BOUNDARIES.md): an IPC
// channel is named by a string constant that four languages must spell
// identically, and nothing in the graph related the copies. A constant whose
// literal starts with a project-declared prefix binds to a `channel:` atom,
// keyed by the literal — the only thing the four declarations share.

@Suite struct ChannelBoundaryTests {
	private static let prefixes = ["srctrl_ipc_"]

	private func atomNames(in storage: OwnedIntermediateStorage) -> [String] {
		storage.nodes.compactMap { $0.serializedName }.filter { $0.hasPrefix("channel\tm") }
	}

	private func index(_ source: String, prefixes: [String]) throws -> OwnedIntermediateStorage {
		let tmp = FileManager.default.temporaryDirectory
			.appendingPathComponent("channel-\(UUID().uuidString).swift")
		defer { try? FileManager.default.removeItem(at: tmp) }
		try source.write(to: tmp, atomically: true, encoding: .utf8)

		let builder = StorageBuilder()
		SyntacticIndexer.indexFile(
			path: tmp.path, moduleName: "M", builder: builder, channelNamePrefixes: prefixes)
		return builder.storage
	}

	@Test func channelNameConstantBindsToChannelAtom() throws {
		let storage = try index(
			"""
			enum Shm {
				private static let memoryNamePrefix = "srctrl_ipc_mem_"
				private static let mutexNamePrefix = "srctrl_ipc_mtx_"
				private static let unrelated = "some other string"
				private static let notAString = 7
			}
			""",
			prefixes: Self.prefixes)

		let atoms = atomNames(in: storage)
		#expect(atoms.sorted() == [
			"channel\tmsrctrl_ipc_mem_\ts\tp",
			"channel\tmsrctrl_ipc_mtx_\ts\tp",
		])

		// The edge runs declaration -> atom, the direction every producer uses,
		// so an atom's incoming edges answer "who speaks this channel".
		let atomIds = Set(
			storage.nodes.filter { ($0.serializedName ?? "").hasPrefix("channel\tm") }.map { $0.id })
		let binds = storage.edges.filter {
			$0.type == EdgeKind.binds && atomIds.contains($0.targetNodeId)
		}
		#expect(binds.count == 2)
	}

	@Test func interpolatedLiteralIsNotAChannelName() throws {
		// An interpolated literal is not a fixed name, so it cannot be the thing
		// another language spells identically.
		let storage = try index(
			"""
			let suffix = "mem_"
			let interpolated = "srctrl_ipc_\\(suffix)"
			""",
			prefixes: Self.prefixes)
		#expect(atomNames(in: storage).isEmpty)
	}

	@Test func noChannelAtomsWithoutDeclaredPrefixes() throws {
		// The feature is off by default: a project that declares no prefixes gets
		// exactly the index it got before this existed.
		let storage = try index(
			#"let memoryNamePrefix = "srctrl_ipc_mem_""#,
			prefixes: [])
		#expect(atomNames(in: storage).isEmpty)
	}
}
