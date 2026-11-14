#include "sdl.hpp"

#include "error.hpp"

#include <SDL2/SDL_image.h>
#include <memory>
#include <string>

namespace
{
    // Trivial state remains readable by Texture destructors during static teardown.
    constinit uint64_t s_rendererGeneration{};
    constinit uint64_t s_nextRendererGeneration{};
    constinit SDL_Renderer *s_activeRenderer{};

    void destroy_renderer(SDL_Renderer *renderer) noexcept
    {
        if (renderer == s_activeRenderer)
        {
            s_rendererGeneration = 0;
            s_activeRenderer = nullptr;
        }
        SDL_DestroyRenderer(renderer);
    }

    using Window   = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;
    using Renderer = std::unique_ptr<SDL_Renderer, decltype(&destroy_renderer)>;

    // Reverse destruction order must release the renderer before its window.
    Window s_window     = Window(nullptr, SDL_DestroyWindow);
    Renderer s_renderer = Renderer(nullptr, destroy_renderer);
    SDL_AudioDeviceID s_audioDevice{}; // You can't really wrap this in a unique_ptr.

    // SDL_image flags.
    constexpr int SDL_IMAGE_FLAGS = IMG_INIT_JPG | IMG_INIT_PNG;
} // namespace

bool sdl::initialize(const char *windowTitle, int windowWidth, int windowHeight)
{
    static constexpr uint32_t SDL_INIT_FLAGS   = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER;
    static constexpr SDL_AudioSpec AUDIO_SPECS = {.freq     = 11025,
                                                  .format   = AUDIO_U8,
                                                  .channels = 2,
                                                  .silence  = 0,
                                                  .samples  = 256};

    auto fail_init = [](const char *stage)
    {
        sdl::error::sdl(-1);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s failed: %s", stage, SDL_GetError());
        sdl::exit();
        return false;
    };

    if (SDL_Init(SDL_INIT_FLAGS) != 0) { return fail_init("SDL_Init"); }

    SDL_Window *tempWindow = SDL_CreateWindow(windowTitle, 0, 0, windowWidth, windowHeight, SDL_WINDOW_SHOWN);
    if (!tempWindow) { return fail_init("SDL_CreateWindow"); }
    s_window.reset(tempWindow);

    // Keep hardware acceleration if only vsync is unavailable. JKSV's UI needs target textures in every mode.
    static constexpr Uint32 RENDERER_FLAGS[] = {
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE,
        SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE};

    for (Uint32 flags : RENDERER_FLAGS)
    {
        SDL_ClearError();
        Renderer candidate(SDL_CreateRenderer(s_window.get(), -1, flags), destroy_renderer);
        SDL_RendererInfo info{};
        bool usable = candidate && SDL_GetRendererInfo(candidate.get(), &info) == 0;
        if (usable && !SDL_RenderTargetSupported(candidate.get()))
        {
            SDL_SetError("Renderer %s does not support target textures", info.name ? info.name : "unknown");
            usable = false;
        }
        if (!usable)
        {
            sdl::error::sdl(-1);
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "SDL renderer attempt flags=0x%X failed: %s",
                        static_cast<unsigned int>(flags),
                        SDL_GetError());
            continue;
        }

        SDL_Log("SDL renderer selected: %s (flags=0x%X)",
                info.name ? info.name : "unknown",
                static_cast<unsigned int>(info.flags));
        s_renderer = std::move(candidate);
        s_activeRenderer = s_renderer.get();
        if (++s_nextRendererGeneration == 0) { ++s_nextRendererGeneration; }
        s_rendererGeneration = s_nextRendererGeneration;
        break;
    }
    if (!s_renderer)
    {
        sdl::exit();
        return false;
    }

    s_audioDevice = SDL_OpenAudioDevice(nullptr, 0, &AUDIO_SPECS, nullptr, 0);
    if (s_audioDevice == 0) { return fail_init("SDL_OpenAudioDevice"); }

    int imgError = IMG_Init(SDL_IMAGE_FLAGS);
    if ((imgError & SDL_IMAGE_FLAGS) != SDL_IMAGE_FLAGS) { return fail_init("IMG_Init"); }

    // This makes the icons in JKSV look nicer when I scale them, but I'm not going to fatal for it failing.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");

    // This isn't fatal, but I'll record it
    sdl::error::sdl(SDL_SetRenderDrawBlendMode(s_renderer.get(), SDL_BLENDMODE_BLEND));

    return true;
}

