import Foundation
import IndexStoreDB

// Semantic pass (SW2): read the compiler-produced index store via
// IndexStoreDB and emit nodes/edges/occurrences. Only files whose newest
// index unit is at least as new as the source are covered here — stale units
// carry wrong occurrence offsets, so those files fall through to the
// syntactic pass (SW3).

final class SemanticIndexer {
	private let index: IndexStoreDB
	private let builder: StorageBuilder
	/// USR → resolved name-hierarchy parts + node kind (memoized; includes
	/// negative results as module-qualified flat-name fallbacks).
	private var resolvedSymbols: [String: (parts: [String], kind: Int32)] = [:]
	/// USRs currently being resolved (cycle guard for malformed stores).
	private var resolving: Set<String> = []

	init(storePath: URL, databasePath: URL, builder: StorageBuilder) throws {
		let library = try IndexStoreLibrary(dylibPath: Toolchain.libIndexStorePath())
		self.index = try IndexStoreDB(
			storePath: storePath.path,
			databasePath: databasePath.path,
			library: library,
			waitUntilDoneInitializing: true,
			listenToUnitEvents: false
		)
		index.pollForUnitChangesAndWait(isInitialScan: true)
		self.builder = builder
	}

	/// The subset of `sourceFiles` with an up-to-date index unit.
	func coveredFiles(of sourceFiles: [String]) -> [String] {
		sourceFiles.filter { path in
			guard let unitDate = index.dateOfLatestUnitFor(filePath: path) else {
				return false
			}
			let sourceDate =
				(try? FileManager.default.attributesOfItem(atPath: path)[.modificationDate]
					as? Date) ?? Date.distantPast
			return unitDate >= (sourceDate ?? Date.distantPast)
		}
	}

	func indexFile(path: String) {
		let fileNodeId = builder.fileNodeId(path: path, complete: true)
		for occurrence in index.symbolOccurrences(inFilePath: path) {
			process(occurrence: occurrence, filePath: path, fileNodeId: fileNodeId)
		}
	}

	// -----------------------------------------------------------------------

	private func process(occurrence: SymbolOccurrence, filePath: String, fileNodeId: Int64) {
		let symbol = occurrence.symbol
		guard symbol.language == .swift, !occurrence.location.isSystem else {
			return
		}
		// Parameters and accessors add noise without graph value.
		if symbol.kind == .parameter || occurrence.roles.contains(.accessorOf) {
			return
		}

		if occurrence.roles.contains(.definition) || occurrence.roles.contains(.declaration) {
			processDefinition(occurrence, fileNodeId: fileNodeId)
		} else if occurrence.roles.contains(.reference) {
			processReference(occurrence, fileNodeId: fileNodeId)
		}
	}

	private func processDefinition(_ occurrence: SymbolOccurrence, fileNodeId: Int64) {
		let symbol = occurrence.symbol
		guard let (parts, kind) = resolve(symbol: symbol, location: occurrence.location) else {
			return
		}
		let nodeId = builder.nodeId(parts: parts, kind: kind)
		builder.recordSymbol(
			nodeId: nodeId,
			definitionKind: occurrence.roles.contains(.implicit)
				? DefinitionKind.implicit : DefinitionKind.explicit
		)
		recordTokenOccurrence(
			elementId: nodeId, occurrence: occurrence, fileNodeId: fileNodeId)

		// Structure edges: module ⟶ top-level, parent ⟶ child. The parent node
		// is a placeholder kind if it has not been defined yet; its own
		// definition upgrades the kind on emission.
		if parts.count > 1 {
			let parentParts = Array(parts.dropLast())
			let parentKind = parentParts.count == 1 ? NodeKind.module : NodeKind.symbol
			let parentId = builder.nodeId(parts: parentParts, kind: parentKind)
			_ = builder.edgeId(type: EdgeKind.member, source: parentId, target: nodeId)
		}

		// Override edges hang off the overriding definition's relations.
		for relation in occurrence.relations where relation.roles.contains(.overrideOf) {
			if let (targetParts, targetKind) = resolve(symbol: relation.symbol, location: nil) {
				let targetId = builder.nodeId(parts: targetParts, kind: targetKind)
				_ = builder.edgeId(type: EdgeKind.override_, source: nodeId, target: targetId)
			}
		}
	}

