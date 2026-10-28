// SPDX-License-Identifier: GPL-3.0-only
// The app's one screen: the host and its pairing requests on the left, the settings on the right,
// and an on-screen keyboard for PINs, the host name and numbers. Drawn with ps5-homebrew-ui in its
// 1920x1080 virtual canvas.
//
// Controls: up/down move, L1/R1 (or left/right where nothing changes) switch panels, Cross acts,
// left/right change a setting, Square puts a setting back to the host's default.
#pragma once

#include "app/psapp/host.hpp"
#include "core/input.hpp"
#include "gfx/draw_list.hpp"
#include "ui/components/component.hpp"
#include "ui/components/keyboard.hpp"
#include "ui/feedback.hpp"
#include "ui/fonts.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace psapp
{

class Screen
{
  public:
    Screen(Host &host, const hui::ui::Fonts &fonts);

    void update(const hui::InputFrame &input, float dt, hui::ui::Feedback &feedback);
    void draw(hui::gfx::DrawList &list, std::uint32_t glass) const;
    const std::string &version() const
    {
        return version_;
    }
    void set_version(std::string version)
    {
        version_ = std::move(version);
    }

  private:
    enum class RowKind
    {
        start_stop,
        restart,
        autoload,
        pairing,
        option,
        save,
        save_restart,
    };
    struct Row
    {
        RowKind kind;
        int index = 0; // the pairing or the option
        hui::gfx::Rect rect;
    };
    enum class Overlay
    {
        none,
        pin,
        text,
        number,
    };

    void layout();
    void handle_main(const hui::InputFrame &input, hui::ui::Feedback &feedback);
    void handle_overlay(const hui::InputFrame &input, hui::ui::Feedback &feedback);
    void activate(const Row &row, hui::ui::Feedback &feedback);
    void step_option(int index, int direction, hui::ui::Feedback &feedback);
    void open_overlay(Overlay kind, const std::string &title, const std::string &initial);
    void close_overlay();
    bool busy() const
    {
        return !snap_.busy.empty();
    }
    bool dirty() const;
    std::string option_text(int index) const;

    void draw_status(hui::gfx::DrawList &list, const hui::gfx::Rect &panel) const;
    void draw_row(hui::ui::Canvas &canvas, const Row &row, bool focused) const;
    void draw_overlay(hui::ui::Canvas &canvas) const;

    Host &host_;
    const hui::ui::Fonts &fonts_;
    Snapshot snap_;
    Values edits_;
    bool edits_loaded_ = false;
    bool reload_edits_ = false; // the next outcome replaces the edits with the files
    std::uint64_t seen_message_ = 0;
    float message_age_ = 100.0f;
    int armed_ = -1;          // the RowKind whose first press is waiting for a second one
    float armed_age_ = 100.0f;
    std::string version_;
    float time_ = 0.0f;

    std::vector<Row> left_;
    std::vector<Row> right_;
    int column_ = 0;
    int focus_[2] = {0, 0};
    hui::gfx::Rect left_panel_;
    hui::gfx::Rect right_panel_;
    float pairing_header_y_ = 0.0f;
    std::size_t hidden_pairings_ = 0;

    hui::ui::ComponentStyle style_;
    hui::ui::Highlight highlight_;
    bool highlight_placed_ = false;

    Overlay overlay_ = Overlay::none;
    std::string overlay_title_;
    std::string buffer_;
    std::string overlay_error_;
    std::string target_; // the pairing id or the option key
    std::string target_name_;
    hui::ui::Keyboard keyboard_;
};

} // namespace psapp
