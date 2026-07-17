// Split a large IntermediateStorage into self-contained chunks that each
// serialize well under the fixed 16 MiB SHM segment (ADR-0002). Port of the
// Rust indexer's chunker (ipc/storage.rs): every element group (a node with
// its symbol/file/occurrences, an edge with its endpoint node stubs, a local
// symbol) is emitted atomically into one chunk together with the rows it
// references. Cross-chunk dedup happens by serialized name / position at
// PersistentStorage inject, so repeating a referenced node stub in a later
// chunk is safe.

package enum StorageChunker {
	// Two chunks plus queue headers must stay comfortably below 16 MiB, and
	// the channel keeps up to two entries queued.
	static let budgetBytes = 7 * 1024 * 1024

	private static let edgeCost = 56
	private static let symbolCost = 32
	private static let locationCost = 64
	private static let occurrenceCost = 32
	private static let stubCostMax = 512

	private static func nodeCost(_ node: OwnedStorageNode) -> Int {
		48 + node.serializedName.utf8.count
	}
	private static func fileCost(_ file: OwnedStorageFile) -> Int {
		64 + file.filePath.utf8.count + file.languageIdentifier.utf8.count
	}
	private static func localSymbolCost(_ localSymbol: OwnedStorageLocalSymbol) -> Int {
		48 + localSymbol.name.utf8.count
	}
	private static func errorCost(_ error: OwnedStorageError) -> Int {
		64 + error.message.utf8.count + error.translationUnit.utf8.count
	}

	package static func estimatedSize(_ storage: OwnedIntermediateStorage) -> Int {
		var total = 1024
		total += storage.nodes.reduce(0) { $0 + nodeCost($1) }
		total += storage.files.reduce(0) { $0 + fileCost($1) }
		total += storage.edges.count * edgeCost
		total += storage.symbols.count * symbolCost
		total += storage.sourceLocations.count * locationCost
		total += storage.localSymbols.reduce(0) { $0 + localSymbolCost($1) }
		total += storage.occurrences.count * occurrenceCost
		total += storage.errors.reduce(0) { $0 + errorCost($1) }
		return total
	}

	/// Split into self-contained chunks. A storage within budget passes
	/// through as a single chunk unchanged.
	package static func chunks(_ storage: OwnedIntermediateStorage) -> [OwnedIntermediateStorage] {
		if estimatedSize(storage) <= budgetBytes {
			return [storage]
		}
		var chunker = Chunker(source: storage)
		return chunker.run()
	}

	private struct Chunker {
		let source: OwnedIntermediateStorage
		let nodesById: [Int64: OwnedStorageNode]
		let filesByNodeId: [Int64: OwnedStorageFile]
		let symbolsByNodeId: [Int64: OwnedStorageSymbol]
		let locationsById: [Int64: OwnedStorageSourceLocation]
		var occIndicesByElement: [Int64: [Int]]

		var emittedLocations: Set<Int64> = []
		var chunks: [OwnedIntermediateStorage] = []
		var cur: OwnedIntermediateStorage
		var curCost = 0
		var curNodeIds: Set<Int64> = []
		var curLocationIds: Set<Int64> = []

		init(source: OwnedIntermediateStorage) {
			self.source = source
			nodesById = Dictionary(source.nodes.map { ($0.id, $0) }, uniquingKeysWith: { a, _ in a })
			filesByNodeId = Dictionary(source.files.map { ($0.id, $0) }, uniquingKeysWith: { a, _ in a })
			symbolsByNodeId = Dictionary(source.symbols.map { ($0.id, $0) }, uniquingKeysWith: { a, _ in a })
			locationsById = Dictionary(
				source.sourceLocations.map { ($0.id, $0) }, uniquingKeysWith: { a, _ in a })
			var occ: [Int64: [Int]] = [:]
			for (index, occurrence) in source.occurrences.enumerated() {
				occ[occurrence.elementId, default: []].append(index)
			}
			occIndicesByElement = occ
			cur = OwnedIntermediateStorage()
			cur.nextId = source.nextId
		}

		mutating func run() -> [OwnedIntermediateStorage] {
			for node in source.nodes {
				let occs = occIndicesByElement.removeValue(forKey: node.id) ?? []
				ensureBudget(nodeGroupCost(nodeId: node.id, occs: occs))
				addNode(node.id, withSymbol: true)
				addOccurrences(occs)
			}
			for edge in source.edges {
				let occs = occIndicesByElement.removeValue(forKey: edge.id) ?? []
				ensureBudget(edgeGroupCost(edge, occs: occs))
				addNode(edge.sourceNodeId, withSymbol: false)
				addNode(edge.targetNodeId, withSymbol: false)
				cur.edges.append(edge)
				curCost += edgeCost
				addOccurrences(occs)
			}
			for localSymbol in source.localSymbols {
				let occs = occIndicesByElement.removeValue(forKey: localSymbol.id) ?? []
				ensureBudget(localSymbolCost(localSymbol) + occurrencesCost(occs))
				cur.localSymbols.append(localSymbol)
				curCost += localSymbolCost(localSymbol)
				addOccurrences(occs)
			}
			// Dangling occurrences (element not in this storage): keep them so
			// chunked inject matches monolithic inject (warn + drop).
			let dangling = occIndicesByElement.values.flatMap { $0 }.sorted()
			for index in dangling {
				ensureBudget(occurrenceCost + locationCost + stubCostMax)
				addOccurrences([index])
			}
			// Locations with no occurrence at all.
			for location in source.sourceLocations where !emittedLocations.contains(location.id) {
				ensureBudget(locationCost + stubCostMax)
				addLocation(location.id)
			}
			for error in source.errors {
				ensureBudget(errorCost(error))
				cur.errors.append(error)
				curCost += errorCost(error)
			}
			flush()
			return chunks
		}

		private func nodeGroupCost(nodeId: Int64, occs: [Int]) -> Int {
			var cost = (nodesById[nodeId].map(nodeCost) ?? 0) + symbolCost
			if let file = filesByNodeId[nodeId] {
				cost += fileCost(file)
			}
			return cost + occurrencesCost(occs)
		}

		private func edgeGroupCost(_ edge: OwnedStorageEdge, occs: [Int]) -> Int {
			edgeCost
				+ nodeGroupCost(nodeId: edge.sourceNodeId, occs: [])
				+ nodeGroupCost(nodeId: edge.targetNodeId, occs: [])
				+ occurrencesCost(occs)
		}

		private func occurrencesCost(_ occs: [Int]) -> Int {
			occs.count * (occurrenceCost + locationCost) + min(occs.count, 4) * stubCostMax
		}

		private mutating func ensureBudget(_ groupCost: Int) {
			if curCost > 0 && curCost + groupCost > budgetBytes {
				flush()
			}
		}

		private mutating func flush() {
			if curCost == 0 {
				return
			}
			chunks.append(cur)
			cur = OwnedIntermediateStorage()
			cur.nextId = source.nextId
			curCost = 0
			curNodeIds.removeAll(keepingCapacity: true)
			curLocationIds.removeAll(keepingCapacity: true)
		}

		private mutating func addNode(_ nodeId: Int64, withSymbol: Bool) {
			if curNodeIds.insert(nodeId).inserted {
				if let node = nodesById[nodeId] {
					cur.nodes.append(node)
					curCost += nodeCost(node)
				}
				if let file = filesByNodeId[nodeId] {
					cur.files.append(file)
					curCost += fileCost(file)
				}
			}
			if withSymbol, let symbol = symbolsByNodeId[nodeId] {
				cur.symbols.append(symbol)
				curCost += symbolCost
			}
		}

		private mutating func addLocation(_ locationId: Int64) {
			guard let location = locationsById[locationId] else {
				return
			}
			if !curLocationIds.insert(locationId).inserted {
				return
			}
			addNode(location.fileNodeId, withSymbol: false)
			cur.sourceLocations.append(location)
			curCost += locationCost
			emittedLocations.insert(locationId)
		}

		private mutating func addOccurrences(_ occIndices: [Int]) {
			for index in occIndices {
				let occurrence = source.occurrences[index]
				addLocation(occurrence.sourceLocationId)
				cur.occurrences.append(occurrence)
				curCost += occurrenceCost
			}
		}
	}
}
