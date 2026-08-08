// hud_editor.cpp
//
// See hud_editor.hpp and README_TEST.md for the full write-up.

#include "hud_editor.hpp"

#include "../main.hpp"
#include "../files.hpp"
#include "../json.hpp"
#include "../player.hpp"
#include "../input.hpp"
#include "../interface/interface.hpp"
#include "../ui/Frame.hpp"
#include "../ui/Button.hpp"
#include "../ui/MainMenu.hpp"

#include <string>

namespace {

	// On-disk schema for this PoC only. This is deliberately NOT the
	// full HudElementOverride/HudLayoutProfile schema described in the
	// architecture document -- it is the smallest possible container
	// that can prove the save/load round trip works end to end. See
	// README_TEST.md, section "Que quedo pendiente".
	struct HudEditorPoCData
	{
		bool hasPosition = false;
		Sint32 x = 0;
		Sint32 y = 0;

		bool serialize(FileInterface* file)
		{
			file->property("has_position", hasPosition);
			file->property("x", x);
			file->property("y", y);
			return true;
		}
	};

	std::string hudEditorPoCSavePath()
	{
		return std::string(outputdir) + "/config/hud_editor_poc.json";
	}

} // namespace

void HudEditorPoC::enter(int playernum)
{
	if (active)
	{
		return;
	}
	if (playernum < 0 || playernum >= MAXPLAYERS || !players[playernum])
	{
		return;
	}

	Player& player = *players[playernum];

	// Remember whatever mode the player was in (should be
	// GUI_MODE_NONE, since this PoC is only reachable from the pause
	// menu, but we restore whatever it actually was to be safe).
	previousGuiMode = player.gui_mode;
	player.gui_mode = GUI_MODE_HUD_EDITOR;

	// Hide (do NOT destroy, do NOT unpause) the pause menu so the
	// live game and its real HUD are fully visible again. gamePaused
	// is a completely separate global flag and is never touched here,
	// so gameplay logic stays frozen for the whole editing session.
	if (MainMenu::main_menu_frame)
	{
		MainMenu::main_menu_frame->setInvisible(true);
	}

	dragging = false;
	wasMouseDownLastTick = false;

	createOverlay(playernum);

	active = true;
}

void HudEditorPoC::exit(int playernum, bool save)
{
	if (!active)
	{
		return;
	}
	if (playernum < 0 || playernum >= MAXPLAYERS || !players[playernum])
	{
		return;
	}

	Player& player = *players[playernum];

	if (save)
	{
		savePosition(playernum);
	}

	destroyOverlay();

	if (MainMenu::main_menu_frame)
	{
		MainMenu::main_menu_frame->setInvisible(false);
	}

	player.gui_mode = previousGuiMode;
	dragging = false;
	active = false;
}

void HudEditorPoC::createOverlay(int playernum)
{
	Player& player = *players[playernum];
	Frame* hudFrame = player.hud.hudFrame;
	if (!hudFrame)
	{
		return;
	}

	// A hollow rectangle that tracks the HP bar's real Frame every
	// tick (see tick() below). Purely visual -- it never itself
	// receives input.
	highlightFrame = hudFrame->addFrame("hud_editor_poc_highlight");
	highlightFrame->setOwner(playernum);
	highlightFrame->setHollow(true);
	highlightFrame->setBorder(3);
	highlightFrame->setBorderColor(makeColor(255, 215, 0, 255));
	highlightFrame->setColor(0);
	highlightFrame->setDisabled(true); // never intercepts clicks

	// Minimal toolbar: a single fixed panel, top-left of the screen,
	// with one button. Deliberately drawn with flat colors only (no
	// image assets) to avoid depending on any texture path.
	toolbarFrame = hudFrame->addFrame("hud_editor_poc_toolbar");
	toolbarFrame->setOwner(playernum);
	toolbarFrame->setSize(SDL_Rect{16, 16, 260, 48});
	toolbarFrame->setBorder(2);
	toolbarFrame->setBorderColor(makeColor(255, 255, 255, 255));
	toolbarFrame->setColor(makeColor(20, 20, 20, 200));

	auto saveButton = toolbarFrame->addButton("hud_editor_poc_save_button");
	saveButton->setOwner(playernum);
	saveButton->setSize(SDL_Rect{4, 4, 252, 40});
	saveButton->setText("Guardar y salir (PoC)");
	saveButton->setFont("fonts/pixel_maz.ttf#16#2");
	saveButton->setJustify(Button::justify_t::CENTER);
	saveButton->setTextColor(makeColor(255, 255, 255, 255));
	saveButton->setTextHighlightColor(makeColor(255, 255, 0, 255));
	saveButton->setColor(0);
	saveButton->setHighlightColor(makeColor(255, 255, 255, 40));
	saveButton->setCallback([](Button& button) {
		const int player = button.getOwner();
		if (player < 0 || player >= MAXPLAYERS || !players[player])
		{
			return;
		}
		players[player]->hudEditor.exit(player, true);
	});
}

