#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <string>

import simnet.app_common;
import simnet.app_compression_dictionary;
import simnet.config;
import simnet.core;
import simnet.pipeline;

namespace
{
    [[nodiscard]] std::filesystem::path maintained_config_directory()
    {
        return std::filesystem::path{__FILE__}.parent_path().parent_path() / "config";
    }

    [[nodiscard]] std::uint64_t network_fingerprint(simnet::SharedConfig const& config)
    {
        return simnet::fingerprint_network_compatibility(config).value;
    }
}

TEST_CASE("every maintained JSON profile loads through its production loader", "[config][profiles]")
{
    auto loaded_count = std::size_t{};
    for (auto const& entry : std::filesystem::directory_iterator{maintained_config_directory()})
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
        {
            continue;
        }

        auto const name = entry.path().filename().string();
        CAPTURE(name);

        if (name.starts_with("shared_"))
        {
            static_cast<void>(simnet::load_shared_config(entry.path()));
        }
        else if (name.starts_with("server_"))
        {
            static_cast<void>(simnet::load_server_config(entry.path()));
        }
        else if (name.starts_with("client_"))
        {
            static_cast<void>(simnet::load_client_config(entry.path()));
        }
        else
        {
            FAIL("maintained JSON filename has no production loader owner");
        }

        ++loaded_count;
    }

    CHECK(loaded_count > 0U);
}

TEST_CASE("shipped default profiles match typed defaults semantically", "[config][defaults]")
{
    auto const directory = maintained_config_directory();

    auto const typed_shared = simnet::default_shared_config();
    auto const typed_server = simnet::default_server_config();
    auto const typed_client = simnet::default_client_config();

    auto const shipped_shared = simnet::load_shared_config(directory / "shared_default.json");
    auto const shipped_server = simnet::load_server_config(directory / "server_default.json");
    auto const shipped_client = simnet::load_client_config(directory / "client_default.json");

    CHECK(network_fingerprint(shipped_shared) == network_fingerprint(typed_shared));
    CHECK(
        simnet::fingerprint_runtime_config(shipped_shared, shipped_server).value ==
        simnet::fingerprint_runtime_config(typed_shared, typed_server).value
    );
    CHECK(
        simnet::fingerprint_runtime_config(shipped_shared, shipped_client).value ==
        simnet::fingerprint_runtime_config(typed_shared, typed_client).value
    );
}

