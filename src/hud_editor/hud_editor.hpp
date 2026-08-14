// hud_editor.hpp
//
// Barony HUD Editor -- Proof of Concept (PoC), v2
// ---------------------------------------------------------------------
// v2 replaces the original approach (which reused the REAL in-game HUD
// Frame objects, and required an active paused game) with a fully
// standalone "recreation": a small set of placeholder images living
// entirely inside the pause/main menu's own widget tree, independent
// of gui_mode, gamePaused, or whether a game is even running. See
// README_TEST.md for the full explanation of why v1 was replaced.
//
// Isolation guarantee: nothing in this file is ever touched unless a
// player explicitly opens "Editor de HUD" from Options, which is the
// ONLY external entry point (see mainHudEditorPoC() below). If a
// player never opens it, HudEditorPoC::active stays false forever and
// every public method here is a no-op.
//
// Scope of this PoC (deliberately minimal -- see README_TEST.md):
//   - HUD normal only. No inventory scene.
//   - The HP bar is the only editable element, represented by a
//     single placeholder image (not the real, live-updating bar).
//   - Position only (X/Y). No scale, no visibility toggle, no snap,
//     no grid, no locking, no multi-select.
//   - A single flat save file (config/hud_editor_poc.json), read by
//     the real game's createHPMPBars() (ui/GameUI.cpp) to place the
//     real HP bar next time a game actually starts. No profiles, no
//     import/export, no "recommended" preset.
//
#pragma once

#include "../main.hpp"

class Frame;
class Button;
class Widget;

class HudEditorPoC
{
public:
	bool isActive() const { return active; }

	// Opens the standalone editor overlay for the given local player
	// index (used only to route input/ownership -- no live Player
	// state is read or modified). Safe to call repeatedly; a no-op
	// if already active. Works identically whether or not a game is
	// currently running.
	void enter(int playernum);

	// Closes the editor overlay. If save is true, the placeholder HP
	// bar's current position is written to disk first.
	void exit(int playernum, bool save);

	// Reads config/hud_editor_poc.json (if it exists) and, if it
	// contains a saved HP bar position, overwrites basePos.x/y with
	// it before the caller applies it to the real Frame. If the file
	// does not exist, can't be parsed, or has no saved position,
	// basePos is returned completely untouched. Called from the real
	// game's createHPMPBars() -- unrelated to, and unaffected by,
	// whether the editor itself is currently open.
	static SDL_Rect applySavedHpBarPosition(SDL_Rect basePos);

	// Reaplica la posicion guardada sobre el hpFrame REAL del jugador,
	// si ya existe en esta sesion (partida ya en curso). Se llama
	// desde MainMenu::closeMainMenu() (ver README_TEST.md, 3.6) para
	// que los cambios se vean apenas volves al juego, sin depender de
	// arrancar una partida nueva o reiniciar la app.
	static void reapplyToLiveHud(int playernum);

private:
	bool active = false;

	// drag state (mouse / touch -- this now runs purely inside the
	// menu widget tree, the same context "Back to Game"/"Settings"
	// already use, so no special input handling is needed)
	bool dragging = false;
	bool wasMouseDownLastTick = false;
	int grabOffsetX = 0;
	int grabOffsetY = 0;

	// editor-only widgets, created in enter()/createOverlay() and
	// fully destroyed in exit()/destroyOverlay(). Nothing here
	// outlives a single editing session, and none of it is part of
	// the real in-game HUD tree.
	Frame* blockerFrame = nullptr;
	Frame* hpPreviewFrame = nullptr;
	Frame* toolbarFrame = nullptr;

	void createOverlay(int playernum);
	void destroyOverlay();
	void savePosition();

	// Per-tick update for the drag logic. Wired up as this class's
	// own Frame::setTickCallback() on blockerFrame -- NOT called from
	// anywhere in game.cpp -- so it keeps running whether or not a
	// game is active, exactly like any other menu widget.
	static void tickCallback(Widget& widget);
};

// Pause/main-menu button callback ("Editor de HUD"). This is the
// single function ui/MainMenu.cpp needs to know about; everything
// else is reached through players[playernum]->hudEditor.
void mainHudEditorPoC(Button& button);
