
#include "client_ecs.hpp"

#include <unordered_map>

#include "../../game/shared/components.hpp"

namespace simnet::client::ecs {
    void init_client_ecs(flecs::world &world)
    {
        world.component<PendingSnapshot>().add(flecs::Singleton);
        world.set<PendingSnapshot>({});

        auto snap_phase = world.entity("ApplySnapshotPhase").add(flecs::Phase);

        // Build a reusable query for looking up entities by NetworkId
        static auto lookup_query = world.query<simnet::ecs::NetworkId, simnet::ecs::Boid>();

        world.system("ApplySnapshot")
                .kind(snap_phase)
                .multi_threaded(false)
                .run([&](flecs::iter &it) {
                    auto *snap = it.world().try_get_mut<PendingSnapshot>();
                    if (!snap || !snap->new_data) return;

                    // Build map of existing NetworkId -> entity
                    std::unordered_map<uint32_t, flecs::entity> existing;
                    lookup_query.run([&](flecs::iter &qit) {
                        while (qit.next()) {
                            auto nids = qit.field<const simnet::ecs::NetworkId>(0);
                            for (int i = 0; i < qit.count(); i++)
                                existing[nids[i].value] = qit.entity(i);
                        }
                    });

                    // Update or create entities
                    for (const auto &re: snap->data.entities) {
                        auto ent = existing.find(re.network_id);
                        flecs::entity e;
                        if (ent != existing.end()) {
                            e = ent->second;
                            existing.erase(ent);
                        } else {
                            e = it.world().entity();
                            e.add<simnet::ecs::Boid>();
                            e.set<simnet::ecs::NetworkId>({re.network_id});
                            e.set<simnet::ecs::Velocity>({Vec3::zero()});
                            e.set<simnet::ecs::SteeringAccumulate>({});
                            e.set<simnet::ecs::BoidIdx>({});
                        }
                        e.set<simnet::ecs::Position>({re.position});
                        e.set<simnet::ecs::Heading>({re.heading});
                        e.set<simnet::ecs::Hue>({re.hue});
                    }

                    // Remove stale entities
                    for (auto &[id, e]: existing)
                        e.destruct();

                    snap->new_data = false;
                });
    }
}