TEST_CASE("maintained networking profiles are matched treatments", "[config][profiles][experiment]")
{
    auto const directory = maintained_config_directory();

    SECTION("representation and cadence")
    {
        auto raw =
            simnet::load_shared_config(directory / "shared_representation_raw_aoi_radius_visual.json");
        auto quantized = simnet::load_shared_config(
            directory / "shared_representation_quantized_aoi_radius_visual.json"
        );
        auto oct = simnet::load_shared_config(
            directory / "shared_representation_oct_heading_aoi_radius_visual.json"
        );
        auto bit_packed = simnet::load_shared_config(
            directory / "shared_representation_bit_packed_aoi_radius_visual.json"
        );
        auto cadence =
            simnet::load_shared_config(directory / "shared_cadence_reduced_aoi_radius_visual.json");

        CHECK_FALSE(raw.pipeline.enable_quantization);
        CHECK(quantized.pipeline.enable_quantization);
        CHECK(oct.pipeline.enable_oct_heading);
        CHECK(bit_packed.pipeline.enable_bit_packing);
        CHECK(cadence.pipeline.send_interval_ticks == 4U);

        CHECK(network_fingerprint(raw) != network_fingerprint(quantized));
        quantized.pipeline.enable_quantization = false;
        CHECK(network_fingerprint(raw) == network_fingerprint(quantized));

        auto quantized_control = oct;
        quantized_control.pipeline.enable_oct_heading = false;
        CHECK(network_fingerprint(quantized_control) == network_fingerprint(
            simnet::load_shared_config(
                directory / "shared_representation_quantized_aoi_radius_visual.json"
            )
        ));

        auto oct_control = bit_packed;
        oct_control.pipeline.enable_bit_packing = false;
        CHECK(network_fingerprint(oct_control) == network_fingerprint(oct));

        cadence.pipeline.send_interval_ticks = 1U;
        CHECK(network_fingerprint(cadence) == network_fingerprint(raw));
    }

    SECTION("Delta field mask")
    {
        auto control =
            simnet::load_shared_config(directory / "shared_delta_whole_record_aoi_radius_visual.json");
        auto treatment =
            simnet::load_shared_config(directory / "shared_delta_field_mask_aoi_radius_visual.json");

        CHECK(control.pipeline.enable_delta);
        CHECK(treatment.pipeline.enable_delta_field_mask);
        CHECK(network_fingerprint(control) != network_fingerprint(treatment));

        treatment.pipeline.enable_delta_field_mask = false;
        CHECK(network_fingerprint(control) == network_fingerprint(treatment));
    }

    SECTION("snapshot delivery")
    {
        auto reliable =
            simnet::load_shared_config(directory / "shared_delivery_reliable_aoi_radius_visual.json");
        auto unreliable = simnet::load_shared_config(
            directory / "shared_delivery_unreliable_aoi_radius_visual.json"
        );

        CHECK(reliable.snapshot_delivery.mode == "reliable_sequenced");
        CHECK(unreliable.snapshot_delivery.mode == "unreliable_sequenced");
        CHECK(network_fingerprint(reliable) != network_fingerprint(unreliable));

        unreliable.snapshot_delivery.mode = reliable.snapshot_delivery.mode;
        CHECK(network_fingerprint(reliable) == network_fingerprint(unreliable));
    }

    SECTION("area of interest")
    {
        auto radius = simnet::load_shared_config(directory / "shared_aoi_radius_visual.json");
        auto fov = simnet::load_shared_config(directory / "shared_aoi_fov_visual.json");

        CHECK(radius.pipeline.area_of_interest.mode == "radius");
        CHECK(fov.pipeline.area_of_interest.mode == "fov");
        CHECK(network_fingerprint(radius) != network_fingerprint(fov));

        fov.pipeline.area_of_interest = radius.pipeline.area_of_interest;
        CHECK(network_fingerprint(radius) == network_fingerprint(fov));
    }

    SECTION("level of detail")
    {
        auto none =
            simnet::load_shared_config(directory / "shared_lod_none_aoi_radius_visual.json");
        auto distance = simnet::load_shared_config(
            directory / "shared_lod_distance_bands_aoi_radius_visual.json"
        );

        CHECK(none.pipeline.level_of_detail.mode == "none");
        CHECK(distance.pipeline.level_of_detail.mode == "distance_bands");
        CHECK(network_fingerprint(none) != network_fingerprint(distance));

        distance.pipeline.level_of_detail = none.pipeline.level_of_detail;
        CHECK(network_fingerprint(none) == network_fingerprint(distance));
    }

    SECTION("compression mode")
    {
        auto none =
            simnet::load_shared_config(directory / "shared_compression_none_aoi_radius_visual.json");
        auto whole =
            simnet::load_shared_config(directory / "shared_compression_whole_aoi_radius_visual.json");
        auto per_packet = simnet::load_shared_config(
            directory / "shared_compression_per_packet_aoi_radius_visual.json"
        );

        CHECK(none.compression.mode == "none");
        CHECK(whole.compression.mode == "whole_update");
        CHECK(per_packet.compression.mode == "per_packet");

        whole.compression = {};
        per_packet.compression = {};
        CHECK(network_fingerprint(whole) == network_fingerprint(none));
        CHECK(network_fingerprint(per_packet) == network_fingerprint(none));
    }

    SECTION("compression dictionary")
    {
        auto control = simnet::load_shared_config(
            directory / "shared_compression_zstd_delta_field_mask_aoi_radius_visual.json"
        );
        auto treatment = simnet::load_shared_config(
            directory /
            "shared_compression_zstd_pipeline_v1_delta_field_mask_aoi_radius_visual.json"
        );

        CHECK(control.compression.dictionary == "none");
        CHECK(treatment.compression.dictionary == "pipeline_v1");
        CHECK(network_fingerprint(control) != network_fingerprint(treatment));

        treatment.compression.dictionary = control.compression.dictionary;
        CHECK(network_fingerprint(control) == network_fingerprint(treatment));
    }

    SECTION("forced packetization")
    {
        auto const packetized =
            simnet::load_shared_config(directory / "shared_packetization_aoi_radius_visual.json");

        CHECK(packetized.packetization.enabled);
        CHECK(packetized.packetization.max_payload_bytes == 256U);
    }
}

