/* The choice of the game, before anything else (main): when nothing has
 * chosen it (no CRUSADER_GAME, no port option such as --game or --replay)
 * and the program does not start in a game's folder, but the folders below
 * the one it starts in (and below its cd folder, as the repository's
 * cd/noregret and cd/noremorse) hold the games' data (STATIC) and their
 * original's program (REGRET.EXE, CRUSADER.EXE), a window shows the games
 * found to choose from; the program then runs again in the folder chosen
 * for its game (restart.h's way), and this run ends with that one's code.
 * Escape or closing the window ends the run. */
#ifndef PORT_SPLASH_H
#define PORT_SPLASH_H

/* Shows the choice when it is wanted; returns only when it is not (the run
 * goes on as it would have). */
void chooseGameFirst(int argc, char *argv[]);
#endif
