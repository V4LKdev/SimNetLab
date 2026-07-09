/**
* @defgroup pipeline simnet.pipeline
 * @brief Snapshot replication pipeline.
 *
 * @details
 * Converts authoritative `WorldSnapshot` data into pipeline-owned
 * `EncodedPacket` byte buffers and decodes those buffers into logical
 * `ClientSnapshotPatch` updates. Supports optional replication techniques through a
 * generic `PipelineDefinition`.
 *
 * @note All encoding and decoding functions are thread-safe when each
 * call supplies its own `ClientReplicationState` and `PipelineScratch`.
 */
export module simnet.pipeline;

/**
 * @par simnet.pipeline:types
 * Vocabulary: profiles, codecs, technique flags, settings, client state,
 * and scratch memory.
 */
export import :types;

/**
 * @par simnet.pipeline:messages
 * I/O envelopes: `EncodeInput` / `EncodeOutput`, `DecodeInput` /
 * `DecodeOutput`, `EncodedPacket`, and detailed reports.
 */
export import :messages;

/**
 * @par simnet.pipeline:codec
 * Public encode/decode entry points: `make_raw_snapshot_pipeline`,
 * `pipeline_decode_signature`, `encode_snapshot`, `decode_packet`.
 */
export import :codec;
