export module simnet.app_evidence;

import simnet.pipeline;
import simnet.telemetry;

export namespace simnet::app
{
    /// Flattens one production encode report into application-owned research evidence.
    void flatten_server_encode_report(
        ServerReplicationMeasurement& measurement,
        EncodeReport const& report,
        ClientReplicationState const& state
    ) noexcept;
}
