// swift-tools-version: 6.0
import PackageDescription

let package = Package(
	name: "sourcetrail-swift-indexer",
	platforms: [
		.macOS(.v14),
	],
	products: [
		.executable(name: "sourcetrail_swift_indexer", targets: ["SourcetrailSwiftIndexer"]),
	],
	dependencies: [
		.package(path: "../../submodules/thoth-ipc/swift/libipc"),
		// The same FlatBuffers checkout libipc vendors — a URL dependency here
		// would clash with it on package identity (SwiftPM warns, soon errors)
		// and drift from the runtime the IPC layer is built against.
		.package(path: "../../submodules/thoth-ipc/swift/libipc/vendor/flatbuffers"),
	],
	targets: [
		.target(
			name: "SourcetrailSwiftIndexerCore",
			dependencies: [
				.product(name: "LibIPC", package: "libipc"),
				.product(name: "FlatBuffers", package: "flatbuffers"),
			],
			path: "Sources/SourcetrailSwiftIndexerCore"
		),
		.executableTarget(
			name: "SourcetrailSwiftIndexer",
			dependencies: ["SourcetrailSwiftIndexerCore"],
			path: "Sources/SourcetrailSwiftIndexer"
		),
		.testTarget(
			name: "SourcetrailSwiftIndexerCoreTests",
			dependencies: ["SourcetrailSwiftIndexerCore"],
			path: "Tests/SourcetrailSwiftIndexerCoreTests"
		),
	]
)
