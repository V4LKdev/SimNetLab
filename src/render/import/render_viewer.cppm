module;

#include <memory>

export module simnet.render:viewer;

import :types;

export namespace simnet
{
    class Viewer
    {
    public:
        explicit Viewer(ViewerConfig config = {});
        ~Viewer();

        Viewer(Viewer const&) = delete;
        Viewer& operator=(Viewer const&) = delete;
        Viewer(Viewer&&) noexcept;
        Viewer& operator=(Viewer&&) noexcept;

        /// All RenderEntityView spans must remain valid for this call.
        [[nodiscard]] ViewerResult draw(RenderFrame const& frame);
        void set_camera_mode(CameraMode mode);

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };
}
