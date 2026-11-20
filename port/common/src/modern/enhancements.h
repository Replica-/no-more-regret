/* The port's enhancements page of the settings (settings.h's ENHANCEMENTS
 * button): the keyboard and mouse controls, the lighting, the wide
 * screen and its zoom, the Silencer's colours, the pickups shown over
 * their places and the upscaled textures, kept in enhanced.cfg. */
#ifndef PORT_ENHANCEMENTS_H
#define PORT_ENHANCEMENTS_H
#include "settings.h"

#include <string>

/* The settings' button that opens the page. */
[[nodiscard]] Setting enhancementsSetting();
/* The page's columns. */
[[nodiscard]] SettingColumns enhancementColumns();

/* The hardware scene: the Silencer's colours and the lighting drawn by
 * the graphics card in full colour (colours.h, lighting.h), on by default
 * (off with the original controls, the tests' screens the art's). */
[[nodiscard]] bool hardwareSetting();
void setHardwareSetting(bool on);
/* The pickups shown over their places (popups.h, on by default). */
[[nodiscard]] bool pickupsNearSetting();
/* The upscaled textures (upscaled.h, on by default; with the hardware
 * scene only). */
[[nodiscard]] bool upscaledSetting();
/* The frames a second shown at the window's top right (host.h's
 * hostShowFps, on by default). */
[[nodiscard]] bool fpsSetting();
/* Their lines of enhanced.cfg, and a line read: true when it was one of
 * theirs. */
[[nodiscard]] std::string enhancementsText();
bool readEnhancement(const std::string &key, const std::string &value);
#endif
