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
		// Semantic engine (SW2): reads the compiler-produced index store.
		// No semver releases upstream — pinned by exact revision (main as of
		// 2026-07-17, builds with Swift 6.4); bump alongside toolchain updates.
		.package(
			url: "https://github.com/swiftlang/indexstore-db.git",
			revision: "c993f4fb4f321fae1945e96a2377742f24e132f4"
		),
		// Syntactic fallback engine (SW3): declaration structure for files
		// without an up-to-date index unit (broken/partial builds).
		.package(url: "https://github.com/swiftlang/swift-syntax.git", exact: "602.0.0"),
	],
	targets: [
		.target(
			name: "SourcetrailSwiftIndexerCore",
			dependencies: [
				.product(name: "LibIPC", package: "libipc"),
				.product(name: "FlatBuffers", package: "flatbuffers"),
				.product(name: "IndexStoreDB", package: "indexstore-db"),
				.product(name: "SwiftSyntax", package: "swift-syntax"),
				.product(name: "SwiftParser", package: "swift-syntax"),
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
