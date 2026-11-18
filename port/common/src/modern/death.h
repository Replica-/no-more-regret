/* The Silencer's death ends the game: the original leaves him lying dead,
 * the world going on, until a game is loaded from the menu. Once a hit has
 * killed him and he has lain dead and still a moment, the port says so over the world (SILENCER
 * TERMINATED) and offers to load a game or to go to the main menu. It is
 * the port's controls' (pointer.h's setting, which the tests' original
 * controls turn off): with the original's, the original's death. */
#ifndef PORT_DEATH_H
#define PORT_DEATH_H
#include "common.h"

/* A hit killed him (crusader.cpp's beginDeath). */
void noteSilencerKilled();
/* A tick of the controller (Controller::run). */
void deathTick();
/* Draws the screen while it is open; true then (hud.cpp). */
bool drawDeath();
#endif
