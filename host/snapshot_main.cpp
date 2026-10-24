// ORION Media Center - Headless host renderer: runs the tour and writes PNG files.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// usage: hui_snapshots <assets dir> <output dir> [design id|all] [width height]
//
// HUI_LIBRARY=<dir> shows that library instead of tests/fixtures/library.
// HUI_THEME=<id> wears that theme (built in, or a file in <output>/.data/themes).
// HUI_PREFS=<json> starts ORION with that library.json (tabs, tab themes, ...).
//
// Runs the same shell, designs and tour script as the console build, through
// Mesa's surfaceless EGL (llvmpipe), with a fixed 60 Hz clock. Only the frames
// the tour marks are rendered, so a full run takes seconds. With
// HUI_STRIP=<from>,<to> it instead writes every frame of the switch between
// two designs (for reviewing the transition).
//
// HUI_REEL=<dir> records instead of photographing: every third simulated frame
// is piped to ffmpeg, which writes animated WebP clips into <dir>.
//   HUI_REEL_SPLIT=1   one clip per tour picture (the Theme Lab: one per theme)
//   otherwise          one clip per design, its whole tour
//   HUI_REEL=<dir> HUI_REEL_SWITCH=1   one clip of L1/R1 cycling through all designs

#include "app/shell.hpp"
#include "app/tour.hpp"
#include "core/save_file.hpp"
#include "demo/catalog.hpp"
#include "gfx/gl_program.hpp"
#include "gfx/renderer.hpp"
#include "library/emulators.hpp"
#include "library/favourites.hpp"
#include "library/icon.hpp"
#include "library/json.hpp"
#include "library/library.hpp"
#include "manifest.hpp"
#include "orion/prefs.hpp"
#include "ui/fonts.hpp"
#include "ui/theme.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb/stb_image_write.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{

bool open_context()
{
    auto get_platform_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    EGLDisplay display =
        get_platform_display != nullptr
            ? get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)
            : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0;
    EGLint minor = 0;
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor) ||
        !eglBindAPI(EGL_OPENGL_API))
        return false;
    const EGLint context_attributes[] = {EGL_CONTEXT_MAJOR_VERSION,
                                         4,
                                         EGL_CONTEXT_MINOR_VERSION,
                                         5,
                                         EGL_CONTEXT_OPENGL_PROFILE_MASK,
                                         EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                                         EGL_NONE};
    EGLContext context =
        eglCreateContext(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, context_attributes);
    return context != EGL_NO_CONTEXT &&
           eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context);
}

bool load_font(hui::gfx::Renderer &renderer, const std::string &path, hui::gfx::Font *font,
               hui::ui::FontRef *ref)
{
    std::string data;
    if (!hui::save::read_file(path, &data) || !font->load(data))
    {
        std::fprintf(stderr, "cannot load font %s\n", path.c_str());
        return false;
    }
    ref->font = font;
    ref->texture = renderer.batch().create_font_texture(*font);
    return true;
}

// Streams raw frames to ffmpeg and gives the finished clip its name on close
// (the name is only known once the tour reaches the picture that ends it).
class Recorder
{
  public:
    Recorder(std::string directory, int width, int height, int fps)
        : directory_(std::move(directory)), width_(width), height_(height), fps_(fps)
    {
    }
    ~Recorder()
    {
        // Frames after the last picture belong to no clip.
        if (pipe_ != nullptr)
        {
            pclose(pipe_);
            std::remove((directory_ + "/.recording.webp").c_str());
        }
    }
    void frame(const unsigned char *rgba)
    {
        if (pipe_ == nullptr)
        {
            char command[512];
            std::snprintf(command, sizeof(command),
                          "ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgba -s %dx%d -r %d -i - "
                          "-vf vflip -c:v libwebp_anim -lossless 0 -q:v 62 -compression_level 5 "
                          "-loop 0 '%s/.recording.webp'",
                          width_, height_, fps_, directory_.c_str());
            pipe_ = popen(command, "w");
            frames_ = 0;
        }
        if (pipe_ != nullptr)
        {
            std::fwrite(rgba, 1, static_cast<std::size_t>(width_) * height_ * 4, pipe_);
            ++frames_;
        }
    }
    void close(const std::string &name)
    {
        if (pipe_ == nullptr)
            return;
        pclose(pipe_);
        pipe_ = nullptr;
        const std::string from = directory_ + "/.recording.webp";
        const std::string to = directory_ + "/" + name + ".webp";
        std::rename(from.c_str(), to.c_str());
        std::fprintf(stderr, "recorded %s: %d frames\n", to.c_str(), frames_);
    }

