module;

#include <filesystem>
#include <string>

#include <raylib.h>

export module simnet.visualization:raylib_instancing;

export namespace simnet::visualization::detail {
    struct BoidRenderAssets {
        Model model{};
        Shader shader{};
        bool loaded = false;
    };

    constexpr const char *instancing_vertex_shader = R"glsl(
#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
in mat4 instanceTransform;

uniform mat4 mvp;

out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;

void main()
{
    vec4 worldPosition = instanceTransform * vec4(vertexPosition, 1.0);
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragNormal = normalize(mat3(instanceTransform) * vertexNormal);
    gl_Position = mvp * worldPosition;
}
)glsl";

    constexpr const char *instancing_fragment_shader = R"glsl(
#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

uniform vec4 colDiffuse;

out vec4 finalColor;

void main()
{
    vec3 lightDir = normalize(vec3(0.35, 0.75, 0.45));
    float light = clamp(dot(normalize(fragNormal), lightDir), 0.0, 1.0) * 0.55 + 0.45;
    finalColor = vec4(colDiffuse.rgb * fragColor.rgb * light, colDiffuse.a * fragColor.a);
}
)glsl";

    [[nodiscard]] inline std::string asset_path(const char *relative_path)
    {
        const std::filesystem::path dev_path =
                std::filesystem::path{SIMNET_DEV_SOURCE_DIR} / relative_path;
        if (std::filesystem::exists(dev_path)) {
            return dev_path.string();
        }
        return relative_path;
    }

    [[nodiscard]] inline BoidRenderAssets load_boid_render_assets()
    {
        BoidRenderAssets assets;
        const std::string model_path = asset_path("assets/boid.obj");
        assets.model = LoadModel(model_path.c_str());
        assets.shader = LoadShaderFromMemory(instancing_vertex_shader, instancing_fragment_shader);
        assets.shader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(assets.shader, "mvp");
        assets.shader.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(assets.shader, "colDiffuse");
        assets.loaded = assets.model.meshCount > 0 && assets.model.materialCount > 0 && assets.shader.id != 0;
        if (assets.loaded) {
            assets.model.materials[0].shader = assets.shader;
        }
        return assets;
    }

    inline void unload_boid_render_assets(BoidRenderAssets &assets)
    {
        if (assets.loaded) {
            UnloadModel(assets.model);
        }
        if (assets.shader.id != 0) {
            UnloadShader(assets.shader);
        }
        assets = {};
    }
}
