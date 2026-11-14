#include "appstates/FtpState.hpp"

#include "ftp/server.hpp"
#include "graphics/colors.hpp"
#include "input.hpp"
#include "strings/strings.hpp"

namespace
{
    enum TextIndex
    {
        TITLE,
        DESCRIPTION,
        STATUS_LABEL,
        RUNNING,
        STOPPED,
        ADDRESS_LABEL,
        ADDRESS_UNAVAILABLE,
        DETAILS_LABEL,
        FOREGROUND_NOTICE,
        CONTROL_GUIDE
    };

    const char *page_text(int index) { return strings::get_by_name(strings::names::FTP_PAGE, index); }
} // namespace

FtpState::FtpState()
    : BaseState(false)
    , m_renderTarget(sdl::TextureManager::load("FtpPageTarget", 1080, 555, SDL_TEXTUREACCESS_TARGET))
    , m_controlGuide(ui::ControlGuide::create(page_text(CONTROL_GUIDE)))
    , m_detailScroll(ui::TextScroll::create("", 24, 350, 1016, 34, 20, colors::WHITE, colors::CLEAR_COLOR, false))
{
    start_service();
}

FtpState::~FtpState() { ftp::stop(); }

void FtpState::update()
{
    const bool hasFocus = BaseState::has_focus();
    m_controlGuide->update(hasFocus);
    m_detailScroll->update(hasFocus);
    if (!hasFocus) { return; }

    if (input::button_pressed(HidNpadButton_B))
    {
        close_page();
        return;
    }

    refresh_status();
    if (!m_running && input::button_pressed(HidNpadButton_A)) { start_service(); }
}

void FtpState::sub_update()
{
    m_controlGuide->sub_update();
    if (!BaseState::has_focus() && m_serviceOwned)
    {
        ftp::stop();
        m_serviceOwned = false;
        refresh_status();
    }
}

void FtpState::render()
{
    const bool hasFocus = BaseState::has_focus();
    m_renderTarget->clear(colors::CLEAR_COLOR);
    sdl::text::render(m_renderTarget, 32, 20, 30, 1000, colors::WHITE, page_text(TITLE));
    sdl::text::render(m_renderTarget, 32, 72, 22, 1000, colors::WHITE, page_text(DESCRIPTION));
    sdl::render_line(m_renderTarget, 32, 126, 1032, 126, colors::DIV_COLOR);

    sdl::text::render(m_renderTarget, 32, 154, 21, 1000, colors::WHITE, page_text(STATUS_LABEL));
    sdl::text::render(m_renderTarget,
                      32,
                      190,
                      26,
                      1000,
                      m_running ? colors::BLUE_GREEN : colors::YELLOW,
                      page_text(m_running ? RUNNING : STOPPED));
    sdl::text::render(m_renderTarget, 32, 242, 21, 1000, colors::WHITE, page_text(ADDRESS_LABEL));
    const std::string_view address = m_address.empty() ? std::string_view{page_text(ADDRESS_UNAVAILABLE)} : m_address;
    sdl::text::render(m_renderTarget, 32, 276, 28, 1000, colors::BLUE_GREEN, address);
    sdl::text::render(m_renderTarget, 32, 324, 19, 1000, colors::WHITE, page_text(DETAILS_LABEL));
    m_detailScroll->render(m_renderTarget, hasFocus);
    sdl::render_line(m_renderTarget, 32, 408, 1032, 408, colors::DIV_COLOR);
    sdl::text::render(m_renderTarget, 32, 438, 21, 1000, colors::WHITE, page_text(FOREGROUND_NOTICE));

    m_renderTarget->render(sdl::Texture::Null, 201, 91);
    m_controlGuide->render(sdl::Texture::Null, hasFocus);
}

void FtpState::start_service()
{
    ftp::stop();
    m_serviceOwned = true;
    ftp::start();
    refresh_status();
}

void FtpState::refresh_status()
{
    m_running = ftp::running();
    m_address = ftp::address();
    const std::string status = ftp::status();
    if (m_detailScroll->get_text() != status) { m_detailScroll->set_text(status, false); }
}

void FtpState::close_page()
{
    // Complete the stop before returning control to any save-management page.
    ftp::stop();
    m_serviceOwned = false;
    m_running = false;
    BaseState::deactivate();
}
