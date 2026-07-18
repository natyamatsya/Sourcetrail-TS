#[cfg(test)]
mod tests {
    use crate::ipc::storage::{
        OwnedIntermediateStorage, OwnedStorageError, OwnedStorageFile, OwnedStorageNode,
        OwnedStorageOccurrence, OwnedStorageSourceLocation, OwnedStorageSymbol,
    };

    // Serialize a storage the same way StorageChannel::push() does (object-API
    // pack into a one-entry queue), then deserialize it back and verify all
    // fields survive the round-trip. The whole hand-written build/read is now a
    // pack/unpack pair (see context/DESIGN_STORAGE_CODEGEN.md).
    fn roundtrip(original: &OwnedIntermediateStorage) -> OwnedIntermediateStorage {
        use crate::schemas::intermediate_storage::sourcetrail::ipc::{
            root_as_intermediate_storage_queue, IntermediateStorageQueueT, IntermediateStorageT,
        };
        use flatbuffers::FlatBufferBuilder;

        let storage = IntermediateStorageT {
            next_id: original.next_id,
            nodes: Some(original.nodes.clone()),
            files: Some(original.files.clone()),
            edges: Some(original.edges.clone()),
            symbols: Some(original.symbols.clone()),
            source_locations: Some(original.source_locations.clone()),
            local_symbols: Some(original.local_symbols.clone()),
            occurrences: Some(original.occurrences.clone()),
            component_accesses: Some(original.component_accesses.clone()),
            errors: Some(original.errors.clone()),
        };
        let queue = IntermediateStorageQueueT {
            storages: Some(vec![storage]),
        };
        let mut fbb = FlatBufferBuilder::with_capacity(4096);
        let off = queue.pack(&mut fbb);
        fbb.finish(off, None);

        let q = root_as_intermediate_storage_queue(fbb.finished_data()).unwrap();
        let storages = q.storages().unwrap();
        assert_eq!(storages.len(), 1);
        OwnedIntermediateStorage::from_fbs(storages.get(0))
    }

    #[test]
    fn roundtrip_nodes_survive() {
        let original = OwnedIntermediateStorage {
            next_id: 42,
            nodes: vec![
                OwnedStorageNode {
                    id: 2,
                    type_: 4096,
                    serialized_name: Some("my_func".into()),
                    modifiers: 0,
                },
                OwnedStorageNode {
                    id: 3,
                    type_: 64,
                    serialized_name: Some("MyStruct".into()),
                    modifiers: 0,
                },
            ],
            ..Default::default()
        };
        let rt = roundtrip(&original);
        assert_eq!(rt.next_id, 42);
        assert_eq!(rt.nodes.len(), 2);
        assert_eq!(rt.nodes[0].id, 2);
        assert_eq!(rt.nodes[0].type_, 4096);
        assert_eq!(rt.nodes[0].serialized_name.as_deref(), Some("my_func"));
        assert_eq!(rt.nodes[1].serialized_name.as_deref(), Some("MyStruct"));
    }

    #[test]
    fn roundtrip_files_survive() {
        let original = OwnedIntermediateStorage {
            next_id: 5,
            files: vec![OwnedStorageFile {
                id: 1,
                file_path: Some("/src/lib.rs".into()),
                language_identifier: Some("rust".into()),
                indexed: true,
                complete: true,
            }],
            ..Default::default()
        };
        let rt = roundtrip(&original);
        assert_eq!(rt.files.len(), 1);
        assert_eq!(rt.files[0].file_path.as_deref(), Some("/src/lib.rs"));
        assert_eq!(rt.files[0].language_identifier.as_deref(), Some("rust"));
        assert!(rt.files[0].indexed);
        assert!(rt.files[0].complete);
    }

    #[test]
    fn roundtrip_source_locations_survive() {
        let original = OwnedIntermediateStorage {
            next_id: 10,
            source_locations: vec![OwnedStorageSourceLocation {
                id: 7,
                file_node_id: 1,
                start_line: 3,
                start_col: 5,
                end_line: 3,
                end_col: 12,
                type_: 0,
            }],
            ..Default::default()
        };
        let rt = roundtrip(&original);
        assert_eq!(rt.source_locations.len(), 1);
        let loc = &rt.source_locations[0];
        assert_eq!(loc.start_line, 3);
        assert_eq!(loc.start_col, 5);
        assert_eq!(loc.end_col, 12);
    }

    #[test]
    fn roundtrip_occurrences_survive() {
        let original = OwnedIntermediateStorage {
            next_id: 10,
            occurrences: vec![OwnedStorageOccurrence {
                element_id: 2,
                source_location_id: 7,
            }],
            ..Default::default()
        };
        let rt = roundtrip(&original);
        assert_eq!(rt.occurrences.len(), 1);
        assert_eq!(rt.occurrences[0].element_id, 2);
        assert_eq!(rt.occurrences[0].source_location_id, 7);
    }

    #[test]
    fn roundtrip_symbols_survive() {
        let original = OwnedIntermediateStorage {
            next_id: 5,
            symbols: vec![OwnedStorageSymbol {
                id: 2,
                definition_kind: 2,
            }],
            ..Default::default()
        };
        let rt = roundtrip(&original);
        assert_eq!(rt.symbols.len(), 1);
        assert_eq!(rt.symbols[0].definition_kind, 2);
    }

    #[test]
    fn roundtrip_errors_survive() {
        let original = OwnedIntermediateStorage {
            next_id: 3,
            errors: vec![OwnedStorageError {
                id: 2,
                message: Some("parse error".into()),
                translation_unit: Some("/src/bad.rs".into()),
                fatal: true,
                indexed: false,
            }],
            ..Default::default()
        };
        let rt = roundtrip(&original);
        assert_eq!(rt.errors.len(), 1);
        assert_eq!(rt.errors[0].message.as_deref(), Some("parse error"));
        assert!(rt.errors[0].fatal);
    }

    #[test]
    fn roundtrip_empty_storage() {
        let original = OwnedIntermediateStorage {
            next_id: 1,
            ..Default::default()
        };
        let rt = roundtrip(&original);
        assert_eq!(rt.next_id, 1);
        assert!(rt.nodes.is_empty());
        assert!(rt.files.is_empty());
    }
}
