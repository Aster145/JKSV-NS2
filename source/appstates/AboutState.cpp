#include "appstates/AboutState.hpp"

#include "graphics/colors.hpp"
#include "input.hpp"
#include "strings/strings.hpp"
#include "sys/startup.hpp"

#include <string>

namespace
{
    enum TextIndex
    {
        TITLE,
        VERSION,
        ORIGINAL_AUTHOR,
        NS2_MAINTAINER,
        BILIBILI_ACCOUNT,
        ORIGINAL_PROJECT,
        CREDITS_TITLE,
        CREDITS,
        LICENSE,
        CONTROL_GUIDE,
        LOG_LABEL,
        LOG_UNAVAILABLE
    };

    const char *page_text(int index) { return strings::get_by_name(strings::names::ABOUT_PAGE, index); }
} // namespace

AboutState::AboutState()
    : m_renderTarget(sdl::TextureManager::load("AboutPageTarget", 1080, 555, SDL_TEXTUREACCESS_TARGET))
    , m_controlGuide(ui::ControlGuide::create(page_text(CONTROL_GUIDE)))
    , m_logInfo(ui::TextScroll::create("", 24, 505, 1016, 30, 18, colors::WHITE, colors::CLEAR_COLOR, false))
{
    refresh_log_info();
}

void AboutState::update()
{
    const bool hasFocus = BaseState::has_focus();
    m_controlGuide->update(hasFocus);
    m_logInfo->update(hasFocus);
    refresh_log_info();
    if (hasFocus && input::button_pressed(HidNpadButton_B)) { BaseState::deactivate(); }
}

void AboutState::sub_update() { m_controlGuide->sub_update(); }

void AboutState::render()
{
    const bool hasFocus = BaseState::has_focus();
    m_renderTarget->clear(colors::CLEAR_COLOR);
    sdl::text::render(m_renderTarget, 32, 18, 26, 1000, colors::WHITE, page_text(TITLE));
    sdl::text::render(m_renderTarget, 32, 64, 32, 1000, colors::BLUE_GREEN, "JKSV-NS2");
    sdl::text::render(m_renderTarget, 390, 74, 22, 640, colors::WHITE, page_text(VERSION));
    sdl::render_line(m_renderTarget, 32, 118, 1032, 118, colors::DIV_COLOR);

    sdl::text::render(m_renderTarget, 32, 146, 24, 1000, colors::WHITE, page_text(ORIGINAL_AUTHOR));
    sdl::text::render(m_renderTarget, 32, 190, 24, 1000, colors::WHITE, page_text(NS2_MAINTAINER));
    sdl::text::render(m_renderTarget, 32, 234, 24, 1000, colors::WHITE, page_text(BILIBILI_ACCOUNT));
    sdl::text::render(m_renderTarget, 32, 278, 22, 1000, colors::WHITE, page_text(ORIGINAL_PROJECT));
    sdl::text::render(m_renderTarget, 32, 330, 22, 1000, colors::BLUE_GREEN, page_text(CREDITS_TITLE));
    sdl::text::render(m_renderTarget, 32, 365, 19, 1000, colors::WHITE, page_text(CREDITS));
    sdl::text::render(m_renderTarget, 32, 448, 18, 1000, colors::WHITE, page_text(LICENSE));
    m_logInfo->render(m_renderTarget, hasFocus);

    m_renderTarget->render(sdl::Texture::Null, 201, 91);
    m_controlGuide->render(sdl::Texture::Null, hasFocus);
}

void AboutState::refresh_log_info()
{
    const sys::LogInfo info = sys::get_log_info();
    const std::string message = info.status == sys::LogStatus::Ready
                                    ? std::string{page_text(LOG_LABEL)} + info.logPath
                                    : std::string{page_text(LOG_UNAVAILABLE)};
    if (m_logInfo->get_text() != message) { m_logInfo->set_text(message, false); }
}
