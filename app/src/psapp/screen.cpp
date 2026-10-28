// SPDX-License-Identifier: GPL-3.0-only
#include "app/psapp/screen.hpp"

#include "ui/glyphs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace psapp
{

using hui::Action;
using hui::Direction;
using hui::gfx::Align;
using hui::gfx::Color;
using hui::gfx::Rect;

namespace
{

// A dark night sky with a warm sun: the palette of the whole screen.
const Color kInk = Color::rgb(0xf4f1ea);
const Color kMuted = Color::rgb(0xa9b0c7);
const Color kFaint = Color::rgb(0x6c7391);
const Color kPanel = Color::rgb(0x121a33, 0.82f);
const Color kRow = Color::rgb(0x1b2546, 0.9f);
const Color kSun = Color::rgb(0xf5a524);
const Color kGood = Color::rgb(0x3ecf8e);
const Color kBad = Color::rgb(0xff6b6b);
const Color kIdle = Color::rgb(0x8a91a8);

constexpr float kRowHeight = 72.0f;
constexpr float kRowGap = 8.0f;
constexpr float kMessageSeconds = 7.0f;
constexpr std::size_t kMaxPairingRows = 3;
constexpr float kArmSeconds = 4.0f;

std::string shorten(const hui::ui::FontRef &font, const std::string &text, float size, float width)
{
    if (font.measure(text, size) <= width)
        return text;
    return font.font->fit(text + " \xE2\x80\xA6", size, width);
}

} // namespace

Screen::Screen(Host &host, const hui::ui::Fonts &fonts) : host_(host), fonts_(fonts)
{
    style_.theme.focus = kSun.with_alpha(0.35f);
    style_.theme.primary = kSun;
    keyboard_.style.theme = style_.theme;
    keyboard_.style.theme.primary = kSun;
    keyboard_.style.theme.on_primary = Color::rgb(0x1a1205);
    keyboard_.style.bindings = hui::ui::KeyboardBindings::standard();
}

bool Screen::dirty() const
{
    return edits_loaded_ && edits_ != snap_.config;
}

std::string Screen::option_text(int index) const
{
    const OptionSpec &spec = option_specs()[static_cast<std::size_t>(index)];
    const auto it = edits_.find(spec.key);
    if (it == edits_.end() || !it->second)
        return std::string("Default (") + spec.default_label + ")";
    const std::string &value = *it->second;
    if (spec.kind == OptionKind::choice)
    {
        for (const Choice &choice : spec.choices)
        {
            if (value == choice.value)
                return choice.label;
        }
    }
    return value;
}

// ---- layout -----------------------------------------------------------------------------------

void Screen::layout()
{
    left_panel_ = {96.0f, 172.0f, 760.0f, 728.0f};
    right_panel_ = {904.0f, 172.0f, 920.0f, 728.0f};
    left_.clear();
    right_.clear();

    const float lx = left_panel_.x + 24.0f;
    const float lw = left_panel_.w - 48.0f;
    float y = left_panel_.y + 64.0f + 150.0f; // title, then the status block
    for (RowKind kind : {RowKind::start_stop, RowKind::restart, RowKind::autoload})
    {
        left_.push_back({kind, 0, {lx, y, lw, kRowHeight}});
        y += kRowHeight + kRowGap;
    }
    pairing_header_y_ = y + 34.0f;
    y += 52.0f;
    const std::size_t shown = std::min(snap_.pairings.size(), kMaxPairingRows);
    hidden_pairings_ = snap_.pairings.size() - shown;
    for (std::size_t i = 0; i < shown; ++i)
    {
        left_.push_back({RowKind::pairing, static_cast<int>(i), {lx, y, lw, kRowHeight}});
        y += kRowHeight + kRowGap;
    }

    const float rx = right_panel_.x + 24.0f;
    const float rw = right_panel_.w - 48.0f;
    y = right_panel_.y + 64.0f;
    for (std::size_t i = 0; i < option_specs().size(); ++i)
    {
        right_.push_back({RowKind::option, static_cast<int>(i), {rx, y, rw, kRowHeight}});
        y += kRowHeight + kRowGap;
    }
    y += 8.0f;
    const float half = (rw - 16.0f) / 2.0f;
    right_.push_back({RowKind::save, 0, {rx, y, half, kRowHeight}});
    right_.push_back({RowKind::save_restart, 0, {rx + half + 16.0f, y, half, kRowHeight}});

    for (int c = 0; c < 2; ++c)
    {
        const int count = static_cast<int>(c == 0 ? left_.size() : right_.size());
        focus_[c] = std::clamp(focus_[c], 0, count - 1);
    }
}

// ---- input ------------------------------------------------------------------------------------

void Screen::update(const hui::InputFrame &input, float dt, hui::ui::Feedback &feedback)
{
    time_ += dt;
    message_age_ += dt;
    armed_age_ += dt;
    snap_ = host_.snapshot();
    if (snap_.message_serial != seen_message_)
    {
        seen_message_ = snap_.message_serial;
        message_age_ = 0.0f;
        feedback.play(snap_.message_ok ? hui::audio::Cue::complete : hui::audio::Cue::error);
        // After a save or a reload the screen shows the files again; other outcomes keep unsaved edits.
        if (reload_edits_)
            edits_loaded_ = false;
        reload_edits_ = false;
    }
    if (!edits_loaded_ && snap_.files_read && snap_.busy.empty())
    {
        edits_ = snap_.config;
        edits_loaded_ = true;
    }
    layout();

    if (overlay_ != Overlay::none)
        handle_overlay(input, feedback);
    else
        handle_main(input, feedback);

    const std::vector<Row> &rows = column_ == 0 ? left_ : right_;
    const Rect target = rows[static_cast<std::size_t>(focus_[column_])].rect;
    if (!highlight_placed_)
    {
        highlight_.snap(target);
        highlight_placed_ = true;
    }
    highlight_.target(target);
    highlight_.update(dt, style_);
    keyboard_.update(dt);
}

void Screen::handle_main(const hui::InputFrame &input, hui::ui::Feedback &feedback)
{
    std::vector<Row> &rows = column_ == 0 ? left_ : right_;
    Row &row = rows[static_cast<std::size_t>(focus_[column_])];
    const bool value_row = row.kind == RowKind::option &&
                           option_specs()[static_cast<std::size_t>(row.index)].kind != OptionKind::text;

    auto switch_column = [&](int to) {
        if (to == column_)
            return;
        // Land on the row nearest in height.
        const float y = row.rect.cy();
        const std::vector<Row> &other = to == 0 ? left_ : right_;
        int best = 0;
        for (std::size_t i = 0; i < other.size(); ++i)
        {
            if (std::fabs(other[i].rect.cy() - y) < std::fabs(other[static_cast<std::size_t>(best)].rect.cy() - y))
                best = static_cast<int>(i);
        }
        column_ = to;
        focus_[to] = best;
        feedback.play(hui::audio::Cue::tab);
    };

    if (input.is_pressed(Action::page_prev))
        switch_column(0);
    else if (input.is_pressed(Action::page_next))
        switch_column(1);

    switch (input.nav)
    {
    case Direction::up:
    case Direction::down: {
        const int step = input.nav == Direction::up ? -1 : 1;
        int next = focus_[column_] + step;
        // Save and "Save and restart" sit side by side: up from either goes to the last option.
        if (column_ == 1 && step < 0 && row.kind == RowKind::save_restart)
            next = focus_[column_] - 2;
        if (column_ == 1 && step > 0 && row.kind == RowKind::save)
            next = -1;
        if (next < 0 || next >= static_cast<int>(rows.size()))
        {
            if (!input.nav_repeat)
                feedback.play(hui::audio::Cue::error, 1.0f, 0.0f, 0.4f);
            break;
        }
        focus_[column_] = next;
        feedback.play(hui::audio::Cue::focus);
        break;
    }
    case Direction::left:
    case Direction::right: {
        const int direction = input.nav == Direction::left ? -1 : 1;
        if (value_row)
            step_option(row.index, direction, feedback);
        else if (column_ == 1 && row.kind == RowKind::save && direction > 0)
        {
            ++focus_[1];
            feedback.play(hui::audio::Cue::focus);
        }
        else if (column_ == 1 && row.kind == RowKind::save_restart && direction < 0)
        {
            --focus_[1];
            feedback.play(hui::audio::Cue::focus);
        }
        else
            switch_column(direction < 0 ? 0 : 1);
        break;
    }
    default:
        break;
    }

    Row &current = (column_ == 0 ? left_ : right_)[static_cast<std::size_t>(focus_[column_])];
    if (input.is_pressed(Action::confirm))
        activate(current, feedback);
    if (input.is_pressed(Action::west) && current.kind == RowKind::option)
    {
        const OptionSpec &spec = option_specs()[static_cast<std::size_t>(current.index)];
        if (edits_[spec.key])
        {
            edits_[spec.key] = std::nullopt;
            feedback.play(hui::audio::Cue::toggle);
        }
    }
    if (input.is_pressed(Action::north))
    {
        host_.refresh();
        reload_edits_ = true;
        feedback.play(hui::audio::Cue::select);
    }
}

void Screen::step_option(int index, int direction, hui::ui::Feedback &feedback)
{
    const OptionSpec &spec = option_specs()[static_cast<std::size_t>(index)];
    std::optional<std::string> &value = edits_[spec.key];
    if (spec.kind == OptionKind::choice)
    {
        // Default, then each choice in order.
        int position = 0;
        for (std::size_t i = 0; value && i < spec.choices.size(); ++i)
        {
            if (*value == spec.choices[i].value)
                position = static_cast<int>(i) + 1;
        }
        const int count = static_cast<int>(spec.choices.size()) + 1;
        position = (position + direction + count) % count;
        value = position == 0 ? std::nullopt
                              : std::optional<std::string>(spec.choices[static_cast<std::size_t>(position - 1)].value);
        feedback.play(hui::audio::Cue::slider);
        return;
    }
    long number = spec.fallback;
    if (value)
        number = std::strtol(value->c_str(), nullptr, 10);
    else
        direction = 0; // the first step shows the default as a number
    number = std::clamp(number + direction * spec.step, spec.min, spec.max);
    value = std::to_string(number);
    feedback.play(hui::audio::Cue::slider);
}

void Screen::activate(const Row &row, hui::ui::Feedback &feedback)
{
    const bool needs_worker = row.kind != RowKind::option && row.kind != RowKind::pairing;
    if (needs_worker && busy())
    {
        feedback.play(hui::audio::Cue::error);
        return;
    }
    // Stopping the host ends a running stream, and a player streaming this screen may be the one
    // pressing: such a press has to come twice.
    const bool ends_stream = snap_.streaming && ((row.kind == RowKind::start_stop && snap_.running) ||
                                                 row.kind == RowKind::restart || row.kind == RowKind::save_restart);
    if (ends_stream && (armed_ != static_cast<int>(row.kind) || armed_age_ > kArmSeconds))
    {
        armed_ = static_cast<int>(row.kind);
        armed_age_ = 0.0f;
        feedback.play(hui::audio::Cue::notify);
        return;
    }
    armed_ = -1;
    switch (row.kind)
    {
    case RowKind::start_stop:
        if (snap_.running)
            host_.stop_host();
        else
            host_.start_host();
        break;
    case RowKind::restart:
        host_.restart_host();
        break;
    case RowKind::autoload:
        host_.set_autoload(!snap_.autoload);
        break;
    case RowKind::pairing: {
        const Pairing &p = snap_.pairings[static_cast<std::size_t>(row.index)];
        target_ = p.id;
        target_name_ = p.name;
        open_overlay(Overlay::pin, "Pair " + (p.name.empty() ? std::string("Moonlight") : p.name), "");
        break;
    }
    case RowKind::option: {
        const OptionSpec &spec = option_specs()[static_cast<std::size_t>(row.index)];
        if (spec.kind == OptionKind::choice)
        {
            step_option(row.index, 1, feedback);
            return;
        }
        target_ = spec.key;
        const auto &value = edits_[spec.key];
        open_overlay(spec.kind == OptionKind::text ? Overlay::text : Overlay::number, spec.label,
                     value ? *value : std::string());
        break;
    }
    case RowKind::save:
    case RowKind::save_restart:
        host_.save_config(edits_, row.kind == RowKind::save_restart);
        reload_edits_ = true;
        break;
    }
    feedback.play(hui::audio::Cue::select);
}

void Screen::open_overlay(Overlay kind, const std::string &title, const std::string &initial)
{
    overlay_ = kind;
    overlay_title_ = title;
    buffer_ = initial;
    overlay_error_.clear();
    if (kind == Overlay::text)
    {
        keyboard_.set_layouts({hui::ui::KeyboardLayout::letters(), hui::ui::KeyboardLayout::symbols()});
        keyboard_.style.max_length = 63;
        keyboard_.set_bounds({480.0f, 470.0f, 960.0f, 400.0f});
    }
    else
    {
        keyboard_.set_layouts({hui::ui::KeyboardLayout::numeric()});
        keyboard_.style.max_length = kind == Overlay::pin ? 4 : 5;
        keyboard_.set_bounds({710.0f, 470.0f, 500.0f, 400.0f});
    }
    keyboard_.set_length(static_cast<int>(buffer_.size()));
    keyboard_.set_focus(0, 0);
    keyboard_.enter();
}

void Screen::close_overlay()
{
    overlay_ = Overlay::none;
}

void Screen::handle_overlay(const hui::InputFrame &input, hui::ui::Feedback &feedback)
{
    const hui::ui::Event event = keyboard_.handle(input, feedback);
    for (int i = 0; i < keyboard_.erased() && !buffer_.empty(); ++i)
        buffer_.pop_back();
    buffer_ += keyboard_.typed();
    keyboard_.set_length(static_cast<int>(buffer_.size()));
    if (!keyboard_.typed().empty() || keyboard_.erased() > 0)
        overlay_error_.clear();

    if (event == hui::ui::Event::cancelled)
    {
        close_overlay();
        return;
    }
    if (event != hui::ui::Event::activated && !input.is_pressed(Action::menu))
        return;

    if (overlay_ == Overlay::pin)
    {
        if (buffer_.size() != 4)
        {
            overlay_error_ = "The PIN has four digits";
            feedback.play(hui::audio::Cue::error);
            return;
        }
        host_.submit_pin(target_, buffer_, target_name_);
        close_overlay();
        return;
    }
    const OptionSpec *spec = find_option(target_);
    if (spec == nullptr)
    {
        close_overlay();
        return;
    }
    if (buffer_.empty())
    {
        edits_[spec->key] = std::nullopt; // empty: the host's default
        close_overlay();
        return;
    }
    if (const auto problem = validate(*spec, buffer_))
    {
        overlay_error_ = std::string(spec->label) + " " + *problem;
        feedback.play(hui::audio::Cue::error);
        return;
    }
    edits_[spec->key] = buffer_;
    close_overlay();
}

// ---- drawing ----------------------------------------------------------------------------------

void Screen::draw_status(hui::gfx::DrawList &list, const Rect &panel) const
{
    const float x = panel.x + 24.0f;
    float y = panel.y + 64.0f;
    const Rect box{x, y, panel.w - 48.0f, 136.0f};
    list.rounded_rect(box, 16.0f, Color::rgb(0x0d1428, 0.9f));

    Color dot = kIdle;
    std::string state = "Checking...";
    std::string detail;
    if (snap_.polled)
    {
        if (!snap_.running)
        {
            dot = kBad;
            state = "Host stopped";
            detail = "Start it here, or let the Payload Manager start it at boot.";
        }
        else if (!snap_.poll_error.empty())
        {
            dot = kSun;
            state = "Host busy";
            detail = "The control socket did not answer: " + snap_.poll_error;
        }
        else if (snap_.streaming)
        {
            dot = kGood;
            state = "Streaming";
            detail = "Moonlight is playing \"" + snap_.app + "\".";
        }
        else
        {
            dot = kGood;
            state = "Host running";
            detail = "Waiting for Moonlight. Add this console by its IP address.";
        }
    }
    const float pulse = 0.75f + 0.25f * std::sin(time_ * 3.0f);
    list.glow({box.x + 24.0f, box.y + 30.0f, 20.0f, 20.0f}, 10.0f, 22.0f, dot.with_alpha(0.35f * pulse));
    list.circle(box.x + 34.0f, box.y + 40.0f, 10.0f, dot);
    hui::ui::text(list, fonts_.display, state, box.x + 60.0f, box.y + 52.0f, 34.0f, kInk);
    hui::ui::text(list, fonts_.regular, shorten(fonts_.regular, detail, 22.0f, box.w - 48.0f), box.x + 24.0f,
                  box.y + 90.0f, 22.0f, kMuted);
    std::string files;
    if (snap_.host_elf_size >= 0)
        files = "Host ELF " + std::to_string((snap_.host_elf_size + 512 * 1024) / (1024 * 1024)) +
                " MB in /data/prosperosunshine";
    else
        files = snap_.bundled_elf ? "Installing the host ELF..." : "No host ELF in /data/prosperosunshine";
    hui::ui::text(list, fonts_.regular, files, box.x + 24.0f, box.y + 120.0f, 19.0f, kFaint);
}

void Screen::draw_row(hui::ui::Canvas &canvas, const Row &row, bool focused) const
{
    hui::gfx::DrawList &list = canvas.list;
    const Rect &r = row.rect;
    const Color label = focused ? kInk : kInk.with_alpha(0.92f);
    list.rounded_rect(r, 14.0f, kRow);

    auto button_row = [&](const std::string &title, const std::string &sub, Color accent, bool enabled) {
        const Color ink = enabled ? label : kFaint;
        list.rounded_rect({r.x, r.y + 14.0f, 6.0f, r.h - 28.0f}, 3.0f, accent.with_alpha(enabled ? 1.0f : 0.4f));
        hui::ui::text(list, fonts_.semibold, title, r.x + 28.0f, r.y + (sub.empty() ? 46.0f : 34.0f), 27.0f, ink);
        if (!sub.empty())
            hui::ui::text(list, fonts_.regular, shorten(fonts_.regular, sub, 19.0f, r.w - 56.0f), r.x + 28.0f,
                          r.y + 60.0f, 19.0f, kMuted);
    };

    switch (row.kind)
    {
    case RowKind::start_stop:
        if (snap_.running)
            button_row("Stop host", "Ends streaming and closes the host.", kBad, !busy());
        else
            button_row("Start host", "Sends the host ELF to the ELF loader on port 9021.", kGood, !busy());
        break;
    case RowKind::restart:
        button_row("Restart host", "Stops the host and starts it again with the saved settings.", kSun, !busy());
        break;
    case RowKind::autoload: {
        std::string sub;
        if (!snap_.manager_present)
            sub = "Needs the Payload Manager (PLDM00001).";
        else if (snap_.autoload && !snap_.manager_autoload)
            sub = "On, but the Payload Manager's own autoload is off.";
        else
            sub = "The Payload Manager loads the host at boot.";
        button_row("Start at boot", sub, kSun, !busy());
        // A switch.
        const Rect track{r.x + r.w - 104.0f, r.cy() - 18.0f, 76.0f, 36.0f};
        list.rounded_rect(track, 18.0f, snap_.autoload ? kSun : Color::rgb(0x3a4363));
        const float knob = snap_.autoload ? track.x + track.w - 18.0f : track.x + 18.0f;
        list.circle(knob, track.cy(), 14.0f, Color::rgb(0xffffff));
        break;
    }
    case RowKind::pairing: {
        const Pairing &p = snap_.pairings[static_cast<std::size_t>(row.index)];
        const float pulse = 0.6f + 0.4f * std::sin(time_ * 4.0f);
        list.rounded_rect({r.x, r.y + 14.0f, 6.0f, r.h - 28.0f}, 3.0f, kSun.with_alpha(pulse));
        hui::ui::text(list, fonts_.semibold, shorten(fonts_.semibold, p.name.empty() ? "Moonlight" : p.name, 27.0f, r.w - 260.0f),
                      r.x + 28.0f, r.y + 34.0f, 27.0f, label);
        hui::ui::text(list, fonts_.regular, p.address.empty() ? "wants to pair" : "from " + p.address + " wants to pair",
                      r.x + 28.0f, r.y + 60.0f, 19.0f, kMuted);
        hui::ui::text(list, fonts_.semibold, "Enter PIN", r.x + r.w - 28.0f, r.y + 46.0f, 24.0f, kSun, Align::right);
        break;
    }
    case RowKind::option: {
        const OptionSpec &spec = option_specs()[static_cast<std::size_t>(row.index)];
        const auto edited = edits_.find(spec.key);
        const auto saved = snap_.config.find(spec.key);
        const bool changed = edits_loaded_ && edited != edits_.end() && saved != snap_.config.end() &&
                             edited->second != saved->second;
        hui::ui::text(list, fonts_.semibold, spec.label, r.x + 24.0f, r.y + 32.0f, 25.0f, label);
        hui::ui::text(list, fonts_.regular, shorten(fonts_.regular, spec.help, 18.0f, 430.0f), r.x + 24.0f,
                      r.y + 58.0f, 18.0f, kMuted);
        const bool is_default = edited == edits_.end() || !edited->second;
        const std::string value = option_text(row.index);
        const float right = r.x + r.w - 24.0f;
        const float value_right = spec.kind == OptionKind::text ? right : right - 34.0f;
        const float width = 330.0f;
        hui::ui::text(list, fonts_.semibold, shorten(fonts_.semibold, value, 24.0f, width), value_right, r.y + 45.0f,
                      24.0f, is_default ? kMuted : kInk, Align::right);
        if (spec.kind != OptionKind::text)
        {
            const Color arrows = focused ? kSun : kFaint;
            const float vw = std::min(fonts_.semibold.measure(value, 24.0f), width);
            hui::ui::text(list, fonts_.semibold, "<", value_right - vw - 22.0f, r.y + 45.0f, 24.0f, arrows,
                          Align::right);
            hui::ui::text(list, fonts_.semibold, ">", right, r.y + 45.0f, 24.0f, arrows, Align::right);
        }
        else if (focused)
        {
            hui::ui::text(list, fonts_.regular, "Cross to edit", right, r.y + 66.0f, 16.0f, kFaint, Align::right);
        }
        if (changed)
            list.circle(r.x + 10.0f, r.y + 23.0f, 5.0f, kSun);
        break;
    }
    case RowKind::save:
        button_row(dirty() ? "Save" : "Saved", dirty() ? "Writes sunshine.conf." : "No unsaved changes.", kGood,
                   !busy());
        break;
    case RowKind::save_restart:
        button_row("Save and restart", "Applies the settings now.", kSun, !busy());
        break;
    }
}

void Screen::draw_overlay(hui::ui::Canvas &canvas) const
{
    hui::gfx::DrawList &list = canvas.list;
    list.rounded_rect({0.0f, 0.0f, 1920.0f, 1080.0f}, 0.0f, Color::rgb(0x05070f, 0.72f));
    const Rect card{440.0f, 180.0f, 1040.0f, 730.0f};
    list.shadow(card, 28.0f, 40.0f, Color::rgb(0x000000, 0.5f));
    list.rounded_rect(card, 28.0f, Color::rgb(0x151d3a));
    hui::ui::text(list, fonts_.display, overlay_title_, card.cx(), card.y + 76.0f, 40.0f, kInk, Align::center);
    std::string hint;
    if (overlay_ == Overlay::pin)
        hint = "Type the four digits Moonlight shows, then Done.";
    else if (overlay_ == Overlay::number)
        hint = "Done keeps the number; leave it empty for the default.";
    else
        hint = "Done keeps the name; leave it empty for the default.";
    hui::ui::text(list, fonts_.regular, hint, card.cx(), card.y + 118.0f, 22.0f, kMuted, Align::center);

    if (overlay_ == Overlay::pin)
    {
        const float size = 96.0f, gap = 24.0f;
        const float x0 = card.cx() - (4 * size + 3 * gap) / 2.0f;
        for (int i = 0; i < 4; ++i)
        {
            const Rect cell{x0 + i * (size + gap), card.y + 150.0f, size, 112.0f};
            const bool current = static_cast<int>(buffer_.size()) == i;
            list.bordered_rect(cell, 18.0f, Color::rgb(0x0d1428), current ? 3.0f : 2.0f,
                               current ? kSun : Color::rgb(0x2f3a60));
            if (i < static_cast<int>(buffer_.size()))
                hui::ui::text(list, fonts_.mono, std::string(1, buffer_[static_cast<std::size_t>(i)]), cell.cx(),
                              cell.y + 78.0f, 56.0f, kInk, Align::center);
        }
    }
    else
    {
        const Rect field{card.x + 80.0f, card.y + 160.0f, card.w - 160.0f, 92.0f};
        list.bordered_rect(field, 18.0f, Color::rgb(0x0d1428), 3.0f, kSun);
        const std::string shown = buffer_.empty() ? std::string() : buffer_;
        const float w = hui::ui::text(list, fonts_.semibold, shown, field.x + 28.0f, field.y + 60.0f, 36.0f, kInk);
        if (std::fmod(time_, 1.0f) < 0.6f)
            list.rounded_rect({field.x + 32.0f + w, field.y + 24.0f, 3.0f, 46.0f}, 1.5f, kSun);
        if (buffer_.empty())
            hui::ui::text(list, fonts_.regular, "Default", field.x + 44.0f, field.y + 60.0f, 30.0f, kFaint);
    }
    if (!overlay_error_.empty())
        hui::ui::text(list, fonts_.semibold, overlay_error_, card.cx(), card.y + 300.0f, 22.0f, kBad, Align::center);
    keyboard_.draw(canvas);
}

void Screen::draw(hui::gfx::DrawList &list, std::uint32_t glass) const
{
    hui::ui::Canvas canvas{list, fonts_, glass, time_};

    // Header.
    list.circle(122.0f, 96.0f, 22.0f, kSun);
    list.glow({100.0f, 74.0f, 44.0f, 44.0f}, 22.0f, 30.0f, kSun.with_alpha(0.35f));
    hui::ui::text(list, fonts_.display, "ProsperoSunshine", 164.0f, 112.0f, 50.0f, kInk);
    hui::ui::text(list, fonts_.regular, "Moonlight game-stream host for the PS5", 164.0f, 146.0f, 22.0f, kMuted);
    if (!version_.empty())
        hui::ui::text(list, fonts_.regular, "App " + version_, 1824.0f, 112.0f, 20.0f, kFaint, Align::right);

    // Panels.
    for (const Rect *panel : {&left_panel_, &right_panel_})
    {
        list.shadow(*panel, 24.0f, 30.0f, Color::rgb(0x000000, 0.35f));
        list.rounded_rect(*panel, 24.0f, kPanel);
    }
    hui::ui::text(list, fonts_.semibold, "HOST", left_panel_.x + 28.0f, left_panel_.y + 44.0f, 22.0f,
                  column_ == 0 ? kSun : kMuted, Align::left, 2.0f);
    hui::ui::text(list, fonts_.semibold, "SETTINGS", right_panel_.x + 28.0f, right_panel_.y + 44.0f, 22.0f,
                  column_ == 1 ? kSun : kMuted, Align::left, 2.0f);
    hui::ui::text(list, fonts_.regular, "/data/prosperosunshine/sunshine.conf", right_panel_.x + right_panel_.w - 28.0f,
                  right_panel_.y + 44.0f, 19.0f, kFaint, Align::right);
    draw_status(list, left_panel_);

    hui::ui::text(list, fonts_.semibold, "PAIRING", left_panel_.x + 28.0f, pairing_header_y_, 22.0f, kMuted,
                  Align::left, 2.0f);
    if (snap_.pairings.empty())
    {
        const float y = pairing_header_y_ + 44.0f;
        hui::ui::paragraph(list, fonts_.regular,
                           snap_.running ? "No pairing requests. Start pairing in Moonlight; its PIN goes in here."
                                         : "Pairing requests appear here while the host runs.",
                           left_panel_.x + 28.0f, y, 22.0f, left_panel_.w - 56.0f, 30.0f, kMuted, 3);
    }
    else if (hidden_pairings_ > 0)
    {
        hui::ui::text(list, fonts_.regular, "and " + std::to_string(hidden_pairings_) + " more",
                      left_panel_.x + 28.0f, left_panel_.y + left_panel_.h - 16.0f, 19.0f, kFaint);
    }

    if (overlay_ == Overlay::none)
        highlight_.draw(canvas, style_, {hui::ui::HighlightKind::tint});
    for (int c = 0; c < 2; ++c)
    {
        const std::vector<Row> &rows = c == 0 ? left_ : right_;
        for (std::size_t i = 0; i < rows.size(); ++i)
            draw_row(canvas, rows[i], overlay_ == Overlay::none && c == column_ && static_cast<int>(i) == focus_[c]);
    }
    if (overlay_ == Overlay::none)
    {
        // The focus ring over the rows.
        const Rect h = highlight_.rect(time_);
        list.bordered_rect(h.inset(-3.0f), 17.0f, Color::rgb(0, 0.0f), 3.0f, kSun.with_alpha(0.9f));
    }

    // Busy line, a press waiting for its second one, and the last outcome.
    if (armed_ >= 0 && armed_age_ <= kArmSeconds && !busy())
    {
        const std::string text = "This ends the running stream. Press Cross again to go on.";
        const float w = fonts_.semibold.measure(text, 24.0f) + 120.0f;
        const Rect bar{960.0f - w / 2.0f, 918.0f, w, 56.0f};
        list.rounded_rect(bar, 28.0f, Color::rgb(0x3a2a10, 0.95f));
        list.circle(bar.x + 36.0f, bar.cy(), 9.0f, kSun);
        hui::ui::text(list, fonts_.semibold, text, bar.x + 60.0f, bar.cy() + 9.0f, 24.0f, kInk);
    }
    else if (busy())
    {
        const Rect bar{560.0f, 918.0f, 800.0f, 56.0f};
        list.rounded_rect(bar, 28.0f, Color::rgb(0x1b2546, 0.95f));
        list.arc(bar.x + 36.0f, bar.cy(), 12.0f, 4.0f, time_ * 6.0f, 4.2f, kSun);
        hui::ui::text(list, fonts_.semibold, snap_.busy, bar.x + 64.0f, bar.cy() + 9.0f, 24.0f, kInk);
    }
    else if (message_age_ < kMessageSeconds && !snap_.message.empty())
    {
        const float fade = std::min(1.0f, (kMessageSeconds - message_age_) / 0.6f);
        list.push_opacity(fade);
        const float w = std::min(1400.0f, fonts_.semibold.measure(snap_.message, 24.0f) + 120.0f);
        const Rect bar{960.0f - w / 2.0f, 918.0f, w, 56.0f};
        list.rounded_rect(bar, 28.0f, Color::rgb(0x1b2546, 0.95f));
        list.circle(bar.x + 36.0f, bar.cy(), 9.0f, snap_.message_ok ? kGood : kBad);
        hui::ui::text(list, fonts_.semibold, shorten(fonts_.semibold, snap_.message, 24.0f, w - 90.0f), bar.x + 60.0f,
                      bar.cy() + 9.0f, 24.0f, kInk);
        list.pop_opacity();
    }

    // Button hints.
    const hui::ui::GlyphStyle glyphs = hui::ui::GlyphStyle::dark();
    hui::ui::HintLayout hints_layout;
    hints_layout.cy = 1030.0f;
    hints_layout.size = 36.0f;
    hints_layout.text_size = 22.0f;
    if (overlay_ != Overlay::none)
    {
        const hui::ui::Hint hints[] = {{hui::ui::Button::cross, "Type"},
                                       {hui::ui::Button::square, "Delete"},
                                       {hui::ui::Button::options, "Done"},
                                       {hui::ui::Button::circle, "Cancel"}};
        hui::ui::draw_hints(list, fonts_, glyphs, hints, 4, 1824.0f, true, hints_layout);
    }
    else
    {
        const hui::ui::Hint hints[] = {{hui::ui::Button::cross, "Select"},
                                       {hui::ui::Button::dpad, "Move / change"},
                                       {hui::ui::Button::square, "Default"},
                                       {hui::ui::Button::triangle, "Reload files"},
                                       {hui::ui::Button::l1, "Panel", hui::ui::Button::r1}};
        hui::ui::draw_hints(list, fonts_, glyphs, hints, 5, 1824.0f, true, hints_layout);
    }

    if (overlay_ != Overlay::none)
        draw_overlay(canvas);
}

} // namespace psapp
