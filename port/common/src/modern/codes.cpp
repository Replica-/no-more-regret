/* The codes read on the terminals. See codes.h. */
#include "codes.h"
#include "gamestate.h"
#include "viewport.h"
#include "widgets.h"
#include "widgets_game.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

/* The codes seen, the arena's (a saved game keeps them). */
const int MOST = 12;
const int DIGITS = 7;
const int LABEL = 40;
struct Code {
    char digits[DIGITS];
    char label[LABEL];
};
struct Codes {
    u8 count;
    Code rows[MOST];
};
Codes &codes = gameGlobal<Codes>();

/* The list's size: its width, a row's height, the room above the first
 * row (the title) and round it. */
const i16 WIDTH = 250;
const i16 GAP = 8;
const i16 ROW = 13;
const i16 TOP = 22;
const i16 PAD = 8;

const char *const WORDS[] = {"passcode", "pass code", "code", "combination", "keypad", "pin", "password"};

std::string lower(const std::string &text)
{
    std::string out = text;
    for (char &c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

/* The text in capitals without the terminal's pauses (*), line breaks (^)
 * and control codes, and without the punctuation at its ends. */
std::string clean(const std::string &text)
{
    std::string out;
    for (const char c : text) {
        if (c == '*' || c == '^' || static_cast<unsigned char>(c) < ' ') {
            if (!out.empty() && out.back() != ' ') {
                out += ' ';
            }
            continue;
        }
        out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    const char *const ends = " :-=.,#_<(";
    const std::size_t first = out.find_first_not_of(ends);
    const std::size_t last = out.find_last_not_of(ends);
    return first == std::string::npos ? std::string() : out.substr(first, last - first + 1);
}

bool isDigit(const std::string &text, std::size_t i)
{
    return i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])) != 0;
}

/* A letter, a digit or a dash: part of a longer name or number. */
bool attached(const std::string &text, std::size_t i)
{
    return i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) != 0 || text[i] == '-');
}

/* What the code opens: the words after "<code word> to" up to "has", "is",
 * "was", "been" or a colon; empty when the text does not say. */
std::string labelOf(const std::string &text, const std::string &before, std::size_t from, std::size_t wordAt,
                    std::size_t wordLength)
{
    const std::size_t to = before.find(" to ", wordAt + wordLength);
    if (to == std::string::npos || to > wordAt + wordLength + 1) {
        return {};
    }
    std::size_t stop = before.size();
    for (const char *const end : {" has ", " is ", " was ", " been ", ":", " changed", " ^", "^"}) {
        const std::size_t at = before.find(end, to + 4);
        if (at != std::string::npos) {
            stop = std::min(stop, at);
        }
    }
    return clean(text.substr(from + to + 4, stop - (to + 4)));
}

void keep(const std::string &digits, std::string label)
{
    for (const char *const filler : {"THE ", "FROM: ", "FROM "}) {
        if (label.rfind(filler, 0) == 0) {
            label.erase(0, std::strlen(filler));
        }
    }
    int at = 0;
    while (at < codes.count && digits != codes.rows[at].digits) {
        at++;
    }
    if (at == codes.count) {
        at = std::min<int>(codes.count, MOST - 1);
        if (codes.count < MOST) {
            codes.count++;
        }
    }
    /* The rows above it down one, it first. */
    for (int i = at; i > 0; i--) {
        codes.rows[i] = codes.rows[i - 1];
    }
    Code &row = codes.rows[0];
    std::snprintf(row.digits, sizeof(row.digits), "%s", digits.c_str());
    std::snprintf(row.label, sizeof(row.label), "%s", label.c_str());
}

}

void noteCodes(const char *source)
{
    const std::string text(source);
    const std::string low = lower(text);
    /* The first line (a title or the sender), for a code that does not say
     * what it opens. */
    std::string title;
    for (std::size_t i = 0; i < text.size() && text[i] != '^' && text[i] != '\n'; i++) {
        title += text[i];
    }
    title = clean(title);
    for (std::size_t i = 0; i < text.size();) {
        if (!isDigit(text, i)) {
            i++;
            continue;
        }
        std::size_t end = i;
        while (isDigit(text, end)) {
            end++;
        }
        const bool alone = (i == 0 || !attached(text, i - 1)) && !attached(text, end);
        if (alone && end - i >= 3 && end - i <= 6) {
            const std::size_t from = i > 160 ? i - 160 : 0;
            const std::string before = low.substr(from, i - from);
            const std::string after = low.substr(end, 40);
            std::size_t wordAt = std::string::npos;
            std::size_t wordLength = 0;
            bool found = false;
            for (const char *const word : WORDS) {
                const std::size_t at = before.rfind(word);
                if (at != std::string::npos && (wordAt == std::string::npos || at > wordAt)) {
                    wordAt = at;
                    wordLength = std::strlen(word);
                }
                found = found || after.find(word) != std::string::npos;
            }
            if (found || wordAt != std::string::npos) {
                std::string label;
                if (wordAt != std::string::npos) {
                    label = labelOf(text, before, from, wordAt, wordLength);
                }
                keep(text.substr(i, end - i), label.empty() ? title : label);
            }
        }
        i = end;
    }
}

i16 codesWidth()
{
    return codes.count == 0U ? 0 : static_cast<i16>(GAP + WIDTH);
}

void paintCodes(i16 x, i16 y)
{
    if (codes.count == 0U) {
        return;
    }
    const i16 left = static_cast<i16>(x + GAP);
    const i16 bottom = static_cast<i16>(y + TOP + (codes.count * ROW) + 6);
    drawViewport->clear(0, left, y, static_cast<s16>(left + WIDTH - 1), bottom);
    drawViewport->rectangle(WIDGET_FOCUS, left, y, static_cast<s16>(left + WIDTH - 1), bottom);
    drawText(CHOICE_FONT, "CODES SEEN", static_cast<s16>(left + PAD), static_cast<s16>(y + 7),
             Ink{WIDGET_WARNING_FACE, WIDGET_WARNING_EDGE});
    for (int i = 0; i < codes.count; i++) {
        /* Its digits, then as much of its label as fits. */
        std::string line = std::string(codes.rows[i].digits) + "  " + codes.rows[i].label;
        while (line.size() > std::strlen(codes.rows[i].digits) &&
               textExtent(CHOICE_FONT, line).width > WIDTH - (2 * PAD)) {
            line.pop_back();
        }
        drawText(CHOICE_FONT, line, static_cast<s16>(left + PAD), static_cast<s16>(y + TOP + (i * ROW)),
                 Ink{WIDGET_FACE, WIDGET_EDGE});
    }
}

i16 codeAt(i16 x, i16 y, i16 *digits, i16 most)
{
    if (codes.count == 0U || x < GAP || x >= GAP + WIDTH || y < TOP) {
        return 0;
    }
    const int row = (y - TOP) / ROW;
    if (row >= codes.count) {
        return 0;
    }
    i16 count = 0;
    for (const char *c = codes.rows[row].digits; *c != '\0' && count < most; c++) {
        digits[count++] = static_cast<i16>(*c - '0');
    }
    return count;
}
