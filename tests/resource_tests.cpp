// Tests for GPU resources made without a GPU (rendering/gpu_resource.h, texture.h, shader.h,
// render_target.h): what games create is plain data; the renderer makes the GPU side later.
#include <cstdint>
#include <memory>
#include <vector>

#include "test_helpers.h"
#include "wlw/rendering/render_target.h"
#include "wlw/rendering/shader.h"
#include "wlw/rendering/texture.h"
#include "wlw/scene/viewport.h"
#include "wlw/utils/textures.h"

TEST_SUITE_BEGIN("resource");

using namespace wlw;
using rendering::TextureFormat;

namespace {

// How many alive resources are `resource` (0 or 1).
int TimesAlive(const rendering::Resource* resource) {
  int found = 0;
  rendering::Resource::InternalAccess::ForEachAlive([&](rendering::Resource& alive) { found += &alive == resource; });
  return found;
}

}  // namespace

TEST_CASE("texture data") {
  // Made with its texels, which wait (copied) for the GPU.
  const uint8_t texels[2 * 2] = {1, 2, 3, 4};
  auto texture = rendering::WTexture2D::Create(2, 2, {.format = TextureFormat::R8}, texels);
  CHECK((texture->Width() == 2 && texture->Height() == 2 && texture->Format() == TextureFormat::R8));
  CHECK(texture->Internal().Pixels() == std::vector<uint8_t>({1, 2, 3, 4}));
  CHECK(texture->Internal().Gpu() == nullptr);  // nothing on the GPU yet
  const uint64_t made = texture->Internal().Version();

  // Update copies: the caller's buffer may change or go away afterwards.
  std::vector<float> data(2 * 2, 0.5f);
  auto floats = rendering::WTexture2D::Create(2, 2, {.format = TextureFormat::R16F});
  CHECK(floats->Internal().Pixels().empty());  // no data: the renderer starts it at zero
  floats->Update(data.data());
  data[0] = 9.0f;
  CHECK(floats->Internal().Pixels().size() == 2 * 2 * sizeof(float));
  CHECK(reinterpret_cast<const float*>(floats->Internal().Pixels().data())[0] == 0.5f);

  texture->Update(texels);
  CHECK(texture->Internal().Version() == made + 1);  // tells the renderer to upload again
  texture->Internal().ReleasePixels();               // what the renderer does once they're on the GPU
  CHECK(texture->Internal().Pixels().empty());

  CHECK((rendering::TexelBytes(TextureFormat::RGB8) == 3 && rendering::TexelBytes(TextureFormat::RGBA16F) == 16));
}

TEST_CASE("texture from image") {
  utils::RawImage image{{3.0f, 1.0f}, 3, std::vector<unsigned char>(3 * 3, 7)};
  auto texture = rendering::WTexture2D::FromImage(image);
  CHECK((texture->Width() == 3 && texture->Height() == 1 && texture->Format() == TextureFormat::RGB8));
  CHECK((texture->Settings().repeat && texture->Settings().mipmaps));  // drawn tiled / small in 3D
  CHECK(texture->Internal().Pixels().size() == 9);

  auto dot = utils::RadialFalloffTexture(8);  // no device: a plain texture
  CHECK((dot->Width() == 8 && dot->Internal().Pixels().size() == 8 * 8 * 4 && !dot->Settings().repeat));
}

TEST_CASE("ids are unique") {
  auto a = rendering::WTexture2D::Create(1, 1);
  auto b = rendering::WTexture2D::Create(1, 1);
  auto shader = rendering::Shader::Create("shader_type canvas; void fragment() { FragColor = vec4(1.0); }");
  CHECK((a->Id() != b->Id() && b->Id() != shader->Id() && a->Id() != 0));
}

TEST_CASE("shaders") {
  // Checked when made: its kind is known without compiling.
  auto canvas = rendering::Shader::Create("shader_type canvas; void fragment() { FragColor = vec4(1.0); }", "glow");
  CHECK((canvas->IsValid() && canvas->GetTarget() == rendering::Shader::Target::Canvas));
  CHECK(canvas->Name() == "glow");
  auto mesh = rendering::Shader::Create("shader_type mesh; void fragment() { FragColor = vec4(1.0); }");
  CHECK((mesh->IsValid() && mesh->GetTarget() == rendering::Shader::Target::Mesh));

  // A wrong text is invalid at once (logged), so nothing tries to draw with it.
  auto broken = rendering::Shader::Create("void fragment() {}");
  CHECK((!broken->IsValid() && !broken->Internal().Source().error.empty()));

  // One that didn't compile (the renderer marks it) stays invalid.
  canvas->Internal().MarkFailed();
  CHECK(!canvas->IsValid());

  auto missing = rendering::Shader::FromFile("no/such/shader.glsl");
  CHECK((!missing->IsValid() && missing->Name() == "no/such/shader.glsl"));
}

