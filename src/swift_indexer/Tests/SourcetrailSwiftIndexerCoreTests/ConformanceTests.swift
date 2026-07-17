import Foundation
import Testing

@testable import SourcetrailSwiftIndexerCore

// SW12 — protocol conformance fidelity. Conformance is INHERITANCE (protocol =
// base); a concrete member satisfying a requirement is an OVERRIDE (witness);
// a conditional conformance's `where` clause becomes constraint edges on the
// extended type's parameter. Default implementations in a protocol extension
// resolve to the requirement node itself, so their self-loop is suppressed.

@Suite struct ConformanceTests {
	private func decodeParts(_ serializedName: String) -> [String] {
		guard serializedName.hasPrefix("::\tm") else { return [] }
		return serializedName.dropFirst(4)
			.components(separatedBy: "\tn")
			.map { $0.replacingOccurrences(of: "\ts\tp", with: "") }
	}

	private func node(
		named qualified: String, in storage: OwnedIntermediateStorage
	) -> OwnedStorageNode? {
		storage.nodes.first { decodeParts($0.serializedName).joined(separator: "::") == qualified }
	}

	private func edgeTargets(
		ofType type: Int32, from sourceId: Int64, in storage: OwnedIntermediateStorage
	) -> [String] {
		storage.edges
			.filter { $0.type == type && $0.sourceNodeId == sourceId }
			.compactMap { edge in
				storage.nodes.first { $0.id == edge.targetNodeId }
					.map { decodeParts($0.serializedName).joined(separator: "::") }
			}
	}

	private func fixture() throws -> URL {
		let root = FileManager.default.temporaryDirectory
			.appendingPathComponent("sw12-\(UUID().uuidString)")
		let sources = root.appendingPathComponent("Sources/Demo")
		try FileManager.default.createDirectory(at: sources, withIntermediateDirectories: true)
		try """
			// swift-tools-version: 6.0
			import PackageDescription
			let package = Package(
				name: "demo",
				targets: [.target(name: "Demo", path: "Sources/Demo")]
			)
			""".write(to: root.appendingPathComponent("Package.swift"), atomically: true, encoding: .utf8)
		try #"""
			public protocol Greeter {
				func greet() -> String
				var name: String { get }
			}

			extension Greeter {
				public func greet() -> String { "hi from \(name)" }
			}

			public struct Robot: Greeter {
				public let name: String
			}

			public struct Human: Greeter {
				public let name: String
				public func greet() -> String { "hello, I am \(name)" }
			}

			public struct Pair<T> {
				public let first: T
				public let second: T
			}
			extension Pair: Greeter where T: CustomStringConvertible {
				public var name: String { "\(first)" }
			}
			"""#.write(to: sources.appendingPathComponent("Conf.swift"), atomically: true, encoding: .utf8)
		return root
	}

	@Test func conformanceAndWitnessAndConditionalConstraints() throws {
		let root = try fixture()
		defer { try? FileManager.default.removeItem(at: root) }

		let storage = PackageIndexer.index(workingDirectory: root.path) { _ in }
		#expect(storage.errors.isEmpty)

		// Conformance: type -> protocol as INHERITANCE (incl. the conditional one).
		let human = try #require(node(named: "Demo::Human", in: storage))
		let pair = try #require(node(named: "Demo::Pair", in: storage))
		#expect(edgeTargets(ofType: EdgeKind.inheritance, from: human.id, in: storage)
			.contains("Demo::Greeter"))
		#expect(edgeTargets(ofType: EdgeKind.inheritance, from: pair.id, in: storage)
			.contains("Demo::Greeter"))

		// Witness edges: a concrete member -> the requirement it satisfies (OVERRIDE),
		// for an explicit method and for a property fulfilled by a stored property.
		let humanGreet = try #require(node(named: "Demo::Human::greet()", in: storage))
		let robotName = try #require(node(named: "Demo::Robot::name", in: storage))
		#expect(edgeTargets(ofType: EdgeKind.override_, from: humanGreet.id, in: storage)
			.contains("Demo::Greeter::greet()"))
		#expect(edgeTargets(ofType: EdgeKind.override_, from: robotName.id, in: storage)
			.contains("Demo::Greeter::name"))

		// SW12: the conditional conformance's `where T: CustomStringConvertible`
		// becomes an INHERITANCE bound on the extended type's parameter.
		let t = try #require(node(named: "Demo::Pair::T", in: storage))
		#expect(edgeTargets(ofType: EdgeKind.inheritance, from: t.id, in: storage)
			.contains { $0.hasSuffix("CustomStringConvertible") })

		// The default implementation in `extension Greeter` resolves to the
		// requirement node — no self-loop override edge.
		#expect(storage.edges.allSatisfy {
			!($0.type == EdgeKind.override_ && $0.sourceNodeId == $0.targetNodeId)
		})
	}
}
