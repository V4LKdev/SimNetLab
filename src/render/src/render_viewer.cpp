module;

#include <memory>
#include <stdexcept>

#include <raylib.h>

module simnet.render;

namespace
{
    bool viewer_active = false;

    void validate_config(simnet::ViewerConfig const& config)
    {
        if (config.window_width == 0 || config.window_height == 0) {
            throw std::runtime_error("viewer window dimensions must be non-zero");
        }
        if (config.panel_width >= config.window_width) {
            throw std::runtime_error("viewer panel_width must be less than window_width");
        }
        if (config.target_frame_rate == 0) {
            throw std::runtime_error("viewer target_frame_rate must be non-zero");
        }
        if (config.entity_scale <= 0.0F || config.picking_radius <= 0.0F) {
            throw std::runtime_error("viewer entity_scale and picking_radius must be positive");
        }
    }
}

namespace simnet
{
    class Viewer::Impl
    {
    public:
        explicit Impl(ViewerConfig const& config)
        {
            validate_config(config);
            if (viewer_active) {
                throw std::runtime_error("only one Viewer may be active per process");
            }
            InitWindow(static_cast<int>(config.window_width), static_cast<int>(config.window_height), config.title.c_str());
            if (!IsWindowReady()) {
                throw std::runtime_error("failed to create viewer window");
            }
            SetTargetFPS(static_cast<int>(config.target_frame_rate));
            viewer_active = true;
        }

        ~Impl()
        {
            if (IsWindowReady()) {
                CloseWindow();
            }
            viewer_active = false;
        }

        [[nodiscard]] ViewerResult draw(RenderFrame const& frame)
        {
            auto result = ViewerResult {};
            result.view_mode = mode_;
            if (!frame.entities.valid()) {
                return result;
            }
            return result;
        }

        void set_view_mode(ViewMode mode) noexcept
        {
            mode_ = mode;
        }

    private:
        ViewMode mode_ { ViewMode::Overview };
    };

    Viewer::Viewer(ViewerConfig config)
        : impl_(std::make_unique<Impl>(config))
    {
    }

    Viewer::~Viewer() = default;
    Viewer::Viewer(Viewer&&) noexcept = default;
    Viewer& Viewer::operator=(Viewer&&) noexcept = default;

    ViewerResult Viewer::draw(RenderFrame const& frame)
    {
        if (!impl_) {
            throw std::runtime_error("cannot draw with a moved-from Viewer");
        }
        return impl_->draw(frame);
    }

    void Viewer::set_view_mode(ViewMode mode)
    {
        if (!impl_) {
            throw std::runtime_error("cannot configure a moved-from Viewer");
        }
        impl_->set_view_mode(mode);
    }
}
