#pragma once
#include "StateManager.hpp"
#include "appstates/BaseState.hpp"
#include "sdl.hpp"
#include "ui/ControlGuide.hpp"
#include "ui/TextScroll.hpp"

#include <memory>

class AboutState final : public BaseState
{
    public:
        AboutState();

        static std::shared_ptr<AboutState> create_and_push()
        {
            auto state = std::make_shared<AboutState>();
            StateManager::push_state(state);
            return state;
        }

        void update() override;
        void sub_update() override;
        void render() override;

    private:
        sdl::SharedTexture m_renderTarget{};
        std::shared_ptr<ui::ControlGuide> m_controlGuide{};
        std::shared_ptr<ui::TextScroll> m_logInfo{};

        void refresh_log_info();
};