  private:
    std::string directory_;
    int width_;
    int height_;
    int fps_;
    FILE *pipe_ = nullptr;
    int frames_ = 0;
};

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: %s <assets dir> <output dir> [design id|all] [width height]\n",
                     argv[0]);
        return 2;
    }
    const std::string assets = argv[1];
    const std::string output = argv[2];
    const std::string only = argc > 3 && std::string(argv[3]) != "all" ? argv[3] : "";
    const int width = argc > 5 ? std::atoi(argv[4]) : 1920;
    const int height = argc > 5 ? std::atoi(argv[5]) : 1080;

    if (!open_context())
    {
        std::fprintf(stderr, "no surfaceless EGL OpenGL 4.5 context\n");
        return 1;
    }
    std::fprintf(stderr, "GL %s / %s\n", reinterpret_cast<const char *>(glGetString(GL_VERSION)),
                 reinterpret_cast<const char *>(glGetString(GL_RENDERER)));
    hui::gfx::set_glsl_prefix("#version 450 core\n");

    hui::gfx::Renderer renderer;
    hui::gfx::Font regular;
    hui::gfx::Font semibold;
    hui::gfx::Font display;
    hui::gfx::Font mono;
    hui::gfx::Font pixel;
    hui::gfx::Font hand;
    hui::ui::Fonts fonts;
    if (!renderer.init() ||
        !load_font(renderer, assets + "/fonts/inter-regular.huifont", &regular, &fonts.regular) ||
        !load_font(renderer, assets + "/fonts/inter-semibold.huifont", &semibold,
                   &fonts.semibold) ||
        !load_font(renderer, assets + "/fonts/montserrat-medium.huifont", &display,
                   &fonts.display) ||
        !load_font(renderer, assets + "/fonts/dejavu-sans-mono.huifont", &mono, &fonts.mono) ||
        !load_font(renderer, assets + "/fonts/press-start-2p.huifont", &pixel, &fonts.pixel) ||
        !load_font(renderer, assets + "/fonts/patrick-hand.huifont", &hand, &fonts.hand))
        return 1;
    hui::demo::Catalog catalog;
    if (!catalog.build_covers(renderer, fonts))
    {
        std::fprintf(stderr, "cover art failed\n");
        return 1;
    }

    // HUI_SHOWCASE=<dir>: a console library of the catalogue's invented games,
    // for pictures of ORION with no real product in them: PS5 and PS4 games
    // and homebrew (<dir>/homebrew/<id>/sce_sys: param.json, icon0.png from
    // their covers), and DS, GBA and SNES games in a RetroArch folder with
    // their covers as box art. Then stop; point HUI_LIBRARY at <dir> (and hide
    // the RetroArch tile, PPSA92100, with hidden_titles) to photograph it.
    if (const char *showcase = std::getenv("HUI_SHOWCASE"))
    {
        constexpr int kSize = hui::demo::Catalog::kCoverSize;
        GLuint target = 0;
        GLuint storage = 0;
        glGenFramebuffers(1, &target);
        glGenRenderbuffers(1, &storage);
        glBindRenderbuffer(GL_RENDERBUFFER, storage);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, kSize, kSize);
        glBindFramebuffer(GL_FRAMEBUFFER, target);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, storage);
        const auto size = static_cast<float>(kSize);
        // ensure_directory makes only the last level: one at a time.
        const auto folder_of = [](std::string path, std::initializer_list<std::string> parts)
        {
            for (const std::string &part : parts)
            {
                hui::save::ensure_directory(path);
                path += "/" + part;
            }
            hui::save::ensure_directory(path);
            return path;
        };
        const auto cover_png = [&](const hui::demo::Item &item, const std::string &path)
        {
            hui::gfx::DrawList list;
            list.image(item.cover, {0.0f, 0.0f, size, size}, hui::gfx::kCanvasUv,
                       hui::gfx::Color::rgb(0xffffff), 0.0f);
            renderer.begin();
            renderer.draw(list);
            renderer.present(target, kSize, kSize, size, size);
            std::vector<unsigned char> pixels(static_cast<std::size_t>(kSize * kSize * 4));
            glReadPixels(0, 0, kSize, kSize, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            for (std::size_t p = 3; p < pixels.size(); p += 4)
                pixels[p] = 255;
            stbi_flip_vertically_on_write(1);
            return stbi_write_png(path.c_str(), kSize, kSize, 4, pixels.data(), kSize * 4) != 0;
        };
        const auto title_folder = [&](const std::string &id, const std::string &name, bool free)
        {
            const std::string folder = folder_of(showcase, {"homebrew", id, "sce_sys"});
            const std::string param =
                std::string("{\n  \"applicationCategoryType\": 0,\n") +
                "  \"applicationDrmType\": \"" + (free ? "free" : "standard") + "\",\n" +
                "  \"contentVersion\": \"01.000.000\",\n" + "  \"localizedParameters\": {\n" +
                "    \"defaultLanguage\": \"en-US\",\n" +
                "    \"en-US\": {\"titleName\": " + hui::json::quote(name) + "}\n  },\n" +
                "  \"titleId\": \"" + id + "\"\n}\n";
            hui::save::write_atomic(folder + "/param.json", param);
            return folder;
        };
        // The emulated games' systems: the database RetroArch names, the
        // ROM's extension and the invented core that plays it.
        struct System
        {
            const char *database;
            const char *extension;
            const char *core;
            const char *core_name;
        };
        constexpr System kSystems[] = {
            {"Nintendo - Nintendo DS", "nds", "desmume", "DeSmuME"},
            {"Nintendo - Game Boy Advance", "gba", "mgba", "mGBA"},
            {"Nintendo - Super Nintendo Entertainment System", "sfc", "snes9x", "Snes9x"},
        };
        constexpr std::size_t kHomebrewFrom = 14; // 0..13 games, then 4 homebrew
        constexpr std::size_t kRomsFrom = 18;     // then 6 emulated games
        const std::string retroarch = title_folder("PPSA92100", "RetroArch", true);
        const std::string arch_root = retroarch.substr(0, retroarch.size() - 8); // no /sce_sys
        for (const System &system : kSystems)
        {
            const std::string cores = folder_of(arch_root, {"cores"});
            const std::string core = cores + "/" + system.core + "_libretro";
            hui::save::write_atomic(core + ".so", "");
            hui::save::write_atomic(
                core + ".info", std::string("display_name = \"") + system.core_name +
                                    "\"\ncorename = \"" + system.core_name + "\"\ndatabase = \"" +
                                    system.database + "\"\nsupported_extensions = \"" +
                                    system.extension + "\"\n");
        }
        int written = 0;
        for (std::size_t i = 0; i < catalog.size(); ++i)
        {
            const hui::demo::Item &item = catalog[i];
            bool ok = true;
            if (i >= kRomsFrom)
            {
                const System &system = kSystems[(i - kRomsFrom) % 3];
                const std::string stem = std::string(item.title) + " (USA)";
                const std::string content = folder_of(arch_root, {"content", system.database});
                ok = hui::save::write_atomic(content + "/" + stem + "." + system.extension, "")
                         .empty();
                const std::string art =
                    folder_of(arch_root, {"thumbnails", system.database, "Named_Boxarts"});
                ok = ok && cover_png(item, art + "/" + stem + ".png");
            }
            else
            {
                // Every fifth game a PS4 one, so both kinds of case show.
                char id[16];
                std::snprintf(id, sizeof(id), "%s%05zu", i % 5 == 4 ? "CUSA" : "PPSA", 92000 + i);
                const std::string folder = title_folder(id, item.title, i >= kHomebrewFrom);
                ok = cover_png(item, folder + "/icon0.png");
            }
            if (!ok)
                return 1;
            ++written;
        }
        std::fprintf(stderr, "showcase: %d titles in %s\n", written, showcase);
        return 0;
    }

    // HUI_HOME_ART=<dir>: ORION's home-screen art, drawn with the kit in its
    // own sky: icon0.png (512 x 512), pic0.png (behind the title while it is
    // selected) and pic1.png (while it starts), both 3840 x 2160. Then stop;
    // tools/prepare-assets.sh turns the pictures into sce_sys/ files.
    if (const char *art_dir = std::getenv("HUI_HOME_ART"))
    {
        using hui::gfx::Color;
        const hui::ui::Theme *orion = nullptr;
        for (const hui::ui::Theme &theme : hui::ui::themes())
        {
            if (std::string(theme.id) == "orion")
                orion = &theme;
        }
        if (orion == nullptr)
            return 1;
        const Color accent = orion->accent;
        // The nebula ORION's library drifts through, held still.
        const auto clouds = [](hui::gfx::DrawList &list, float scale)
        {
            struct Cloud
            {
                float x, y, size;
                std::uint32_t hue;
                float alpha;
            };
            constexpr Cloud kClouds[] = {
                {300.0f, 300.0f, 900.0f, 0x2448c8, 0.2f},
                {1520.0f, 240.0f, 820.0f, 0x1a9ad0, 0.18f},
                {1100.0f, 840.0f, 960.0f, 0x1f5f9a, 0.13f},
                {380.0f, 920.0f, 700.0f, 0x1c3a9c, 0.15f},
            };
            for (const Cloud &c : kClouds)
            {
                const float s = c.size * scale;
                list.shadow({c.x * scale - s * 0.5f, c.y * scale - s * 0.5f, s, s}, s * 0.5f,
                            s * 0.55f, Color::rgb(c.hue, c.alpha));
            }
        };
        // The O-ring of the library's header, large: a ring with a moon on it.
        const auto logo = [&](hui::gfx::DrawList &list, float cx, float cy, float r)
        {
            list.glow({cx - r, cy - r, r * 2.0f, r * 2.0f}, r, r * 0.9f, accent.with_alpha(0.32f));
            list.ring(cx, cy, r, r * 0.17f, accent.with_alpha(0.9f));
            list.ring(cx, cy, r - r * 0.06f, r * 0.03f, Color::rgb(0xffffff, 0.45f));
            const float mx = cx + std::cos(0.8f) * r;
            const float my = cy + std::sin(0.8f) * r;
            list.glow({mx - r * 0.3f, my - r * 0.3f, r * 0.6f, r * 0.6f}, r * 0.3f, r * 0.35f,
                      Color::rgb(0xffffff, 0.55f));
            list.circle(mx, my, r * 0.3f, Color::rgb(0xffffff));
        };
        const auto paint = [&](int w, int h, float vw, float vh, const hui::gfx::DrawList &list,
                               const std::string &path)
        {
            GLuint target = 0;
            GLuint storage = 0;
            glGenFramebuffers(1, &target);
            glGenRenderbuffers(1, &storage);
            glBindRenderbuffer(GL_RENDERBUFFER, storage);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
            glBindFramebuffer(GL_FRAMEBUFFER, target);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                                      storage);
            hui::gfx::BackdropSpec sky = orion->backdrop;
            sky.time = 42.0f;
            sky.params[0] = 252.0f;
            sky.params[1] = 12.0f;
            renderer.begin();
            renderer.backdrop(sky);
            renderer.draw(list);
            renderer.present(target, w, h, vw, vh);
            std::vector<unsigned char> pixels(static_cast<std::size_t>(w) *
                                              static_cast<std::size_t>(h) * 4);
            glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            for (std::size_t i = 3; i < pixels.size(); i += 4)
                pixels[i] = 255;
            stbi_flip_vertically_on_write(1);
            const bool ok = stbi_write_png(path.c_str(), w, h, 4, pixels.data(), w * 4) != 0;
            std::fprintf(stderr, "%s %s\n", ok ? "wrote" : "could not write", path.c_str());
            glDeleteRenderbuffers(1, &storage);
            glDeleteFramebuffers(1, &target);
            return ok;
        };
        const std::string dir = art_dir;
        hui::save::ensure_directory(dir);
        bool ok = true;
        {
            hui::gfx::DrawList list;
            clouds(list, 512.0f / 1920.0f * 1.6f);
            logo(list, 256.0f, 214.0f, 104.0f);
            hui::ui::text(list, fonts.display, "ORION", 256, 428, 84, Color::rgb(0xffffff),
                          hui::gfx::Align::center, 6.0f);
            // HUI_HOME_ART_BADGE=<word>: a tag in the top right corner, so a
            // second ORION tile (a new title ID) is told from the first.
            if (const char *badge = std::getenv("HUI_HOME_ART_BADGE"); badge != nullptr && *badge)
            {
                constexpr float kSize = 34.0f;
                constexpr float kTracking = 3.0f;
                const float width = fonts.semibold.font->measure(badge, kSize) +
                                    kTracking * static_cast<float>(std::strlen(badge)) + 44.0f;
                const hui::gfx::Rect pill{512.0f - 28.0f - width, 28.0f, width, 56.0f};
                list.shadow({pill.x, pill.y + 4.0f, pill.w, pill.h}, 28.0f, 14.0f,
                            Color::rgb(0x000000, 0.45f));
                list.rounded_rect(pill, 28.0f, accent);
                hui::ui::text(list, fonts.semibold, badge, pill.cx(), pill.y + 40.0f, kSize,
                              Color::rgb(0x061018), hui::gfx::Align::center, kTracking);
            }
            ok = paint(512, 512, 512.0f, 512.0f, list, dir + "/icon0.png") && ok;
        }
        {
            // The home screen writes the title's name and Play over the left
            // and the bottom: the mark sits to the right, alone.
            hui::gfx::DrawList list;
            clouds(list, 1.0f);
            logo(list, 1400.0f, 470.0f, 190.0f);
            ok = paint(3840, 2160, 1920.0f, 1080.0f, list, dir + "/pic0.png") && ok;
        }
        {
            hui::gfx::DrawList list;
            clouds(list, 1.0f);
            logo(list, 960.0f, 440.0f, 120.0f);
            hui::ui::text(list, fonts.display, "ORION", 960, 712, 104, Color::rgb(0xffffff),
                          hui::gfx::Align::center, 10.0f);
            hui::ui::text(list, fonts.semibold, "MEDIA CENTER", 960, 772, 28, accent,
                          hui::gfx::Align::center, 14.0f);
            ok = paint(3840, 2160, 1920.0f, 1080.0f, list, dir + "/pic1.png") && ok;
        }
        return ok ? 0 : 1;
    }

    GLuint framebuffer = 0;
    GLuint color = 0;
    glGenFramebuffers(1, &framebuffer);
    glGenRenderbuffers(1, &color);
    glBindRenderbuffer(GL_RENDERBUFFER, color);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 1;

    // Settings are not read from (or written to) a real save: a scratch folder.
    const std::string data_root = output + "/.data";
    hui::save::ensure_directory(output);
    hui::save::ensure_directory(data_root);
    std::remove((data_root + "/settings.bin").c_str());
    // HUI_LIBRARY=<dir>: the library to show, laid out like a console's /data
    // (<dir>/homebrew, <dir>/icon-cache). The default is the invented one the
    // unit tests use, so pictures do not depend on what this PC has.
    hui::library::Library titles;
    {
        const char *library_dir = std::getenv("HUI_LIBRARY");
        const std::string base =
            library_dir != nullptr ? library_dir : assets + "/../tests/fixtures/library";
        hui::library::ScanOptions scan;
        scan.roots = {base + "/homebrew"};
        scan.icon_cache = base + "/icon-cache";
        titles.titles = hui::library::scan(scan);
        titles.scanned = true;
        // Favourites go to the scratch folder and start empty, like settings.
        titles.state_dir = data_root;
        titles.themes_dir = assets + "/themes";
        // HUI_SETUP=1: open on the first-run setup, as a new console would.
        titles.offer_setup = std::getenv("HUI_SETUP") != nullptr;
        for (const char *file : {"/favourites.json", "/library.json", "/theme.json", "/themes.txt"})
            std::remove((data_root + file).c_str());
        // HUI_THEME=<id>: wear that theme, built in or one placed in
        // <output>/.data/themes, as if picked in Settings.
        if (const char *theme = std::getenv("HUI_THEME"))
            hui::save::write_atomic(data_root + "/theme.json",
                                    std::string("{\"base\": \"") + theme + "\"}\n");
        if (const char *prefs = std::getenv("HUI_PREFS"))
            hui::save::write_atomic(data_root + "/library.json", std::string(prefs) + "\n");
        // The emulators' games, with the folders HUI_PREFS names.
        const hui::orion::LibraryPrefs prefs = hui::orion::load_prefs(titles.state_dir);
        hui::library::add_emulator_games(titles, hui::orion::game_folders(prefs),
                                         titles.state_dir + "/art");
        // HUI_FAVOURITE_ALL=1: every title starred, the hidden ones aside:
        // Favourites then holds games, homebrew and emulated games together.
        if (std::getenv("HUI_FAVOURITE_ALL") != nullptr)
        {
            std::vector<std::string> ids;
            for (const hui::library::Title &title : titles.titles)
            {
                if (!prefs.title_hidden(title.id))
                    ids.push_back(title.id);
            }
            hui::library::save_favourites(titles.state_dir, ids);
        }
        const int icons = hui::library::load_icons(titles, renderer.batch());
        std::fprintf(stderr, "library: %zu titles, %d icons from %s\n", titles.titles.size(), icons,
                     base.c_str());
        // HUI_CASES: a folder of case pictures to try (as <state_dir>/cases).
        const char *cases = std::getenv("HUI_CASES");
        hui::library::load_case_templates(titles,
                                          {assets + "/cases", cases != nullptr
                                                                  ? std::string(cases)
                                                                  : titles.state_dir + "/cases"},
                                          renderer.batch());
    }
    hui::app::Shell shell(fonts, catalog, titles, data_root, renderer.glass_texture());
    shell.set_version("host");

    constexpr float kDt = 1.0f / 60.0f;
    // HUI_MANIFEST=<file.json>: describe every design and theme (host/manifest.cpp).
    if (const char *manifest = std::getenv("HUI_MANIFEST"))
        return hui::host::write_manifest(manifest, shell) ? 0 : 1;

    std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 4));
    stbi_flip_vertically_on_write(1);
    bool ok = true;
    const auto render = [&](const std::string &name)
    {
        shell.compose(renderer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        renderer.present(framebuffer, width, height);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        // The PNG is opaque whatever the blending left in the alpha channel.
        for (std::size_t i = 3; i < pixels.size(); i += 4)
            pixels[i] = 255;
        const std::string path = output + "/" + name + ".png";
        ok = stbi_write_png(path.c_str(), width, height, 4, pixels.data(), width * 4) != 0 && ok;
        std::fprintf(stderr, "wrote %s: %zu shapes, %zu draw calls, GL error 0x%x\n", path.c_str(),
                     renderer.last_instances(), renderer.last_draw_calls(), glGetError());
    };

    if (const char *strip = std::getenv("HUI_STRIP"))
    {
        int from = 0;
        int to = 1;
        std::sscanf(strip, "%d,%d", &from, &to);
        hui::InputFrame idle;
        idle.connected = true;
        shell.show(static_cast<std::size_t>(from), false);
        for (int frame = 0; frame < 120; ++frame)
            shell.update(idle, kDt);
        shell.show(static_cast<std::size_t>(to), true);
        for (int frame = 0; frame < 36; ++frame)
        {
            shell.update(idle, kDt);
            char name[32];
            std::snprintf(name, sizeof(name), "strip-%02d", frame);
            render(name);
        }
        return ok ? 0 : 1;
    }

    if (const char *reel = std::getenv("HUI_REEL"))
    {
        constexpr int kEvery = 3; // 60 Hz simulated, 20 frames per second recorded
        hui::save::ensure_directory(reel);
        Recorder recorder(reel, width, height, 60 / kEvery);
        const auto record = [&]()
        {
            shell.compose(renderer);
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            renderer.present(framebuffer, width, height);
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            recorder.frame(pixels.data());
        };
        hui::InputFrame idle;
        idle.connected = true;
        long count = 0;
        if (std::getenv("HUI_REEL_SWITCH") != nullptr)
        {
            // R1 through every design, then the info panel: the app in one clip.
            shell.show(0, false);
            for (std::size_t i = 0; i <= shell.concept_count(); ++i)
            {
                for (int frame = 0; frame < 66; ++frame, ++count)
                {
                    hui::InputFrame input = idle;
                    if (frame == 0 && i > 0)
                        input.pressed = hui::action_bit(hui::Action::page_next);
                    shell.update(input, kDt);
                    if (count % kEvery == 0)
                        record();
                }
            }
            recorder.close("switcher");
            return 0;
        }
        const bool split = std::getenv("HUI_REEL_SPLIT") != nullptr;
        hui::app::Tour tour(shell, only);
        std::size_t design = shell.current();
        char name[96];
        while (!tour.finished() && count < 60 * 60 * 60)
        {
            const hui::InputFrame input = tour.step(kDt);
            if (!split && shell.current() != design)
            {
                std::snprintf(name, sizeof(name), "%s", shell.concept_at(design).info().id);
                recorder.close(name);
                design = shell.current();
            }
            shell.update(input, kDt);
            if (count % kEvery == 0)
                record();
            ++count;
            if (!tour.capture().empty())
            {
                if (split)
                    recorder.close(tour.capture());
                tour.capture_done();
            }
        }
        if (!split)
            recorder.close(shell.concept_at(design).info().id);
        return 0;
    }

    hui::app::Tour tour(shell, only);
    if (tour.finished())
    {
        std::fprintf(stderr, "no design named '%s'\n", only.c_str());
        return 2;
    }
    long frames = 0;
    while (!tour.finished() && frames < 60 * 60 * 60)
    {
        const hui::InputFrame input = tour.step(kDt);
        // Nothing is presented here, so the numbers a console would measure
        // are stood in for: 60 Hz with a little jitter and a rare long frame.
        hui::app::Telemetry &telemetry = shell.telemetry();
        const float jitter = static_cast<float>((frames * 7919) % 97) / 97.0f;
        telemetry.push(16.3f + jitter * 0.8f + (frames % 211 == 0 ? 5.5f : 0.0f));
        telemetry.fps = 59.9f;
        telemetry.average_ms = 16.7f;
        telemetry.voices = static_cast<int>(frames / 40 % 4);
        telemetry.draw_calls = renderer.last_draw_calls();
        telemetry.instances = renderer.last_instances();
        // Scan for emulators, as main.cpp does it (pictures are not kept here).
        if (shell.take_rescan_request())
        {
            const hui::orion::LibraryPrefs prefs = hui::orion::load_prefs(titles.state_dir);
            hui::library::add_emulator_games(titles, hui::orion::game_folders(prefs),
                                             titles.state_dir + "/art");
            hui::library::load_icons(titles, renderer.batch());
            ++titles.generation;
        }
        shell.update(input, kDt);
        // All at once: a picture is taken when its covers are there.
        if (auto wanted = shell.take_wanted_icons(); !wanted.empty())
            hui::library::load_wanted_icons(titles, &wanted, renderer.batch(), 1 << 20);
        hui::library::load_sharp_icons(titles, shell.sharp_icons(), renderer.batch(), 1);
        hui::library::load_wide_icons(titles, shell.wide_icons(), titles.state_dir + "/wide",
                                      renderer.batch(), 2);
        ++frames;
        if (!tour.capture().empty())
        {
            render(tour.capture());
            tour.capture_done();
        }
        else
        {
            shell.rehearse(); // as on the console, every frame is drawn
        }
    }
    std::fprintf(stderr, "tour: %ld frames simulated\n", frames);
    return ok ? 0 : 1;
}
