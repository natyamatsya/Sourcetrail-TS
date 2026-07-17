// Storage assembly with the same dedup discipline as the Rust collector:
// nodes dedup by serialized name, edges by (type, source, target), files by
// path — so re-encounters (a symbol referenced from many files) never fork
// duplicate rows. PersistentStorage injects by name, but within one push the
// ids must already be coherent.

final class StorageBuilder {
	private(set) var storage = OwnedIntermediateStorage()

	/// file path → file node id
	private var fileIds: [String: Int64] = [:]
	/// serialized node name → node id
	private var nodeIds: [String: Int64] = [:]
	/// node ids that already have a symbol row
	private var symbolIds: Set<Int64> = []
	/// (type, source, target) → edge id
	private var edgeIds: [String: Int64] = [:]
	/// local symbol name → id
	private var localSymbolIds: [String: Int64] = [:]

	func fileNodeId(path: String, complete: Bool) -> Int64 {
		if let id = fileIds[path] {
			return id
		}
		let id = storage.allocateId()
		fileIds[path] = id
		storage.nodes.append(
			OwnedStorageNode(
				id: id,
				type: NodeKind.file,
				serializedName: NameHierarchy.serializeFile(path: path)
			)
		)
		storage.files.append(
			OwnedStorageFile(id: id, filePath: path, complete: complete)
		)
		return id
	}

	func hasFile(path: String) -> Bool {
		fileIds[path] != nil
	}

	/// Node for `parts`, deduped by serialized name. First emission wins the
	/// kind; later, more specific kinds upgrade a plain `symbol` placeholder.
	func nodeId(parts: [String], kind: Int32) -> Int64 {
		let name = NameHierarchy.serialize(parts: parts)
		if let id = nodeIds[name] {
			if kind != NodeKind.symbol,
				let index = storage.nodes.firstIndex(where: { $0.id == id }),
				storage.nodes[index].type == NodeKind.symbol
			{
				storage.nodes[index] = OwnedStorageNode(
					id: id, type: kind, serializedName: name)
			}
			return id
		}
		let id = storage.allocateId()
		nodeIds[name] = id
		storage.nodes.append(
			OwnedStorageNode(id: id, type: kind, serializedName: name)
		)
		return id
	}

	func recordSymbol(nodeId: Int64, definitionKind: Int32) {
		if symbolIds.insert(nodeId).inserted {
			storage.symbols.append(
				OwnedStorageSymbol(id: nodeId, definitionKind: definitionKind)
			)
		}
	}

	func edgeId(type: Int32, source: Int64, target: Int64) -> Int64 {
		let key = "\(type):\(source):\(target)"
		if let id = edgeIds[key] {
			return id
		}
		let id = storage.allocateId()
		edgeIds[key] = id
		storage.edges.append(
			OwnedStorageEdge(id: id, type: type, sourceNodeId: source, targetNodeId: target)
		)
		return id
	}

	func localSymbolId(name: String) -> Int64 {
		if let id = localSymbolIds[name] {
			return id
		}
		let id = storage.allocateId()
		localSymbolIds[name] = id
		storage.localSymbols.append(OwnedStorageLocalSymbol(id: id, name: name))
		return id
	}

	@discardableResult
	func recordOccurrence(
		elementId: Int64,
		fileNodeId: Int64,
		startLine: UInt32,
		startCol: UInt32,
		endLine: UInt32,
		endCol: UInt32,
		locationType: Int32
	) -> Int64 {
		let locationId = storage.allocateId()
		storage.sourceLocations.append(
			OwnedStorageSourceLocation(
				id: locationId,
				fileNodeId: fileNodeId,
				startLine: startLine,
				startCol: startCol,
				endLine: endLine,
				endCol: endCol,
				type: locationType
			)
		)
		storage.occurrences.append(
			OwnedStorageOccurrence(elementId: elementId, sourceLocationId: locationId)
		)
		return locationId
	}

	func recordError(message: String, translationUnit: String, fatal: Bool = false) {
		storage.errors.append(
			OwnedStorageError(
				id: storage.allocateId(),
				message: message,
				translationUnit: translationUnit,
				fatal: fatal
			)
		)
	}

	func appendPlainFile(path: String, complete: Bool) {
		if !hasFile(path: path) {
			_ = fileNodeId(path: path, complete: complete)
		}
	}
}
