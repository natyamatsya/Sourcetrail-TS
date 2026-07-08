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
		.package(url: "https://github.com/google/flatbuffers.git", from: "25.2.10"),
	],
	targets: [
		.executableTarget(
			name: "SourcetrailSwiftIndexer",
			dependencies: [
				.product(name: "LibIPC", package: "libipc"),
				.product(name: "FlatBuffers", package: "flatbuffers"),
			],
			path: "Sources/SourcetrailSwiftIndexer"
		),
	]
)
