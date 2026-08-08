// hud_editor.hpp
//
// Barony HUD Editor -- Proof of Concept (PoC)
// ---------------------------------------------------------------------
// This file, together with hud_editor.cpp, is the ENTIRE new-code
// footprint of the "Editor de HUD" proof of concept. See
// README_TEST.md at the root of this delivery for the full write-up
// (integration guide, how to test, how to revert).
//
// Isolation guarantee: nothing in this file is ever touched unless a
// player explicitly opens "Editor de HUD" from the pause menu, which
// is the ONLY external entry point (see mainHudEditorPoC() below). If
// a player never opens it, HudEditorPoC::active stays false forever
// and every public method here is a no-op (see the top of each
// function body in hud_editor.cpp).
//
// Scope of this PoC (deliberately minimal -- see README_TEST.md):
//   - HUD normal only. No inventory scene.
//   - The HP bar is the only editable element.
//   - Position only (X/Y). No scale, no visibility toggle, no snap,
//     no grid, no locking, no multi-select.
//   - A single flat save file (config/hud_editor_poc.json). No
//     profiles, no import/export, no "recommended" preset. Those are
//     designed (see the architecture document from the previous
//     phase) but intentionally NOT implemented here.
//
#pragma once

#include "../main.hpp"

class Frame;
class Button;

class HudEditorPoC
{
public:
	bool isActive() const { return active; }

	// Opens the editor overlay for the given local player index.
	// Safe to call repeatedly; a no-op if already active.
	void enter(int playernum);

	// Closes the editor overlay and restores the previous gui_mode.
	// If save is true, the HP bar's current on-screen position is
	// written to disk first.
	void exit(int playernum, bool save);

	// Must be called once per game tick for this player. Immediately
	// returns if the editor is not active for this player, so it is
	// always safe to call unconditionally from the main loop.
	void tick(int playernum);

	// Reads config/hud_editor_poc.json (if it exists) and, if it
	// contains a saved HP bar position, overwrites basePos.x/y with
	// it before the caller applies it to the real Frame. If the file
	// does not exist, can't be parsed, or has no saved position,
	// basePos is returned completely untouched -- this is what
	// guarantees "si el archivo no existe, se usa la posicion
	// original" with zero special-casing at the call site.
	static SDL_Rect applySavedHpBarPosition(SDL_Rect basePos);

private:
	bool active = false;
	int previousGuiMode = 0;

	// drag state (mouse / touch only in this PoC -- see README_TEST.md)
	bool dragging = false;
	bool wasMouseDownLastTick = false;
	int grabOffsetX = 0;
	int grabOffsetY = 0;

	// editor-only widgets, created in enter()/createOverlay() and
	// fully destroyed in exit()/destroyOverlay(). Nothing here
	// outlives a single editing session.
	Frame* highlightFrame = nullptr;
	Frame* toolbarFrame = nullptr;

	void createOverlay(int playernum);
	void destroyOverlay();
	void savePosition(int playernum);
};

// Pause-menu button callback ("Editor de HUD"). This is the single
// function ui/MainMenu.cpp needs to know about; everything else is
// reached through players[playernum]->hudEditor.
void mainHudEditorPoC(Button& button);
