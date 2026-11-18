/* The choice of the game. See splash.h. */
#include "splash.h"
#include "gamefolder.h"
#include "profile.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

/* A game found: its name for CRUSADER_GAME, its title and its folder. */
struct Game {
    std::string game;
    std::string title;
    fs::path folder;
};

std::string upper(std::string text)
{
    for (char &c : text) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return text;
}

/* The names in folder, in capitals. */
std::vector<std::string> namesIn(const fs::path &folder)
{
    std::vector<std::string> names;
    std::error_code error;
    for (const fs::directory_entry &entry : fs::directory_iterator(folder, error)) {
        names.push_back(upper(entry.path().filename().string()));
    }
    return names;
}

bool has(const std::vector<std::string> &names, const char *name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

/* The game whose data and original's program folder holds, "" for none. */
std::string gameIn(const fs::path &folder)
{
    const std::vector<std::string> names = namesIn(folder);
    if (!has(names, "STATIC")) {
        return {};
    }
    if (has(names, "REGRET.EXE")) {
        return "regret";
    }
    if (has(names, "CRUSADER.EXE")) {
        return "remorse";
    }
    return {};
}

/* The games in the folders below start and below its cd folder, No Remorse
 * first; one of each. */
std::vector<Game> gamesBelow(const fs::path &start)
{
    std::vector<Game> games;
    for (const fs::path &under : {start, start / "cd"}) {
        std::error_code error;
        for (const fs::directory_entry &entry : fs::directory_iterator(under, error)) {
            if (!entry.is_directory(error)) {
                continue;
            }
            const std::string game = gameIn(entry.path());
            const bool known = std::any_of(games.begin(), games.end(), [&](const Game &g) { return g.game == game; });
            if (!game.empty() && !known) {
                games.push_back(
                    {game, game == "regret" ? "CRUSADER: NO REGRET" : "CRUSADER: NO REMORSE", entry.path()});
            }
        }
    }
    std::sort(games.begin(), games.end(), [](const Game &a, const Game &b) { return a.game > b.game; });
    return games;
}

/* Whether something has chosen the game already: CRUSADER_GAME, a port
 * option (--game, a replay's, a test's), or the folder the program starts
 * in. */
bool chosenAlready(int argc, char *argv[])
{
    if (std::getenv("CRUSADER_GAME") != nullptr) {
        return true;
    }
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]).rfind("--", 0) == 0) {
            return true;
        }
    }
    return !gameIn(gameFolder().path).empty();
}

/* The window's size and the panels' place, in its pixels. */
const float WIDTH = 640;
const float HEIGHT = 400;
const float PANEL_X = 80;
const float PANEL_W = 480;
const float PANEL_H = 90;
const float FIRST_Y = 130;
const float GAP = 24;

float panelY(std::size_t i)
{
    return FIRST_Y + (static_cast<float>(i) * (PANEL_H + GAP));
}

/* Text of size times the debug font's 8 pixels, centred on x. */
void text(SDL_Renderer *renderer, float x, float y, float size, const std::string &line)
{
    SDL_SetRenderScale(renderer, size, size);
    const float width = static_cast<float>(line.size()) * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * size;
    SDL_RenderDebugText(renderer, (x - (width / 2)) / size, y / size, line.c_str());
    SDL_SetRenderScale(renderer, 1, 1);
}

void draw(SDL_Renderer *renderer, const std::vector<Game> &games, std::size_t lit)
{
    SDL_SetRenderDrawColor(renderer, 8, 6, 10, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, 200, 30, 30, 255);
    text(renderer, WIDTH / 2, 40, 3, "NO MORE REGRET");
    SDL_SetRenderDrawColor(renderer, 150, 150, 160, 255);
    text(renderer, WIDTH / 2, 84, 1.5F, "CHOOSE A GAME");
    for (std::size_t i = 0; i < games.size(); i++) {
        const bool regret = games[i].game == "regret";
        const SDL_FRect panel{PANEL_X, panelY(i), PANEL_W, PANEL_H};
        if (i == lit) {
            SDL_SetRenderDrawColor(renderer, regret ? 30 : 70, regret ? 50 : 14, regret ? 90 : 14, 255);
        } else {
            SDL_SetRenderDrawColor(renderer, 22, 22, 28, 255);
        }
        SDL_RenderFillRect(renderer, &panel);
        if (i == lit) {
            SDL_SetRenderDrawColor(renderer, regret ? 90 : 230, regret ? 150 : 50, regret ? 255 : 40, 255);
        } else {
            SDL_SetRenderDrawColor(renderer, 70, 70, 80, 255);
        }
        SDL_RenderRect(renderer, &panel);
        SDL_SetRenderDrawColor(renderer, 235, 235, 240, 255);
        text(renderer, WIDTH / 2, panelY(i) + 24, 2.5F, games[i].title);
        SDL_SetRenderDrawColor(renderer, 120, 120, 130, 255);
        std::string folder = games[i].folder.string();
        if (folder.size() > 58) {
            folder = "..." + folder.substr(folder.size() - 55);
        }
        text(renderer, WIDTH / 2, panelY(i) + 64, 1, folder);
    }
    SDL_SetRenderDrawColor(renderer, 110, 110, 120, 255);
    text(renderer, WIDTH / 2, HEIGHT - 24, 1, "CLICK OR ARROWS AND ENTER CHOOSE   ESC QUITS");
    SDL_RenderPresent(renderer);
}