void HudEditorPoC::destroyOverlay()
{
	if (highlightFrame)
	{
		highlightFrame->removeSelf();
		highlightFrame = nullptr;
	}
	if (toolbarFrame)
	{
		// removing the parent frame also removes the button inside it
		toolbarFrame->removeSelf();
		toolbarFrame = nullptr;
	}
}

void HudEditorPoC::tick(int playernum)
{
	if (!active)
	{
		return;
	}
	if (playernum < 0 || playernum >= MAXPLAYERS || !players[playernum])
	{
		return;
	}

	Player& player = *players[playernum];
	Frame* hudFrame = player.hud.hudFrame;
	Frame* hpFrame = player.hud.hpFrame;
	if (!hudFrame || !hpFrame)
	{
		return;
	}

	// Keep the highlight aligned to the real HP bar frame every
	// single tick, whether or not it is currently being dragged --
	// this is what guarantees the highlight can never visually drift
	// away from the element it represents.
	if (highlightFrame)
	{
		const SDL_Rect hpPos = hpFrame->getSize();
		highlightFrame->setSize(SDL_Rect{hpPos.x - 4, hpPos.y - 4, hpPos.w + 8, hpPos.h + 8});
	}

	const bool mouseDown = inputs.bMouseLeft(playernum);

	// Red de seguridad: el mismo boton/tecla que abre el menu de pausa
	// siempre permite salir del editor (con guardado), sin depender de
	// que el overlay se haya podido dibujar o tocar correctamente.
	if (playernum >= 0 && playernum < MAXPLAYERS && Input::inputs[playernum].consumeBinaryToggle("Pause Game"))
	{
		exit(playernum, true);
		return;
	}

	if (dragging)
	{
		if (!mouseDown)
		{
			dragging = false;
		}
		else
		{
			// getRelativeMousePosition() returns the cursor position in
			// the SAME coordinate space as hudFrame's own children's
			// SDL_Rect (i.e. the same space hpFrame->getSize() lives
			// in), so no manual scale conversion is required here.
			const SDL_Rect cursorRel = hudFrame->getRelativeMousePosition(true);
			SDL_Rect newPos = hpFrame->getSize();
			newPos.x = cursorRel.x - grabOffsetX;
			newPos.y = cursorRel.y - grabOffsetY;
			hpFrame->setSize(newPos);
		}
	}
	else if (mouseDown && !wasMouseDownLastTick && hpFrame->capturesMouseInRealtimeCoords())
	{
		// bMouseLeft() is a level (held) signal, not an edge signal,
		// so the edge is computed manually here via
		// wasMouseDownLastTick. This is what stops a held click from
		// re-triggering drag-start logic every tick.
		const SDL_Rect cursorRel = hudFrame->getRelativeMousePosition(true);
		const SDL_Rect hpPosNow = hpFrame->getSize();
		grabOffsetX = cursorRel.x - hpPosNow.x;
		grabOffsetY = cursorRel.y - hpPosNow.y;
		dragging = true;
	}

	wasMouseDownLastTick = mouseDown;
}

void HudEditorPoC::savePosition(int playernum)
{
	if (playernum < 0 || playernum >= MAXPLAYERS || !players[playernum])
	{
		return;
	}
	Frame* hpFrame = players[playernum]->hud.hpFrame;
	if (!hpFrame)
	{
		return;
	}

	const SDL_Rect pos = hpFrame->getSize();

	HudEditorPoCData data;
	data.hasPosition = true;
	data.x = pos.x;
	data.y = pos.y;

	const std::string path = hudEditorPoCSavePath();
	FileHelper::writeObject(path.c_str(), EFileFormat::Json, data);
}

SDL_Rect HudEditorPoC::applySavedHpBarPosition(SDL_Rect basePos)
{
	HudEditorPoCData data;
	const std::string path = hudEditorPoCSavePath();
	if (!FileHelper::readObject(path.c_str(), data))
	{
		// File missing / unreadable / corrupt: keep the position the
		// game already computed. This is the "usar la posicion
		// original si el archivo no existe" requirement, satisfied
		// with no special-casing at the call site.
		return basePos;
	}
	if (!data.hasPosition)
	{
		return basePos;
	}

	SDL_Rect result = basePos;
	result.x = data.x;
	result.y = data.y;
	return result;
}

void mainHudEditorPoC(Button& button)
{
	Player::soundActivate();

	const int player = MainMenu::getMenuOwner();
	if (player < 0 || player >= MAXPLAYERS || !players[player])
	{
		return;
	}

	players[player]->hudEditor.enter(player);
}
