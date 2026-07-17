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
	/// SW10: per-file SwiftSyntax facts (exact name extents + declaration scope
	/// extents), used to enrich the store's positional occurrences. Set per file.
	private var scopeMap: DeclScopeMap?
	/// SW11: per-file generic parameters + constraints (syntactic). Set per file.
	private var genericMap: GenericParamMap?
	/// SW11: resolved name parts of each definition, keyed by its name-token
	/// position — the owner lookup for `emitGenerics`. Reset per file.
	private var defPartsByPos: [SyntaxPos: [String]] = [:]
	/// SW11: resolved target of each constraint reference, keyed by position.
	/// Reset per file.
	private var refTargetByPos: [SyntaxPos: (parts: [String], kind: Int32)] = [:]
	/// SW11 (type arguments): whether/where generic use sites emit
	/// EDGE_TYPE_ARGUMENT.
	private let specializationScope: SpecializationScope
	/// SW11: per-file type-argument sites (arg pos → base pos). Nil when the scope
	/// is `off`. Set per file.
	private var genericArgMap: GenericArgMap?
	/// SW11: symbol at each occurrence position in the current file — the base
	/// type of a `Base<Arg>` application, for the `local` locality check. Reset
	/// per file.
	private var symbolByPos: [SyntaxPos: Symbol] = [:]
	/// SW11: memoized base-type locality (non-system definition) by USR.
	private var localityByUsr: [String: Bool] = [:]

	init(
		storePath: URL,
		databasePath: URL,
		builder: StorageBuilder,
		toolchainPath: String = "",
		specializationScope: SpecializationScope = .local
	) throws {
		let library = try IndexStoreLibrary(
			dylibPath: Toolchain.libIndexStorePath(toolchainPath: toolchainPath))
		self.index = try IndexStoreDB(
			storePath: storePath.path,
			databasePath: databasePath.path,
			library: library,
			waitUntilDoneInitializing: true,
			listenToUnitEvents: false
		)
		index.pollForUnitChangesAndWait(isInitialScan: true)
		self.builder = builder
		self.specializationScope = specializationScope
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
		// SW10/SW11: parse the file once for exact name/scope extents and generic
		// facts. Semantic coverage means the file compiled, so it parses too.
		scopeMap = DeclScopeMap.build(path: path)
		genericMap = GenericParamMap.build(path: path)
		genericArgMap = specializationScope == .off ? nil : GenericArgMap.build(path: path)
		defPartsByPos.removeAll(keepingCapacity: true)
		refTargetByPos.removeAll(keepingCapacity: true)
		symbolByPos.removeAll(keepingCapacity: true)
		defer {
			scopeMap = nil
			genericMap = nil
			genericArgMap = nil
		}
		let occurrences = index.symbolOccurrences(inFilePath: path)
		// SW11: index the file's occurrences by position so a type argument can
		// look up the symbol of its generic base for the `local` scope gate.
		if genericArgMap != nil {
			for occurrence in occurrences {
				symbolByPos[positionOf(occurrence.location)] = occurrence.symbol
			}
		}
		for occurrence in occurrences {
			process(occurrence: occurrence, filePath: path, fileNodeId: fileNodeId)
		}
		// SW11: with all definitions and constraint targets resolved for this
		// file, materialize the generic-parameter tier and its bound edges.
		emitGenerics(fileNodeId: fileNodeId)
		// SW12: conditional-conformance constraints on extensions.
		emitExtensionConstraints(fileNodeId: fileNodeId)
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
		// SW11: remember where this definition landed so emitGenerics can find the
		// owner's parts when attaching its type parameters.
		defPartsByPos[positionOf(occurrence.location)] = parts
		recordDefinitionLocations(
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

		// Override edges hang off the overriding definition's relations. This also
		// carries protocol-requirement satisfaction (a witness overrides the
		// requirement). SW12: a default implementation in a protocol extension
		// resolves to the same node as the requirement it satisfies, so skip the
		// resulting self-loop.
		for relation in occurrence.relations where relation.roles.contains(.overrideOf) {
			if let (targetParts, targetKind) = resolve(symbol: relation.symbol, location: nil) {
				let targetId = builder.nodeId(parts: targetParts, kind: targetKind)
				if targetId != nodeId {
					_ = builder.edgeId(type: EdgeKind.override_, source: nodeId, target: targetId)
				}
			}
		}
	}

	private func processReference(_ occurrence: SymbolOccurrence, fileNodeId: Int64) {
		let symbol = occurrence.symbol
		guard let (targetParts, targetKind) = resolve(symbol: symbol, location: occurrence.location)
		else {
			return
		}
		// SW11: a reference sitting at a generic bound's target is not a plain
		// container→type usage — emitGenerics attaches it to the parameter node
		// instead. Record the resolved target and suppress the default edge here.
		let position = positionOf(occurrence.location)
		// Remember every resolved reference by position: SW11 constraint targets
		// and SW12 needs the extended-type reference of a conditional-conformance
		// extension too.
		refTargetByPos[position] = (targetParts, targetKind)
		if genericMap?.isConstraintTarget(position) == true {
			return
		}
		let targetId = builder.nodeId(parts: targetParts, kind: targetKind)

		// SW11: a direct type argument of a generic application (`Box<Int>`) is an
		// EDGE_TYPE_ARGUMENT rather than a plain TYPE_USAGE, gated by scope. `local`
		// keeps it only when the base type (`Box`) is defined outside the SDK.
		let forceTypeArgument = isTypeArgumentEdge(at: position)

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
		} else if forceTypeArgument {
			edgeType = EdgeKind.typeArgument
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

	// SW10: a definition gets a precise name TOKEN plus a SCOPE spanning its
	// whole declaration, so the code view can show and navigate the full
	// class/function body (as it does for C++). The store gives only the name
	// position; SwiftSyntax supplies the exact name extent and the brace-to-brace
	// scope. Falls back to the approximate token if the syntax lookup misses
	// (e.g. macro-synthesized members with no source decl).
	private func recordDefinitionLocations(
		elementId: Int64, occurrence: SymbolOccurrence, fileNodeId: Int64
	) {
		let line = Int(max(occurrence.location.line, 1))
		let col = Int(max(occurrence.location.utf8Column, 1))
		if let ext = scopeMap?.extents(line: line, column: col) {
			recordExtent(elementId: elementId, fileNodeId: fileNodeId, extent: ext.name,
				type: LocationKind.token)
			recordExtent(elementId: elementId, fileNodeId: fileNodeId, extent: ext.scope,
				type: LocationKind.scope)
		} else {
			recordTokenOccurrence(elementId: elementId, occurrence: occurrence, fileNodeId: fileNodeId)
		}
	}

	// The (line, utf8-column) an occurrence lands on — the join key between the
	// store's positions and the SwiftSyntax maps (SW10/SW11).
	private func positionOf(_ location: SymbolLocation) -> SyntaxPos {
		SyntaxPos(line: Int(max(location.line, 1)), column: Int(max(location.utf8Column, 1)))
	}

	// SW11: whether a reference at `position` is a direct type argument that
	// should become an EDGE_TYPE_ARGUMENT under the current scope. `all` accepts
	// every application; `local` requires the generic base to be non-system.
	private func isTypeArgumentEdge(at position: SyntaxPos) -> Bool {
		guard let basePos = genericArgMap?.base(of: position) else {
			return false
		}
		switch specializationScope {
		case .off: return false
		case .all: return true
		case .local: return baseIsLocal(basePos)
		}
	}

	// A generic base type is "local" when it has a definition outside the SDK /
	// standard library — i.e. in the indexed package or one of its SPM deps. This
	// keeps stdlib containers (Array, Optional, Dictionary) from emitting a type
	// argument edge for every instantiation.
	private func baseIsLocal(_ basePos: SyntaxPos) -> Bool {
		guard let baseSymbol = symbolByPos[basePos] else {
			return false
		}
		if let cached = localityByUsr[baseSymbol.usr] {
			return cached
		}
		let local = index
			.occurrences(ofUSR: baseSymbol.usr, roles: [.definition, .declaration])
			.contains { !$0.location.isSystem }
		localityByUsr[baseSymbol.usr] = local
		return local
	}

	// SW11: materialize the generic-parameter tier for this file. Each param is a
	// NODE_TYPE_PARAMETER member of its owner with a precise name token; each
	// bound is an edge from the parameter to the resolved constraint target
	// (conformance/class → INHERITANCE, same-type → TYPE_USAGE), carrying the
	// bound type's clickable token. Owners/targets that did not resolve in this
	// file (e.g. not semantically covered) are skipped — the graph degrades to
	// its pre-SW11 shape rather than forking a mis-named node.
	private func emitGenerics(fileNodeId: Int64) {
		guard let genericMap else { return }
		for (ownerPos, params) in genericMap.paramsByOwner {
			guard let ownerParts = defPartsByPos[ownerPos] else { continue }
			let ownerId = builder.nodeId(parts: ownerParts, kind: NodeKind.symbol)
			for param in params {
				let paramId = builder.nodeId(
					parts: ownerParts + [param.name], kind: NodeKind.typeParameter)
				// The store may have already emitted this param under a guessed kind
				// (no dedicated generic-param symbol kind exists); typeParameter is
				// authoritative at its declaration.
				builder.setNodeType(nodeId: paramId, type: NodeKind.typeParameter)
				builder.recordSymbol(nodeId: paramId, definitionKind: DefinitionKind.explicit)
				_ = builder.edgeId(type: EdgeKind.member, source: ownerId, target: paramId)
				recordExtent(
					elementId: paramId, fileNodeId: fileNodeId, extent: param.extent,
					type: LocationKind.token)
			}
		}
		for constraint in genericMap.constraints {
			guard let ownerParts = defPartsByPos[constraint.ownerPos],
				let target = refTargetByPos[constraint.targetPos]
			else { continue }
			let paramId = builder.nodeId(
				parts: ownerParts + [constraint.paramName], kind: NodeKind.typeParameter)
			let targetId = builder.nodeId(parts: target.parts, kind: target.kind)
			let edgeId = builder.edgeId(
				type: constraint.edgeKind, source: paramId, target: targetId)
			recordExtent(
				elementId: edgeId, fileNodeId: fileNodeId, extent: constraint.targetExtent,
				type: LocationKind.token)
		}
	}

	// SW12: conditional-conformance constraints (`extension Pair: Greeter where
	// T: CustomStringConvertible`). The constrained parameter belongs to the
	// extended type, so resolve the extended type at its reference position and
	// attach the bound to its parameter node (created by SW11 when the type was
	// defined; forced to typeParameter if the store guessed another kind).
	private func emitExtensionConstraints(fileNodeId: Int64) {
		guard let genericMap else { return }
		for constraint in genericMap.extensionConstraints {
			guard let extended = refTargetByPos[constraint.extendedTypePos],
				let target = refTargetByPos[constraint.targetPos]
			else { continue }
			let paramId = builder.nodeId(
				parts: extended.parts + [constraint.paramName], kind: NodeKind.typeParameter)
			builder.setNodeType(nodeId: paramId, type: NodeKind.typeParameter)
			let targetId = builder.nodeId(parts: target.parts, kind: target.kind)
			let edgeId = builder.edgeId(
				type: constraint.edgeKind, source: paramId, target: targetId)
			recordExtent(
				elementId: edgeId, fileNodeId: fileNodeId, extent: constraint.targetExtent,
				type: LocationKind.token)
		}
	}

	private func recordExtent(
		elementId: Int64, fileNodeId: Int64, extent: SourceExtent, type: Int32
	) {
		builder.recordOccurrence(
			elementId: elementId,
			fileNodeId: fileNodeId,
			startLine: extent.startLine,
			startCol: extent.startColumn,
			endLine: extent.endLine,
			endCol: extent.endColumn,
			locationType: type
		)
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
	/// libIndexStore.dylib of the toolchain that produced the store. A
	/// configured toolchain (SW5) wins — its libIndexStore must match the store
	/// it wrote; otherwise fall back to the default `swiftc` via xcrun.
	static func libIndexStorePath(toolchainPath: String = "") throws -> String {
		if !toolchainPath.isEmpty {
			// <toolchain>/usr/lib/libIndexStore.dylib
			return URL(fileURLWithPath: toolchainPath)
				.appendingPathComponent("usr/lib/libIndexStore.dylib")
				.path
		}
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