TEST_CASE("representative profiles map to the intended pipeline techniques", "[config][pipeline]")
{
    auto const directory = maintained_config_directory();

    auto const representation = simnet::load_shared_config(
        directory / "shared_representation_bit_packed_aoi_radius_visual.json"
    );
    auto const representation_pipeline = simnet::app::make_snapshot_pipeline(representation);
    CHECK(
        simnet::has_all_flags(
            representation_pipeline.techniques,
            simnet::PipelineTechniqueFlags::Quantization
        )
    );
    CHECK(
        simnet::has_all_flags(
            representation_pipeline.techniques,
            simnet::PipelineTechniqueFlags::OctHeading
        )
    );
    CHECK(
        simnet::has_all_flags(
            representation_pipeline.techniques,
            simnet::PipelineTechniqueFlags::BitPacking
        )
    );

    auto const delta =
        simnet::load_shared_config(directory / "shared_delta_field_mask_aoi_radius_visual.json");
    auto const delta_pipeline = simnet::app::make_snapshot_pipeline(delta);
    CHECK(simnet::has_all_flags(delta_pipeline.techniques, simnet::PipelineTechniqueFlags::Delta));
    CHECK(
        simnet::has_all_flags(
            delta_pipeline.techniques,
            simnet::PipelineTechniqueFlags::DeltaFieldMask
        )
    );

    auto const cadence =
        simnet::load_shared_config(directory / "shared_cadence_reduced_aoi_radius_visual.json");
    auto const cadence_pipeline = simnet::app::make_snapshot_pipeline(cadence);
    CHECK(
        simnet::has_all_flags(
            cadence_pipeline.techniques,
            simnet::PipelineTechniqueFlags::SendInterval
        )
    );
    CHECK(cadence_pipeline.send_interval.interval_ticks == 4U);
}

TEST_CASE("dictionary profile identity is fixed before transport", "[config][compression][transport]")
{
    auto const directory = maintained_config_directory();

    auto const ordinary = simnet::load_shared_config(
        directory / "shared_compression_zstd_delta_field_mask_aoi_radius_visual.json"
    );
    auto const selected = simnet::load_shared_config(
        directory / "shared_compression_zstd_pipeline_v1_delta_field_mask_aoi_radius_visual.json"
    );

    auto const ordinary_pipeline = simnet::app::make_snapshot_pipeline(ordinary);
    auto const selected_pipeline = simnet::app::make_snapshot_pipeline(selected);

    auto loaded =
        simnet::app::load_compression_dictionary(simnet::app::make_compression_settings(selected));
    REQUIRE(loaded.has_value());
    CHECK(loaded->name == "pipeline_v1");
    CHECK(loaded->dictionary.identity().dictionary_id == 0x534E0001U);
    CHECK(loaded->dictionary.identity().byte_count == 16384U);
    CHECK(loaded->dictionary.identity().content_fingerprint == 0x5fe43e7c3e7804a1ULL);

    auto const ordinary_identity = simnet::app::make_session_identity(ordinary, ordinary_pipeline);
    auto const selected_identity = simnet::app::make_session_identity(
        selected,
        selected_pipeline,
        &loaded->dictionary.identity()
    );

    CHECK(
        selected_identity.compatibility_fingerprint != ordinary_identity.compatibility_fingerprint
    );
    CHECK(
        selected_identity.application_wire_fingerprint ==
        ordinary_identity.application_wire_fingerprint
    );
}