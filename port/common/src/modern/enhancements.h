/* The port's enhancements page of the settings (settings.h's ENHANCEMENTS
 * button): the keyboard and mouse controls, the lighting, the wide
 * screen and its zoom, the Silencer's colours, the walk's path line and
 * the pickups shown over their places, kept in enhanced.cfg. */
#ifndef PORT_ENHANCEMENTS_H
#define PORT_ENHANCEMENTS_H
#include "settings.h"

#include <string>

/* The settings' button that opens the page. */
[[nodiscard]] Setting enhancementsSetting();
/* The page's columns. */
[[nodiscard]] SettingColumns enhancementColumns();

/* The walk's path line (hud.cpp) and the pickups shown over their places
 * (popups.h), on by default. */
[[nodiscard]] bool pathLineSetting();
[[nodiscard]] bool pickupsNearSetting();
/* Their lines of enhanced.cfg, and a line read: true when it was one of
 * theirs. */
[[nodiscard]] std::string enhancementsText();
bool readEnhancement(const std::string &key, const std::string &value);
#endif
