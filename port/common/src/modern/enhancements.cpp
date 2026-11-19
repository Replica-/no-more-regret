/* The port's enhancements page. See enhancements.h. */
#include "enhancements.h"
#include "gamestate.h"
#include "colours.h"
#include "lighting.h"
#include "pointer.h"
#include "screen.h"

namespace {

bool &hardwareOn = gameSetting<bool>(true);
bool &pickupsNearOn = gameSetting<bool>(true);
bool &upscaledOn = gameSetting<bool>(true);

/* An on and off setting of a flag. */
Setting flagSetting(Words heading, bool &flag)
{
    bool *const on = &flag;
    return Setting{heading,
                   onOffChoices(),
                   0,
                   [on]() { return static_cast<i16>(*on ? 0 : 1); },
                   [on](i16 choice) { *on = choice == 0; },
                   nullptr};
}

Setting pointerControlsSetting()
{
    return Setting{Words{"KEYBOARD AND MOUSE", "키보드와 마우스"},
                   onOffChoices(),
                   0,
                   []() { return static_cast<i16>(pointerSetting() ? 0 : 1); },
                   [](i16 choice) { setPointerSetting(choice == 0); },
                   nullptr};
}

Setting lightingChoice()
{
    return Setting{Words{"LIGHTING", "조명"},
                   onOffChoices(),
                   0,
                   []() { return static_cast<i16>(lightingSetting() ? 0 : 1); },
                   [](i16 choice) { setLightingSetting(choice == 0); },
                   nullptr};
}

/* The wide screen: off (640x480), the native resolution (the world all
 * round the screen), or that dimmed. */
Setting nativeSetting()
{
    return Setting{Words{"NATIVE RESOLUTION", "네이티브 해상도"},
                   {SettingChoice{Words{"OFF", "끔"}, true}, SettingChoice{Words{"ON", "켬"}, false},
                    SettingChoice{Words{"DIM", "어둡게"}, false}},
                   0,
                   []() { return static_cast<i16>(screenWide()); },
                   [](i16 choice) { screenSetWide(static_cast<u8>(choice)); },
                   nullptr};
}

/* The native resolution's pixel size: the largest whole one that fits
 * (AUTO), or at most 1 to 4 (screen.h's screenSetZoom). */
Setting zoomSetting()
{
    return Setting{Words{"ZOOM", "확대"},
                   {SettingChoice{Words{"AUTO", "자동"}, false}, SettingChoice{Words{"X1", "X1"}, false},
                    SettingChoice{Words{"X2", "X2"}, false}, SettingChoice{Words{"X3", "X3"}, false},
                    SettingChoice{Words{"X4", "X4"}, false}},
                   0,
                   []() { return static_cast<i16>(screenZoom()); },
                   [](i16 choice) { screenSetZoom(static_cast<u8>(choice)); },
                   nullptr};
}

Setting hardwareChoice()
{
    return Setting{Words{"HARDWARE SCENE", "하드웨어 장면"},
                   onOffChoices(),
                   0,
                   []() { return static_cast<i16>(hardwareOn ? 0 : 1); },
                   [](i16 choice) { hardwareOn = choice == 0; },
                   nullptr};
}

Setting coloursSetting()
{
    return Setting{Words{"CHARACTER COLOURS", "캐릭터 색상"},
                   {SettingChoice{Words{"CHOOSE", "선택"}, false}},
                   0,
                   []() { return static_cast<i16>(0); },
                   [](i16 /*choice*/) {},
                   []() { return coloursGump(); }};
}

}

Setting enhancementsSetting()
{
    return Setting{Words{"ENHANCEMENTS", "개선 사항"},
                   {SettingChoice{Words{"SETTINGS", "설정"}, false}},
                   0,
                   []() { return static_cast<i16>(0); },
                   [](i16 /*choice*/) {},
                   []() -> NewGump * { return new SettingsGump(enhancementColumns()); }};
}

SettingColumns enhancementColumns()
{
    return SettingColumns{{pointerControlsSetting(), hardwareChoice(), lightingChoice(), nativeSetting(), zoomSetting(),
                           coloursSetting()},
                          {flagSetting(Words{"PICKUPS NEAR SOURCE", "획득 표시 위치"}, pickupsNearOn),
                           flagSetting(Words{"UPSCALED TEXTURES", "업스케일 텍스처"}, upscaledOn)}};
}

bool hardwareSetting()
{
    return hardwareOn;
}

void setHardwareSetting(bool on)
{
    hardwareOn = on;
}

bool pickupsNearSetting()
{
    return pickupsNearOn;
}

bool upscaledSetting()
{
    return upscaledOn;
}

std::string enhancementsText()
{
    std::string text;
    text += std::string("hardware=") + (hardwareOn ? "1" : "0") + "\r\n";
    text += std::string("pickups_near=") + (pickupsNearOn ? "1" : "0") + "\r\n";
    text += std::string("upscaled=") + (upscaledOn ? "1" : "0") + "\r\n";
    text += "zoom=" + std::to_string(screenZoom()) + "\r\n";
    return text;
}

bool readEnhancement(const std::string &key, const std::string &value)
{
    if (key == "hardware") {
        hardwareOn = value != "0";
        return true;
    }
    /* The walk's path line, which is gone (walk_path=, and path_line=
     * before it): dropped. */
    if (key == "walk_path" || key == "path_line") {
        return true;
    }
    if (key == "pickups_near") {
        pickupsNearOn = value != "0";
        return true;
    }
    if (key == "upscaled") {
        upscaledOn = value != "0";
        return true;
    }
    if (key == "zoom") {
        screenSetZoom(static_cast<u8>(value.size() == 1 && value[0] >= '0' && value[0] <= '4' ? value[0] - '0' : 0));
        return true;
    }
    return false;
}
