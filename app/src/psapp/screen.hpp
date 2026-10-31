// SPDX-License-Identifier: GPL-3.0-only
// The app's screen, drawn with ps5-homebrew-ui in its 1920x1080 virtual canvas. Three pages under a
// tab bar (L1/R1):
//   Home      a hero with the host's state and the sun emblem, the host's actions (start or stop,
//             restart, start at boot), pairing requests as cards (Cross: the PIN) and the paired
//             devices;
//   Settings  one tile per sunshine.conf option (Cross opens its editor, Square puts it back to the
//             host's default), then Save, Save and restart, and Revert;
//   Diagnostics the report for a bug report, to read here, as a file and over the network.
// Editors and the PIN come up as a sheet over the page: a list for choices, a stepper for numbers,
// the on-screen keyboard for text and the PIN. The page is frosted behind cards and sheets (the
// kit's glass), and focus, pages, sheets and messages move with springs and fades.
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
    // Three layers, drawn in this order with a glass capture after the first and (when has_sheet())
    // after the second: the light behind the page, the page, the sheet over it.
    void draw_background(hui::gfx::DrawList &list) const;
    void draw_page(hui::gfx::DrawList &list, std::uint32_t glass) const;
    void draw_sheet(hui::gfx::DrawList &list, std::uint32_t glass) const;
    bool has_sheet() const
    {
        return sheet_amount_ > 0.01f;
    }
    void set_version(std::string version)
    {
        version_ = std::move(version);
    }

  private:
    enum class Page
    {
        home,
        settings,
        diagnostics,
    };
    enum class Sheet
    {
        none,
        pin,
        text,
        choice,
        number,
    };
    enum class Action
    {
        start_stop,
        restart,
        autoload,
        save,
        save_restart,
        revert,
    };

    // Input.
    void switch_page(Page page, hui::ui::Feedback &feedback);
    void handle_home(const hui::InputFrame &input, hui::ui::Feedback &feedback);
    void handle_settings(const hui::InputFrame &input, hui::ui::Feedback &feedback);
    void handle_diagnostics(const hui::InputFrame &input, hui::ui::Feedback &feedback);
    void handle_sheet(const hui::InputFrame &input, hui::ui::Feedback &feedback);
    void run(Action action, hui::ui::Feedback &feedback);
    void open_tile(int index);
    void open_sheet(Sheet kind, const std::string &title, const std::string &help, const std::string &initial);
    void close_sheet();

    // State helpers.
    bool busy() const
    {
        return !snap_.busy.empty();
    }
    bool dirty() const;
    // A client is connected: the host streams. A host that reports its client (client=) and names
    // none keeps the app open for a client that left: a paused session, not a stream.
    bool live() const
    {
        return snap_.streaming && !(snap_.client_known && snap_.client.empty());
    }
    std::string value_text(int index) const;
    hui::gfx::Rect focus_rect() const;
    hui::gfx::Rect action_rect(int index) const;
    hui::gfx::Rect pairing_rect(int index) const;
    hui::gfx::Rect tile_rect(int index) const;
    hui::gfx::Rect settings_action_rect(int index) const;
    int visible_pairings() const;
    float appear(int order) const; // 0..1, the staggered entrance of the page's parts

    // Drawing.
    void draw_top_bar(hui::gfx::DrawList &list, std::uint32_t glass) const;
    void draw_home(hui::gfx::DrawList &list, std::uint32_t glass) const;
    void draw_settings(hui::gfx::DrawList &list, std::uint32_t glass) const;
    void draw_diagnostics(hui::gfx::DrawList &list, std::uint32_t glass) const;
    void draw_button(hui::gfx::DrawList &list, std::uint32_t glass, const hui::gfx::Rect &r, int icon,
                     const std::string &label, bool primary, float focus, bool enabled) const;
    void draw_footer(hui::gfx::DrawList &list, std::uint32_t glass) const;
    void glass_card(hui::gfx::DrawList &list, std::uint32_t glass, const hui::gfx::Rect &r, float radius,
                    float focus) const;

    Host &host_;
    const hui::ui::Fonts &fonts_;
    Snapshot snap_;
    Values edits_;
    bool edits_loaded_ = false;
    bool reload_edits_ = false; // the next outcome replaces the edits with the files
    std::uint64_t seen_message_ = 0;
    std::size_t seen_pairings_ = 0;
    float message_age_ = 100.0f;
    int armed_ = -1; // the Action whose first press waits for a second one (it ends a stream)
    float armed_age_ = 100.0f;
    std::string version_;
    float time_ = 0.0f;
    std::uint32_t blocked_ = 0; // buttons held since the start, ignored until let go
    bool blocked_logged_ = false;

    // Pages and focus.
    Page page_ = Page::home;
    float page_age_ = 0.0f; // seconds since the page appeared
    int page_direction_ = 1;
    int home_row_ = 0; // 0 the actions, 1 the pairing cards
    int home_action_ = 0;
    int home_card_ = 0;
    int tile_ = 0; // 0..5 the tiles, 6..8 the actions under them
    float tile_amount_[9] = {};
    float action_amount_[3] = {};
    float card_amount_[2] = {};
    float tab_x_ = -1.0f; // the selected tab's plate glides between tabs
    hui::ui::ComponentStyle style_;
    hui::ui::Highlight highlight_;
    bool snap_highlight_ = true;
    float sun_spin_ = 0.0f;
    float sun_glow_ = 0.0f;
    float sun_dim_ = 1.0f;
    float pulse_ = 0.0f; // the stream's rings

    // The report, split into lines for the diagnostics page.
    std::vector<std::string> report_lines_;
    std::uint64_t seen_report_ = 0;
    int report_scroll_ = 0;
    float report_amount_ = 0.0f; // its button's focus

    // The sheet.
    Sheet sheet_ = Sheet::none;
    Sheet sheet_drawn_ = Sheet::none; // stays while the sheet fades out
    float sheet_amount_ = 0.0f;
    std::string sheet_title_;
    std::string sheet_help_;
    std::string buffer_;
    std::string sheet_error_;
    std::string target_; // the pairing id or the option key
    std::string target_name_;
    int choice_ = 0; // the focused row of a choice sheet (0: the default)
    long number_ = 0;
    bool number_default_ = false;
    hui::ui::Keyboard keyboard_;
    hui::ui::Highlight sheet_highlight_;
};

} // namespace psapp
