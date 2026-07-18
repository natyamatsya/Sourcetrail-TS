#[cfg(test)]
mod tests {
    use crate::ipc::storage::{
        OwnedIntermediateStorage, OwnedStorageError, OwnedStorageFile, OwnedStorageNode,
        OwnedStorageOccurrence, OwnedStorageSourceLocation, OwnedStorageSymbol,
    };
    use crate::schemas::intermediate_storage::sourcetrail::ipc::root_as_intermediate_storage_queue;

    // Build a storage, serialize it through the FlatBuffers path used by
    // StorageChannel::push(), then deserialize it back and verify all fields
    // survive the round-trip.
    fn roundtrip(original: &OwnedIntermediateStorage) -> OwnedIntermediateStorage {
        // Replicate the serialize_queue logic (it's private, so we go through
        // the public StorageChannel::push path via a helper that exposes the
        // serialized bytes directly).
        use crate::schemas::intermediate_storage::sourcetrail::ipc::{
            IntermediateStorage, IntermediateStorageArgs, IntermediateStorageQueue,
            IntermediateStorageQueueArgs, StorageEdge, StorageEdgeArgs, StorageError,
            StorageErrorArgs, StorageFile, StorageFileArgs, StorageNode, StorageNodeArgs,
            StorageOccurrence, StorageOccurrenceArgs, StorageSourceLocation,
            StorageSourceLocationArgs, StorageSymbol, StorageSymbolArgs,
        };
        use flatbuffers::FlatBufferBuilder;

        let mut fbb = FlatBufferBuilder::with_capacity(4096);

        // Build the single IntermediateStorage table.
        let node_offsets: Vec<_> = original
            .nodes
            .iter()
            .map(|n| {
                let name = fbb.create_string(&n.serialized_name);
                StorageNode::create(
                    &mut fbb,
                    &StorageNodeArgs {
                        id: n.id,
                        type_: n.type_,
                        serialized_name: Some(name),
                    },
                )
            })
            .collect();
        let nodes_v = fbb.create_vector(&node_offsets);

        let file_offsets: Vec<_> = original
            .files
            .iter()
            .map(|f| {
                let path = fbb.create_string(&f.file_path);
                let lang = fbb.create_string(&f.language_identifier);
                StorageFile::create(
                    &mut fbb,
                    &StorageFileArgs {
                        id: f.id,
                        file_path: Some(path),
                        language_identifier: Some(lang),
                        indexed: f.indexed,
                        complete: f.complete,
                    },
                )
            })
            .collect();
        let files_v = fbb.create_vector(&file_offsets);

        let edge_offsets: Vec<_> = original
            .edges
            .iter()
            .map(|e| {
                StorageEdge::create(
                    &mut fbb,
                    &StorageEdgeArgs {
                        id: e.id,
                        type_: e.type_,
                        source_node_id: e.source_node_id,
                        target_node_id: e.target_node_id,
                    },
                )
            })
            .collect();
        let edges_v = fbb.create_vector(&edge_offsets);

        let sym_offsets: Vec<_> = original
            .symbols
            .iter()
            .map(|s| {
                StorageSymbol::create(
                    &mut fbb,
                    &StorageSymbolArgs {
                        id: s.id,
                        definition_kind: s.definition_kind,
                    },
                )
            })
            .collect();
        let symbols_v = fbb.create_vector(&sym_offsets);

        let loc_offsets: Vec<_> = original
            .source_locations
            .iter()
            .map(|l| {
                StorageSourceLocation::create(
                    &mut fbb,
                    &StorageSourceLocationArgs {
                        id: l.id,
                        file_node_id: l.file_node_id,
                        start_line: l.start_line,
                        start_col: l.start_col,
                        end_line: l.end_line,
                        end_col: l.end_col,
                        type_: l.type_,
                    },
                )
            })
            .collect();
        let locs_v = fbb.create_vector(&loc_offsets);

        let occ_offsets: Vec<_> = original
            .occurrences
            .iter()
            .map(|o| {
                StorageOccurrence::create(
                    &mut fbb,
                    &StorageOccurrenceArgs {
                        element_id: o.element_id,
                        source_location_id: o.source_location_id,
                    },
                )
            })
            .collect();
        let occs_v = fbb.create_vector(&occ_offsets);

        let err_offsets: Vec<_> = original
            .errors
            .iter()
            .map(|e| {
                let msg = fbb.create_string(&e.message);
                let tu = fbb.create_string(&e.translation_unit);
                StorageError::create(
                    &mut fbb,
                    &StorageErrorArgs {
                        id: e.id,
                        message: Some(msg),
                        translation_unit: Some(tu),
                        fatal: e.fatal,
                        indexed: e.indexed,
                    },
                )
            })
            .collect();
        let errs_v = fbb.create_vector(&err_offsets);

        let raw = IntermediateStorage::create(
            &mut fbb,
            &IntermediateStorageArgs {
                next_id: original.next_id,
                nodes: Some(nodes_v),
                files: Some(files_v),
                edges: Some(edges_v),
                symbols: Some(symbols_v),
                source_locations: Some(locs_v),
                local_symbols: None,
                occurrences: Some(occs_v),
                component_accesses: None,
                errors: Some(errs_v),
            },
        );

        let typed: Vec<flatbuffers::WIPOffset<IntermediateStorage>> =
            vec![flatbuffers::WIPOffset::new(raw.value())];
        let storages_v = fbb.create_vector(&typed);
        let queue = IntermediateStorageQueue::create(
            &mut fbb,
            &IntermediateStorageQueueArgs {
                storages: Some(storages_v),
            },
        );
        fbb.finish(queue, None);
        let buf = fbb.finished_data();

        let q = root_as_intermediate_storage_queue(buf).unwrap();
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
                    serialized_name: "my_func".into(),
                    modifiers: 0,
                },
                OwnedStorageNode {
                    id: 3,
                    type_: 64,
                    serialized_name: "MyStruct".into(),
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
        assert_eq!(rt.nodes[0].serialized_name, "my_func");
        assert_eq!(rt.nodes[1].serialized_name, "MyStruct");
    }

    #[test]
    fn roundtrip_files_survive() {
        let original = OwnedIntermediateStorage {
            next_id: 5,
            files: vec![OwnedStorageFile {
                id: 1,
                file_path: "/src/lib.rs".into(),
                language_identifier: "rust".into(),
                indexed: true,
                complete: true,
            }],
            ..Default::default()
        };
        let rt = roundtrip(&original);
        assert_eq!(rt.files.len(), 1);
        assert_eq!(rt.files[0].file_path, "/src/lib.rs");
        assert_eq!(rt.files[0].language_identifier, "rust");
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
                message: "parse error".into(),
                translation_unit: "/src/bad.rs".into(),
                fatal: true,
                indexed: false,
            }],
            ..Default::default()
        };
        let rt = roundtrip(&original);
        assert_eq!(rt.errors.len(), 1);
        assert_eq!(rt.errors[0].message, "parse error");
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
