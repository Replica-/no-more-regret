/* The port's enhanced.cfg. See enhanced.h. */
#include "enhanced.h"
#include "gamestate.h"
#include "asylum.h"
#include "bindings.h"
#include "clock.h"
#include "files.h"
#include "host.h"
#include "language.h"
#include "pointer.h"
#include "lighting.h"
#include "colours.h"
#include "enhancements.h"
#include "hud.h"
#include "options.h"
#include "profile.h"
#include "screen.h"
#include "utility.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <tuple>

namespace {

/* The sound's depth the player chose (8 or 16), 0 none: initAsylum's
 * depth is then crusader.cfg's DMA channel's, which enhanced.cfg does not
 * keep. A setting outside the arena. */
u16 &chosenSoundBits = gameSetting<u16>(0);
/* debug_feature=true read, which the file keeps when it is written again:
 * the debug features themselves are the run's, as it starts (host.h's
 * hostDebugFeatures). */
bool &debugFeatureKept = gameSetting<bool>(false);

/* The text without the spaces, tabs and returns at its ends. */
std::string trimmed(const std::string &text)
{
    const std::size_t first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) {
        return "";
    }
    const std::size_t last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

std::string lowered(std::string text)
{
    for (char &c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

/* crusader.cfg's settings the shared settings hold, in the order they are
 * read: frameskip before limitblasts, as No Remorse's frameskip spills into
 * limitBlasts (options.cpp). */
const char *const SHARED_OPTIONS[] = {"music",     "musicvolume", "soundvolume", "mousespeed", "animation",
                                      "frameskip", "limitblasts", "subtitles",   "violence"};

/* The text's lines, without the spaces and returns at their ends. */
std::vector<std::string> linesOf(const std::string &text)
{
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        lines.push_back(trimmed(text.substr(start, end - start)));
        start = end + 1;
    }
    return lines;
}

/* The line's key in lower case, empty for none (a comment). */
std::string keyOf(const std::string &line)
{
    const std::size_t equals = line.find('=');
    if (line.empty() || line[0] == '#' || line[0] == '/' || equals == std::string::npos) {
        return "";
    }
    return lowered(trimmed(line.substr(0, equals)));
}

bool sharedOption(const std::string &key)
{
    for (const char *option : SHARED_OPTIONS) {
        if (key == option) {
            return true;
        }
    }
    return false;
}

/* The game folder's file's text, empty without it. */
std::string fileText(const char *name)
{
    std::string text;
    if (fileExists(name) == 0) {
        return text;
    }
    std::FILE *file = fileStream(name, "rb");
    if (file == nullptr) {
        return text;
    }
    char buffer[512];
    std::size_t count = 0;
    while ((count = fileStreamRead(file, buffer, 1, sizeof buffer)) > 0) {
        text.append(buffer, count);
    }
    fileStreamClose(file);
    return text;
}

/* The shared settings' text (host.h's hostSharedSettings): crusader.cfg's
 * SHARED_OPTIONS as the game wrote them, the port's settings but the game
 * and the debug features (the full screen and the sound's depth always),
 * and the bindings' (sharedBindingsText) over those shared has. */
std::string sharedText(const std::string &shared)
{
    std::string text = "# The settings both games share, which each game reads over its own crusader.cfg and "
                       "enhanced.cfg and writes when it writes them; bind_<name>=default is each game's own.\r\n";
    const std::vector<std::string> options = linesOf(fileText("crusader.cfg"));
    for (const char *option : SHARED_OPTIONS) {
        for (const std::string &line : options) {
            if (keyOf(line) == option) {
                text += line + "\r\n";
                break;
            }
        }
    }
    for (const std::string &line : linesOf(enhancedText())) {
        const std::string key = keyOf(line);
        if (key.empty() || key == "game" || key == "debug_feature" || key == "fullscreen" || key == "sound_bits" ||
            key == "keys" || key == "mouse_roll" || key.starts_with("bind_")) {
            continue;
        }
        text += line + "\r\n";
    }
    text += std::string("fullscreen=") + (hostFullscreen() ? "1" : "0") + "\r\n";
    text += "sound_bits=" + std::to_string(chosenSoundBits) + "\r\n";
    return text + sharedBindingsText(shared);
}

/* The shared settings read over the game's: crusader.cfg's (readOptionText)
 * and the port's (readEnhancedText); keys= only when it is another preset,
 * which puts every action back to its own. */
void readShared(const std::string &shared)
{
    std::string options;
    std::string port;
    for (const std::string &line : linesOf(shared)) {
        const std::string key = keyOf(line);
        if (key.empty() || key == "game" || key == "debug_feature") {
            continue;
        }
        if (sharedOption(key)) {
            options += line + "\n";
        } else if (key != "keys" || lowered(trimmed(line.substr(line.find('=') + 1))) != keyPresetName(keyPreset())) {
            port += line + "\n";
        }
    }
    readOptionText(options.c_str(), nullptr);
    std::ignore = readEnhancedText(port);
}

}

std::string enhancedText()
{
    std::string text = "# The port's own settings beside crusader.cfg, which the originals read and write: the "
                       "language of the game's text, the sound's depth, the full screen, the wide screen, the "
                       "game speed, the keys' preset, whether the mouse rolls, each action's inputs.\r\n";
    text += std::string("game=") + PROFILE_NAME + "\r\n";
    text += std::string("language=") + languageCode(gameLanguage) + "\r\n";
    if (chosenSoundBits != 0U) {
        text += "sound_bits=" + std::to_string(chosenSoundBits) + "\r\n";
    }
    if (hostFullscreen()) {
        text += "fullscreen=1\r\n";
    }
    /* Always: on by default, so off is kept too. */
    text += "widescreen=" + std::to_string(screenWide()) + "\r\n";
    /* Always: a recording keeps the speed it ran at, whatever the default
     * becomes. */
    text += std::string("game_speed=") + clockMachineSpeedName(clockMachineSpeed()) + "\r\n";
    if (debugFeatureKept) {
        text += "debug_feature=true\r\n";
    }
    text += std::string("pointer=") + (pointerSetting() ? "1" : "0") + "\r\n";
    text += std::string("lighting=") + (lightingSetting() ? "1" : "0") + "\r\n";
    text += "colours=" + coloursText() + "\r\n";
    text += enhancementsText();
    return text + bindingsText();
}

std::vector<std::string> readEnhancedText(const std::string &text)
{
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string line = trimmed(text.substr(start, end - start));
        start = end + 1;
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos || line[0] == '#' || line[0] == '/') {
            continue;
        }
        const std::string key = lowered(trimmed(line.substr(0, equals)));
        if (key == "language") {
            gameLanguage = languageOfCode(lowered(trimmed(line.substr(equals + 1))).c_str());
        }
        if (key == "sound_bits") {
            /* The sound's depth in place of crusader.cfg's DMA channel's:
             * 8 or 16; another leaves the channel's. */
            const std::string value = trimmed(line.substr(equals + 1));
            u16 bits = 0;
            if (value == "8") {
                bits = 8;
            } else if (value == "16") {
                bits = 16;
            }
            chooseSoundBits(bits);
            continue;
        }
        if (key == "widescreen") {
            /* The experimental wide screen: 1 the sides, 2 the sides
             * dimmed; another off. */
            const std::string value = trimmed(line.substr(equals + 1));
            u8 mode = 0;
            if (value == "1") {
                mode = 1;
            } else if (value == "2") {
                mode = 2;
            }
            screenSetWide(mode);
            continue;
        }
        if (key == "game_speed") {
            /* The machine's speed: slow, normal or fast; another leaves it. */
            MachineSpeed named = MachineSpeed::NORMAL;
            if (clockMachineSpeedNamed(lowered(trimmed(line.substr(equals + 1))).c_str(), &named)) {
                clockSetMachineSpeed(named);
            }
            continue;
        }
        if (key == "fullscreen") {
            /* The window over the whole screen: 1; another, a window. */
            hostSetFullscreen(trimmed(line.substr(equals + 1)) == "1");
            continue;
        }
        if (key == "pointer") {
            /* The keyboard and mouse controls (pointer.h): 0 the
             * original's mouse; another on. */
            setPointerSetting(trimmed(line.substr(equals + 1)) != "0");
            continue;
        }
        if (key == "lighting") {
            /* The lighting (lighting.h): 0 the art as it is; another on. */
            setLightingSetting(trimmed(line.substr(equals + 1)) != "0");
            continue;
        }
        if (readEnhancement(key, trimmed(line.substr(equals + 1)))) {
            continue;
        }
        if (key == "colours") {
            /* The Silencer's colours (colours.h): three RRGGBB, or a
             * preset's number. */
            setColoursText(trimmed(line.substr(equals + 1)));
            continue;
        }
        if (key == "debug_feature") {
            /* The debug features: true (or 1); another off. */
            const std::string value = lowered(trimmed(line.substr(equals + 1)));
            debugFeatureKept = value == "true" || value == "1";
            continue;
        }
        if (key == "game") {
            /* The last game played, which the one program reads as it
             * starts (unified/src/system/values.cpp). */
            continue;
        }
    }
    return readBindingsText(text);
}

