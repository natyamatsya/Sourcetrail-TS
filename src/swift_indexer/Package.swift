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
		.package(path: "../../inspiration/cpp-ipc/swift/libipc"),
	],
	targets: [
		.executableTarget(
			name: "SourcetrailSwiftIndexer",
			dependencies: [
				.product(name: "LibIPC", package: "libipc"),
			],
			path: "Sources/SourcetrailSwiftIndexer"
		),
	]
)
