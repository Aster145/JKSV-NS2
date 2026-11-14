#pragma once
#include "StateManager.hpp"
#include "appstates/BaseState.hpp"
#include "sdl.hpp"
#include "ui/ControlGuide.hpp"
#include "ui/TextScroll.hpp"

#include <memory>
#include <string>

/// @brief FTP is active only while this page owns the foreground.
class FtpState final : public BaseState
{
    public:
        FtpState();
        ~FtpState() override;

        static std::shared_ptr<FtpState> create_and_push()
        {
            auto state = std::make_shared<FtpState>();
            StateManager::push_state(state);
            return state;
        }

        void update() override;
        void sub_update() override;
        void render() override;

    private:
        sdl::SharedTexture m_renderTarget{};
        std::shared_ptr<ui::ControlGuide> m_controlGuide{};
        std::shared_ptr<ui::TextScroll> m_detailScroll{};
        bool m_serviceOwned{};
        bool m_running{};
        std::string m_address{};

        void start_service();
        void refresh_status();
        void close_page();
};