void chooseSoundBits(u16 bits)
{
    chosenSoundBits = bits == 8 || bits == 16 ? bits : 0;
    ASS_SetDepth(chosenSoundBits);
}

void readEnhancedOptions()
{
    loadBindings();
    /* What the port's enhancements draw (hud.h). */
    startHud();
    /* The port's controls by default: the keyboard and mouse controls
     * (pointer.h), the modern keys, the lighting (lighting.h) and the wide
     * screen, which enhanced.cfg's pointer=, keys=, lighting= and
     * widescreen= change; the tests'
     * REGRET_ORIGINAL_CONTROLS keeps the original's, the controls and the
     * screens their results came from. */
    const bool original = hostEnvironment("REGRET_ORIGINAL_CONTROLS") != nullptr;
    setPointerSetting(!original);
    /* The lighting too: the tests' screens are the art's own. */
    setLightingSetting(!original);
    setHardwareSetting(!original);
    setKeyPreset(original ? KeyPreset::ORIGINAL : KeyPreset::MODERN);
    /* The wide screen too: the world beside the 640x480 screen. */
    screenSetWide(original ? 0 : 1);
    /* The window's own changes of the full screen (its key) kept. */
    hostFullscreenAction(writeEnhancedOptions);
    for (const std::string &line : readEnhancedText(fileText(ENHANCED_FILE))) {
        debugPrintf(0xff, "Ignoring %s of %s: a setting it does not know\r\n", line.c_str(), ENHANCED_FILE);
    }
    /* The settings both games share over the game's own; the first game
     * to run makes them from its own. */
    std::string shared;
    hostSharedSettings(&shared);
    if (shared.empty()) {
        hostWriteSharedSettings(sharedText(shared));
    } else {
        readShared(shared);
    }
}

void writeEnhancedOptions()
{
    fileRemove(ENHANCED_FILE);
    std::FILE *file = fileStream(ENHANCED_FILE, "wb");
    if (file == nullptr) {
        return;
    }
    const std::string text = enhancedText();
    std::ignore = fileStreamWrite(file, text.data(), 1, text.size());
    fileStreamClose(file);
    /* And the settings both games share (sharedText). */
    std::string shared;
    hostSharedSettings(&shared);
    hostWriteSharedSettings(sharedText(shared));
}