TEST_CASE("render target") {
  auto target = rendering::RenderTarget::Create({.format = TextureFormat::RGBA16F, .depth = true});
  const auto texture = target->GetTexture();
  CHECK((texture->Internal().IsRenderTarget() && texture->Format() == TextureFormat::RGBA16F));
  CHECK((target->Width() == 1 && target->Height() == 1));

  // Resizing changes the size the renderer will allocate; the texture object stays the same, so
  // materials holding it keep working.
  target->SetSize(320, 240);
  CHECK((target->GetTexture() == texture && texture->Width() == 320 && texture->Height() == 240));
  target->SetSize(0, -5);  // never below one texel
  CHECK((target->Width() == 1 && target->Height() == 1));

  // A render target's texels come from the GPU: Update is ignored.
  const float texel[4] = {1, 1, 1, 1};
  texture->Update(texel);
  CHECK(texture->Internal().Pixels().empty());

  // The GPU couldn't draw 16-bit floats: the renderer falls back, and the texture follows.
  target->Internal().SetFormat(TextureFormat::RGBA8);
  CHECK((target->Settings().format == TextureFormat::RGBA8 && texture->Format() == TextureFormat::RGBA8));

  // Viewports make theirs the same way: no device.
  auto view = scene::Viewport::Create({.format = TextureFormat::RGBA16F, .depth = false});
  CHECK((view->Internal().GetTarget() != nullptr && view->GetTexture() == view->Internal().GetTarget()->GetTexture()));
  CHECK((view->Internal().GetTarget()->Settings().format == TextureFormat::RGBA16F &&
         !view->Internal().GetTarget()->Settings().depth));
}

TEST_CASE("alive resources") {
  // The warm-up finds every resource alive (shaders included: canvas ones aren't in any scene)...
  auto texture = rendering::WTexture2D::Create(1, 1);
  auto shader = rendering::Shader::Create("shader_type canvas; void fragment() { FragColor = vec4(1.0); }");
  auto target = rendering::RenderTarget::Create();
  CHECK((TimesAlive(texture.get()) == 1 && TimesAlive(shader.get()) == 1 && TimesAlive(target.get()) == 1));

  // ...and doesn't keep any alive.
  const rendering::Resource* gone = texture.get();
  texture.reset();
  CHECK(TimesAlive(gone) == 0);

  // Making resources while visiting is allowed (a visit may load something): they're visited too.
  std::unique_ptr<rendering::WTexture2D> made;
  bool made_visited = false;
  rendering::Resource::InternalAccess::ForEachAlive([&](rendering::Resource& alive) {
    if (!made) made = rendering::WTexture2D::Create(1, 1);
    made_visited |= &alive == made.get();
  });
  CHECK(made_visited);
}

TEST_CASE("alive list: owned any way, destroyed while visiting") {
  const size_t before = rendering::Resource::InternalAccess::AliveCount();
  {
    // Create returns unique_ptr; turned into a shared_ptr it's still the same object on the list.
    std::shared_ptr<rendering::WTexture2D> shared = rendering::WTexture2D::Create(1, 1);
    auto unique = rendering::RenderTarget::Create();  // its texture is a resource too
    CHECK((TimesAlive(shared.get()) == 1 && TimesAlive(unique.get()) == 1));
    CHECK(TimesAlive(unique->GetTexture().get()) == 1);
    CHECK(rendering::Resource::InternalAccess::AliveCount() == before + 3);

    // Destroying one the walk hasn't reached yet: skipped, never visited dead.
    auto first = rendering::WTexture2D::Create(1, 1);
    auto second = rendering::WTexture2D::Create(1, 1);
    const rendering::Resource* second_address = second.get();
    bool second_visited = false;
    rendering::Resource::InternalAccess::ForEachAlive([&](rendering::Resource& alive) {
      if (&alive == first.get()) second.reset();
      second_visited |= &alive == second_address;
    });
    CHECK(!second_visited);
    CHECK(TimesAlive(first.get()) == 1);
  }
  // Everything made above is gone, and the list is whole again (compacted after the walk).
  CHECK(rendering::Resource::InternalAccess::AliveCount() == before);
  int count = 0;
  rendering::Resource::InternalAccess::ForEachAlive([&](rendering::Resource&) { ++count; });
  CHECK(static_cast<size_t>(count) == before);
}

TEST_SUITE_END();
