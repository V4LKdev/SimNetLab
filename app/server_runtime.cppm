/// @brief Server executable runtime entry point.
export module simnet.server_runtime;

export namespace simnet::app
{
    [[nodiscard]] int run_server(int argc, char** argv);
}
