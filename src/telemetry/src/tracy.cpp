module;
#include <memory>
#include <tracy/Tracy.hpp>

module telemetry;

struct ScopedTracyZone::Impl {
    tracy::SourceLocationData srcloc;
    tracy::ScopedZone zone;

    Impl(const char *name, const char *file, int line)
        : srcloc{
              name, nullptr /* function */,
              file ? file : "",
              static_cast<uint32_t>(line),
              0
          }
          , zone{&srcloc, -1 /* depth */, true /* active */}
    {
    }
};

ScopedTracyZone::ScopedTracyZone(const char *name,
                                 const char *file,
                                 int line)
    : impl_{std::make_unique<Impl>(name, file, line)}
{
}

ScopedTracyZone::~ScopedTracyZone() = default;
