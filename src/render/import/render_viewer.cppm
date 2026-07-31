module;

#include <memory>
#include <string>

export module simnet.render:viewer;

import :types;

export namespace simnet
{
    class Viewer
    {
    public:
        /// Empty output_directory saves screenshots in the current working directory.
        explicit Viewer(ViewerConfig config = {}, std::string output_directory = {});
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
