// ps5-homebrew-ui - Design "Library": the titles installed on this console.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORION's home: every title ShadowMountPlus can mount (library/library.hpp),
// as a grid of covers with the focused title's details underneath. Titles
// without artwork get a monogram on a colour derived from their ID, so the
// same title always looks the same.

#include "concepts/concepts.hpp"

#include "core/tween.hpp"
#include "ui/components/grid.hpp"
#include "ui/glyphs.hpp"
#include "ui/motion.hpp"
#include "ui/widgets.hpp"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace hui::concepts
{

namespace
{

using gfx::Color;
using gfx::Rect;

constexpr float kMargin = 96.0f;
constexpr float kGridTop = 222.0f;
constexpr float kGridBottom = 972.0f;
// Seven columns keep two whole rows in view, so scrolling never leaves a
// single row alone on the screen.
constexpr int kColumns = 7;

constexpr const char *kTechniques[] = {
    "Reads the console's library the way ShadowMountPlus does: folders, images, icon cache",
    "GridView over the real titles: one gliding focus, spring scrolling, soft edges",
    "Monogram covers in a colour hashed from the title ID for titles without art",
    "An empty state that says where ORION looked",
};

constexpr app::TourStep kTour[] = {
    {0.5f, 0, Direction::right},
    {0.3f, 0, Direction::right},
    {0.3f, 0, Direction::down},
    {0.6f, 0, Direction::none, "browse"},
};

const ui::Theme &library_theme()
{
    for (const ui::Theme &theme : ui::themes())
    {
        if (std::strcmp(theme.id, "acrylic") == 0)
            return theme;
    }
    return ui::themes()[0];
}

// FNV-1a of the ID, then a finaliser: IDs differ only in their last digits,
// and without the mixing neighbouring IDs land on neighbouring hues.
Color hashed_color(const std::string &id, float saturation, float value)
{
    std::uint32_t hash = 2166136261u;
    for (char c : id)
        hash = (hash ^ static_cast<unsigned char>(c)) * 16777619u;
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    hash *= 0x846ca68bu;
    hash ^= hash >> 16;
    const float hue = static_cast<float>(hash % 360u) / 60.0f;
    const float chroma = value * saturation;
    const float x = chroma * (1.0f - std::fabs(std::fmod(hue, 2.0f) - 1.0f));
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    switch (static_cast<int>(hue))
    {
    case 0:
        r = chroma, g = x;
        break;
    case 1:
        r = x, g = chroma;
        break;
    case 2:
        g = chroma, b = x;
        break;
    case 3:
        g = x, b = chroma;
        break;
    case 4:
        r = x, b = chroma;
        break;
    default:
        r = chroma, b = x;
        break;
    }
    const float m = value - chroma;
    return Color{r + m, g + m, b + m, 1.0f};
}

// Up to two letters: the initials of the first two words.
std::string monogram(const std::string &name)
{
    std::string letters;
    bool start = true;
    for (char c : name)
    {
        const bool letter = std::isalnum(static_cast<unsigned char>(c)) != 0;
        if (letter && start)
        {
            letters += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (letters.size() == 2)
                break;
        }
        start = !letter;
    }
    return letters;
}

std::string describe(const library::Title &title)
{
    std::string text = library::platform_name(title.platform);
    if (!text.empty())
        text += "  \xc2\xb7  ";
    text += title.source == library::Source::image ? "Image" : "Folder";
    return text;
}

class Library final : public app::Concept
{
  public:
    explicit Library(app::Context &context) : context_(context), theme_(library_theme())
    {
        grid_.style.theme = theme_;
        grid_.style.columns = kColumns;
        grid_.style.gap_x = 28.0f;
        grid_.style.gap_y = 30.0f;
        // Every edge refuses, as the home screen's own grids do.
        grid_.style.wrap = ui::GridWrap::none;
        grid_.style.card.title_size = 22.0f;
        grid_.style.card.subtitle_size = 17.0f;
        grid_.style.card.badge_corner = ui::CardCorner::top_right;
        grid_.style.card.glow = true;
        grid_.art = [this](ui::Canvas &canvas, const Rect &art, float radius,
                           const ui::CardItem &item, float /*focus*/)
        { draw_art(canvas, art, radius, item); };

        std::vector<ui::CardItem> items;
        for (const library::Title &title : context_.library.titles)
        {
            ui::CardItem item;
            item.title = title.name;
            item.subtitle = describe(title);
            item.badge = title.category == library::Category::media ? "MEDIA" : "";
            item.texture = title.texture;
            item.top = hashed_color(title.id, 0.55f, 0.42f);
            item.bottom = hashed_color(title.id, 0.7f, 0.2f);
            item.accent = hashed_color(title.id, 0.6f, 1.0f);
            items.push_back(std::move(item));
        }
        grid_.set_items(std::move(items));
        grid_.set_bounds(
            {kMargin, kGridTop, gfx::kVirtualWidth - 2.0f * kMargin, kGridBottom - kGridTop});
    }

    const app::ConceptInfo &info() const override
    {
        static const app::ConceptInfo kInfo{
            "library",
            "Library",
            "Every title on this console, as ShadowMountPlus sees it",
            "src/concepts/library.cpp",
            audio::SoundSet::glass,
            library_theme().accent,
            kTechniques,
        };
        return kInfo;
    }

    void enter() override
    {
        age_ = 0.0f;
        grid_.enter();
    }

    void update(const InputFrame &input, float dt, app::Feedback &feedback) override
    {
        age_ += dt;
        clock_ += dt;
        grid_.style.reduced_motion = context_.settings.reduced_motion;
        if (grid_.count() > 0)
            grid_.handle(input, feedback);
        grid_.update(dt);
    }

    void draw(app::Frame &frame) const override
    {
        frame.backdrop = theme_.backdrop;
        frame.backdrop.time = context_.settings.reduced_motion ? 0.0f : clock_;
        if (theme_.style == ui::SurfaceStyle::glass)
        {
            // Slow pools of light give the frosted cards something to blur.
            for (int i = 0; i < 3; ++i)
            {
                const float phase = clock_ * 0.1f + static_cast<float>(i) * 2.1f;
                const float cx = 420.0f + static_cast<float>(i) * 540.0f + std::sin(phase) * 140.0f;
                const float cy = 560.0f + std::cos(phase * 1.3f) * 200.0f;
                frame.scene.shadow(
                    {cx - 170.0f, cy - 170.0f, 340.0f, 340.0f}, 170.0f, 190.0f,
                    gfx::mix(theme_.primary, theme_.accent, static_cast<float>(i % 2))
                        .with_alpha(0.3f));
            }
            frame.glass = true;
        }

        gfx::DrawList &list = frame.overlay;
        const std::uint32_t glass = frame.glass ? frame.glass_texture : 0;
        ui::Canvas canvas{list, context_.fonts, glass, clock_};
        ui::Painter paint(list, context_.fonts, theme_, glass);
        list.push_opacity(tween::cubic_out(age_ / 0.35f));

        draw_header(list, paint);
        if (grid_.count() > 0)
        {
            grid_.draw(canvas);
            draw_details(list, paint);
        }
        else
        {
            draw_empty(list, paint);
        }

        const ui::Hint hints[] = {
            {ui::Button::dpad, "Browse"},
            {ui::Button::touchpad, "About"},
        };
        ui::draw_hints(list, context_.fonts, ui::GlyphStyle::dark(), hints, 2, 1824, true);
        list.pop_opacity();
    }

    std::span<const app::TourStep> tour() const override
    {
        return kTour;
    }

  private:
    void draw_header(gfx::DrawList &list, const ui::Painter &paint) const
    {
        const ui::Fonts &fonts = context_.fonts;
        ui::text(list, fonts.semibold, "ORION MEDIA CENTER", kMargin, 104, 17, theme_.accent,
                 gfx::Align::left, 4.0f);
        ui::text(list, fonts.display, "Library", kMargin, 172, 62, paint.page_text());
        const std::size_t count = context_.library.titles.size();
        const std::string summary = std::to_string(count) + (count == 1 ? " title" : " titles");
        ui::text(list, fonts.regular, summary.c_str(), gfx::kVirtualWidth - kMargin, 172, 24,
                 paint.page_text_muted(), gfx::Align::right);
    }

    void draw_art(ui::Canvas &canvas, const Rect &art, float radius, const ui::CardItem &item) const
    {
        if (item.texture != 0)
        {
            canvas.list.image(item.texture, art, item.uv, Color::rgb(0xffffff), radius);
            return;
        }
        canvas.list.gradient_rect(art, radius, item.top, item.bottom);
        const std::string letters = monogram(item.title);
        ui::text(canvas.list, canvas.fonts.display, letters.c_str(), art.cx(),
                 art.y + art.h * 0.5f + art.h * 0.13f, art.h * 0.36f, Color::rgb(0xffffff, 0.9f),
                 gfx::Align::center);
    }

    void draw_details(gfx::DrawList &list, const ui::Painter &paint) const
    {
        const library::Title &title =
            context_.library.titles[static_cast<std::size_t>(grid_.focus())];
        std::string line = title.id;
        if (!title.version.empty())
            line += "   v" + title.version;
        line += "   " + std::string(library::category_name(title.category));
        ui::text(list, context_.fonts.regular, line.c_str(), kMargin, 1024, 21,
                 paint.page_text_muted());
    }

    void draw_empty(gfx::DrawList &list, const ui::Painter &paint) const
    {
        const ui::Fonts &fonts = context_.fonts;
        const float cx = gfx::kVirtualWidth * 0.5f;
        ui::text(list, fonts.display, "No titles yet", cx, 520, 46, paint.page_text(),
                 gfx::Align::center);
        ui::text(list, fonts.regular,
                 "ORION shows what ShadowMountPlus can mount from /data/homebrew,", cx, 576, 24,
                 paint.page_text_muted(), gfx::Align::center);
        ui::text(list, fonts.regular, "/mnt/ext0/homebrew and USB drives.", cx, 610, 24,
                 paint.page_text_muted(), gfx::Align::center);
    }

    app::Context &context_;
    const ui::Theme &theme_;
    ui::GridView grid_;
    float age_ = 0.0f;
    float clock_ = 0.0f;
};

} // namespace

std::unique_ptr<app::Concept> make_library(app::Context &context)
{
    return std::make_unique<Library>(context);
}

} // namespace hui::concepts