	private func processReference(_ occurrence: SymbolOccurrence, fileNodeId: Int64) {
		let symbol = occurrence.symbol
		guard let (targetParts, targetKind) = resolve(symbol: symbol, location: occurrence.location)
		else {
			return
		}
		let targetId = builder.nodeId(parts: targetParts, kind: targetKind)

		// The containing symbol comes from the occurrence's relations.
		var sourceId: Int64?
		var isBaseOf = false
		var isCall = occurrence.roles.contains(.call)
		for relation in occurrence.relations {
			if relation.roles.contains(.baseOf) {
				// `symbol` appears as base in the definition of relation.symbol.
				isBaseOf = true
				if let (parts, kind) = resolve(symbol: relation.symbol, location: nil) {
					sourceId = builder.nodeId(parts: parts, kind: kind)
				}
				break
			}
			if relation.roles.contains(.calledBy) || relation.roles.contains(.containedBy) {
				isCall = isCall || relation.roles.contains(.calledBy)
				if let (parts, kind) = resolve(symbol: relation.symbol, location: nil) {
					sourceId = builder.nodeId(parts: parts, kind: kind)
				}
			}
		}

		let edgeType: Int32
		if isBaseOf {
			edgeType = EdgeKind.inheritance
		} else if isCall {
			edgeType = EdgeKind.call
		} else if symbol.kind == .module {
			edgeType = EdgeKind.import_
		} else if isTypeKind(symbol.kind) {
			edgeType = EdgeKind.typeUsage
		} else {
			edgeType = EdgeKind.usage
		}

		let elementId: Int64
		if let sourceId {
			elementId = builder.edgeId(type: edgeType, source: sourceId, target: targetId)
		} else {
			// No resolvable context (e.g. top-level script code): record the
			// occurrence against the referenced node itself.
			elementId = targetId
		}
		recordTokenOccurrence(
			elementId: elementId, occurrence: occurrence, fileNodeId: fileNodeId)
	}

	private func recordTokenOccurrence(
		elementId: Int64, occurrence: SymbolOccurrence, fileNodeId: Int64
	) {
		let line = UInt32(max(occurrence.location.line, 1))
		let startCol = UInt32(max(occurrence.location.utf8Column, 1))
		// The store has no end position; the display name's base identifier
		// length is the best available token extent.
		let tokenLength = UInt32(max(baseIdentifier(of: occurrence.symbol.name).count, 1))
		builder.recordOccurrence(
			elementId: elementId,
			fileNodeId: fileNodeId,
			startLine: line,
			startCol: startCol,
			endLine: line,
			endCol: startCol + tokenLength - 1,
			locationType: LocationKind.token
		)
	}

	// `foo(bar:baz:)` → `foo`; `subscript(_:)` → `subscript`.
	private func baseIdentifier(of name: String) -> String {
		if let parenIndex = name.firstIndex(of: "(") {
			return String(name[name.startIndex..<parenIndex])
		}
		return name
	}

	// -----------------------------------------------------------------------
	// USR → name hierarchy