/* The panel under x,y; -1 for none. */
int panelAt(float x, float y, std::size_t count)
{
    for (std::size_t i = 0; i < count; i++) {
        if (x >= PANEL_X && x < PANEL_X + PANEL_W && y >= panelY(i) && y < panelY(i) + PANEL_H) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/* The window's choice: an index into games, -1 for none (Escape, closed). */
int ask(const std::vector<Game> &games)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return 0;
    }
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer(PROFILE_TITLE, static_cast<int>(WIDTH * 1.5F), static_cast<int>(HEIGHT * 1.5F),
                                     SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        SDL_Quit();
        return 0;
    }
    SDL_SetRenderLogicalPresentation(renderer, static_cast<int>(WIDTH), static_cast<int>(HEIGHT),
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    std::size_t lit = 0;
    int chosen = -2;
    while (chosen == -2) {
        draw(renderer, games, lit);
        SDL_Event event;
        if (!SDL_WaitEvent(&event)) {
            continue;
        }
        SDL_ConvertEventToRenderCoordinates(renderer, &event);
        switch (event.type) {
        case SDL_EVENT_QUIT:
            chosen = -1;
            break;
        case SDL_EVENT_MOUSE_MOTION: {
            const int at = panelAt(event.motion.x, event.motion.y, games.size());
            if (at >= 0) {
                lit = static_cast<std::size_t>(at);
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            const int at = panelAt(event.button.x, event.button.y, games.size());
            if (event.button.button == SDL_BUTTON_LEFT && at >= 0) {
                chosen = at;
            }
            break;
        }
        case SDL_EVENT_KEY_DOWN:
            switch (event.key.key) {
            case SDLK_ESCAPE:
                chosen = -1;
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
            case SDLK_SPACE:
                chosen = static_cast<int>(lit);
                break;
            case SDLK_UP:
            case SDLK_LEFT:
                lit = (lit + games.size() - 1) % games.size();
                break;
            case SDLK_DOWN:
            case SDLK_RIGHT:
                lit = (lit + 1) % games.size();
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return chosen;
}

/* The program run again in the game's folder for it; this run ends with
 * its code. */
[[noreturn]] void runIn(const Game &game, [[maybe_unused]] char *argv[])
{
#ifdef _WIN32
    _putenv_s("CRUSADER_GAME", game.game.c_str());
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    STARTUPINFOW startup = {};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process = {};
    const std::wstring folder = game.folder.wstring();
    if (CreateProcessW(path, GetCommandLineW(), nullptr, nullptr, TRUE, 0, nullptr, folder.c_str(), &startup,
                       &process) == 0) {
        std::exit(3);
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    std::exit(static_cast<int>(code));
#else
    setenv("CRUSADER_GAME", game.game.c_str(), 1);
    if (chdir(game.folder.c_str()) != 0) {
        std::exit(3);
    }
#if defined(__APPLE__)
    const std::string program = programFile().string();
#else
    const std::string program = "/proc/self/exe";
#endif
    execv(program.c_str(), argv);
    std::exit(3);
#endif
}

}

void chooseGameFirst(int argc, char *argv[])
{
    if (chosenAlready(argc, argv)) {
        return;
    }
    const std::vector<Game> games = gamesBelow(startDirectory());
    if (games.empty()) {
        return;
    }
    const int chosen = ask(games);
    if (chosen < 0) {
        std::exit(0);
    }
    runIn(games[static_cast<std::size_t>(chosen)], argv);
}
