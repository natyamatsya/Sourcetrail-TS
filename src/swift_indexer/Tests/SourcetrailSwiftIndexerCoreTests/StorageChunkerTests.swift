import Testing

@testable import SourcetrailSwiftIndexerCore

@Suite struct StorageChunkerTests {
	@Test func smallStoragePassesThroughAsSingleChunk() {
		var storage = OwnedIntermediateStorage()
		let a = storage.allocateId()
		storage.nodes.append(OwnedStorageNode(id: a, type: NodeKind.class, serializedName: "A"))
		storage.symbols.append(OwnedStorageSymbol(id: a, definitionKind: DefinitionKind.explicit))

		let chunks = StorageChunker.chunks(storage)
		#expect(chunks.count == 1)
		#expect(chunks[0].nodes.count == 1)
	}

	// A storage above the budget splits into multiple chunks, each of which is
	// self-contained: every edge endpoint, every occurrence's element and
	// location, and every location's file node appear in the SAME chunk.
	@Test func largeStorageSplitsIntoSelfContainedChunks() {
		var storage = OwnedIntermediateStorage()
		let fileId = storage.allocateId()
		storage.nodes.append(
			OwnedStorageNode(id: fileId, type: NodeKind.file, serializedName: "/f.swift"))
		storage.files.append(OwnedStorageFile(id: fileId, filePath: "/f.swift", complete: true))

		// Enough padded nodes/edges/occurrences to blow past the 7 MiB budget.
		let padding = String(repeating: "x", count: 4096)
		var previous: Int64?
		for i in 0..<3000 {
			let nodeId = storage.allocateId()
			storage.nodes.append(
				OwnedStorageNode(
					id: nodeId, type: NodeKind.method, serializedName: "n\(i)-\(padding)"))
			storage.symbols.append(
				OwnedStorageSymbol(id: nodeId, definitionKind: DefinitionKind.explicit))
			let locId = storage.allocateId()
			storage.sourceLocations.append(
				OwnedStorageSourceLocation(
					id: locId, fileNodeId: fileId, startLine: UInt32(i + 1), startCol: 1,
					endLine: UInt32(i + 1), endCol: 4, type: LocationKind.token))
			storage.occurrences.append(
				OwnedStorageOccurrence(elementId: nodeId, sourceLocationId: locId))
			if let previous {
				storage.edges.append(
					OwnedStorageEdge(
						id: storage.allocateId(), type: EdgeKind.call, sourceNodeId: previous,
						targetNodeId: nodeId))
			}
			previous = nodeId
		}

		let chunks = StorageChunker.chunks(storage)
		#expect(chunks.count > 1, "expected the oversized storage to split")

		for chunk in chunks {
			let nodeIds = Set(chunk.nodes.map(\.id))
			let locationIds = Set(chunk.sourceLocations.map(\.id))
			// Edge endpoints present in-chunk.
			for edge in chunk.edges {
				#expect(nodeIds.contains(edge.sourceNodeId))
				#expect(nodeIds.contains(edge.targetNodeId))
			}
			// Occurrence element + location present in-chunk.
			for occurrence in chunk.occurrences {
				#expect(nodeIds.contains(occurrence.elementId))
				#expect(locationIds.contains(occurrence.sourceLocationId))
			}
			// Each location's file node present in-chunk.
			for location in chunk.sourceLocations {
				#expect(nodeIds.contains(location.fileNodeId))
			}
			// Each chunk stays within budget.
			#expect(StorageChunker.estimatedSize(chunk) <= StorageChunker.budgetBytes + 4096)
		}

		// No rows lost across the split.
		#expect(chunks.reduce(0) { $0 + $1.edges.count } == storage.edges.count)
		#expect(chunks.reduce(0) { $0 + $1.occurrences.count } == storage.occurrences.count)
		let allNodeIds = Set(chunks.flatMap { $0.nodes.map(\.id) })
		#expect(allNodeIds == Set(storage.nodes.map(\.id)))
	}
}