void sdl::exit()
{
    if (s_audioDevice != 0)
    {
        SDL_CloseAudioDevice(s_audioDevice);
        s_audioDevice = 0;
    }
    s_renderer.reset();
    s_window.reset();
    IMG_Quit();
    SDL_Quit();
}

SDL_Renderer *sdl::get_renderer() noexcept { return s_renderer.get(); }

uint64_t sdl::get_renderer_generation() noexcept { return s_rendererGeneration; }

SDL_AudioDeviceID sdl::get_audio_device() noexcept { return s_audioDevice; }

bool sdl::frame_begin(sdl::Color color) noexcept
{
    // Set render target back to framebuffer.
    if (!sdl::set_render_target(sdl::Texture::Null) || !sdl::set_render_draw_color(color)) { return false; }
    SDL_Renderer *renderer = s_renderer.get();

    if (sdl::error::sdl(SDL_RenderClear(renderer))) { return false; }
    return true;
}

void sdl::frame_end() noexcept { SDL_RenderPresent(s_renderer.get()); }

bool sdl::set_render_target(sdl::SharedTexture &target) noexcept
{
    SDL_Renderer *renderer     = s_renderer.get();
    SDL_Texture *targetPointer = target->get();
    if (sdl::error::sdl(SDL_SetRenderTarget(renderer, targetPointer))) { return false; }
    return true;
}

bool sdl::set_render_draw_color(sdl::Color color) noexcept
{
    SDL_Renderer *renderer = s_renderer.get();
    const uint8_t red      = sdl::color::get_red(color);
    const uint8_t green    = sdl::color::get_green(color);
    const uint8_t blue     = sdl::color::get_blue(color);
    const uint8_t alpha    = sdl::color::get_alpha(color);

    if (sdl::error::sdl(SDL_SetRenderDrawColor(renderer, red, green, blue, alpha))) { return false; }
    return true;
}
bool sdl::render_line(sdl::SharedTexture &target, int xA, int yA, int xB, int yB, sdl::Color color) noexcept
{
    if (!sdl::set_render_target(target) || !sdl::set_render_draw_color(color)) { return false; }

    SDL_Renderer *renderer = s_renderer.get();
    if (sdl::error::sdl(SDL_RenderDrawLine(renderer, xA, yA, xB, yB))) { return false; };
    return true;
}

bool sdl::render_rect(sdl::SharedTexture &target, int x, int y, int width, int height, sdl::Color color) noexcept
{
    if (!sdl::set_render_target(target) || !sdl::set_render_draw_color(color)) { return false; }

    SDL_Renderer *renderer = s_renderer.get();
    const SDL_Rect rect    = {.x = x, .y = y, .w = width, .h = height};
    if (sdl::error::sdl(SDL_RenderDrawRect(renderer, &rect))) { return false; }
    return true;
}

bool sdl::render_rect_fill(sdl::SharedTexture &target, int x, int y, int width, int height, sdl::Color color) noexcept
{
    if (!sdl::set_render_target(target) || !sdl::set_render_draw_color(color)) { return false; }

    SDL_Renderer *renderer = s_renderer.get();
    const SDL_Rect rect    = {.x = x, .y = y, .w = width, .h = height};
    if (sdl::error::sdl(SDL_RenderFillRect(renderer, &rect))) { return false; }
    return true;
}