	/// Resolve a symbol to (name parts, node kind). Parts start with the
	/// module name. Parents resolve recursively via the childOf relation on
	/// the parent's canonical definition; extensions redirect to the extended
	/// type. Unresolvable parents degrade to module-qualified flat names.
	private func resolve(
		symbol: Symbol, location: SymbolLocation?
	) -> (parts: [String], kind: Int32)? {
		if let cached = resolvedSymbols[symbol.usr] {
			return cached
		}
		if symbol.kind == .module {
			let result = ([symbol.name], NodeKind.module)
			resolvedSymbols[symbol.usr] = result
			return result
		}
		guard resolving.insert(symbol.usr).inserted else {
			return nil  // cycle in a malformed store
		}
		defer { resolving.remove(symbol.usr) }

		let definition = index.occurrences(ofUSR: symbol.usr, roles: [.definition, .declaration])
			.first { !$0.location.isSystem }
		let moduleName = definition?.location.moduleName ?? location?.moduleName ?? ""

		var parentParts: [String]
		if let parent = definition?.relations.first(where: { $0.roles.contains(.childOf) })?.symbol {
			if parent.kind == .extension {
				parentParts = resolveExtension(parent)
					?? (moduleName.isEmpty ? [] : [moduleName])
			} else {
				parentParts = resolve(symbol: parent, location: nil)?.parts
					?? (moduleName.isEmpty ? [] : [moduleName])
			}
		} else {
			parentParts = moduleName.isEmpty ? [] : [moduleName]
		}
		if parentParts.isEmpty {
			parentParts = ["<unknown>"]
		}

		let ownName = symbol.name.isEmpty ? "<anonymous>" : symbol.name
		let result = (parentParts + [ownName], nodeKind(for: symbol))
		resolvedSymbols[symbol.usr] = result
		return result
	}

	/// Extension USR → the extended type's parts: the extended type occurs
	/// with an `extendedBy` relation naming the extension.
	private func resolveExtension(_ extensionSymbol: Symbol) -> [String]? {
		let related = index.occurrences(relatedToUSR: extensionSymbol.usr, roles: .extendedBy)
		guard let extendedType = related.first?.symbol else {
			return nil
		}
		return resolve(symbol: extendedType, location: related.first?.location)?.parts
	}

	private func isTypeKind(_ kind: IndexSymbolKind) -> Bool {
		switch kind {
		case .enum, .struct, .class, .protocol, .union, .typealias, .extension:
			return true
		default:
			return false
		}
	}

	private func nodeKind(for symbol: Symbol) -> Int32 {
		switch symbol.kind {
		case .module: return NodeKind.module
		case .enum: return NodeKind.enum
		case .struct: return NodeKind.struct
		case .class: return NodeKind.class  // actors surface as class too
		case .protocol: return NodeKind.interface
		case .union: return NodeKind.union
		case .typealias: return NodeKind.typedef
		case .function, .conversionFunction: return NodeKind.function
		case .variable: return NodeKind.globalVariable
		case .field, .instanceProperty, .classProperty, .staticProperty: return NodeKind.field
		case .enumConstant: return NodeKind.enumConstant
		case .instanceMethod, .classMethod, .staticMethod, .constructor, .destructor:
			return NodeKind.method
		case .macro: return NodeKind.macro
		case .namespace, .namespaceAlias: return NodeKind.module
		case .using, .concept, .commentTag, .parameter, .extension, .unknown:
			return NodeKind.symbol
		}
	}
}

enum Toolchain {
	/// libIndexStore.dylib of the active toolchain (what the compiler that
	/// produced the store links against).
	static func libIndexStorePath() throws -> String {
		let output = try ProcessRunner.run(
			executable: "/usr/bin/xcrun",
			arguments: ["--find", "swiftc"],
			currentDirectory: URL(fileURLWithPath: "/"),
			timeout: 30
		)
		let swiftcPath = output.stdout.trimmingCharacters(in: .whitespacesAndNewlines)
		guard !swiftcPath.isEmpty else {
			throw SwiftIndexerIpcError.invalidStorageQueue("xcrun --find swiftc failed")
		}
		// <toolchain>/usr/bin/swiftc → <toolchain>/usr/lib/libIndexStore.dylib
		return URL(fileURLWithPath: swiftcPath)
			.deletingLastPathComponent()
			.deletingLastPathComponent()
			.appendingPathComponent("lib/libIndexStore.dylib")
			.path
	}
}
