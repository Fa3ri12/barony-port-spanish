// hud_editor.cpp
//
// See hud_editor.hpp and README_TEST.md for the full write-up.

#include "hud_editor.hpp"

#include "../main.hpp"
#include "../files.hpp"
#include "../json.hpp"
#include "../player.hpp"
#include "../input.hpp"
#include "../ui/Frame.hpp"
#include "../ui/Field.hpp"
#include "../ui/Button.hpp"
#include "../ui/Widget.hpp"
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

	// Same defaults the real game uses for the HP bar
	// (Player::HUD_t::HPMP_FRAME_START_X/Y in player.hpp) so that,
	// with nothing saved yet, the placeholder starts out exactly
	// where the real bar would.
	constexpr int kDefaultHpBarX = 14;
	constexpr int kDefaultHpBarY = 106;
	constexpr int kHpBarWidth = 222;
	constexpr int kHpBarHeight = 26;

} // namespace

void HudEditorPoC::enter(int playernum)
{
	if (active)
	{
		return;
	}

	dragging = false;
	wasMouseDownLastTick = false;

	createOverlay(playernum);

	active = true;
}

void HudEditorPoC::exit(int playernum, bool save)
{
	(void)playernum;

	if (!active)
	{
		return;
	}

	if (save)
	{
		savePosition();
	}

	destroyOverlay();

	dragging = false;
	active = false;
}

void HudEditorPoC::createOverlay(int playernum)
{
	if (!MainMenu::main_menu_frame)
	{
		return;
	}

	// Deshabilitar explicitamente todo lo que ya existia en el menu
	// (lista de botones, logo, etc.) mientras el editor esta abierto.
	// Agregar el blocker encima NO alcanza por si solo: en las zonas
	// donde el blocker no dibuja nada propio (la mayor parte de la
	// pantalla), un toque ahi cae directamente sobre lo que haya
	// debajo -- por eso se podia seguir tocando "Volver al juego"
	// aunque el editor estuviera abierto. Deshabilitar cada hijo
	// existente bloquea esos clicks sin importar donde caigan.
	for (Frame* child : MainMenu::main_menu_frame->getFrames())
	{
		child->setDisabled(true);
	}

	// Fully transparent full-screen frame, added as a child of the
	// pause/main menu's own root -- the same pattern mainSettings()
	// already uses to show the Options window over the button list
	// without hiding or destroying main_menu_frame (see
	// README_TEST.md, section 3.8). It also drives the drag logic
	// every tick via setTickCallback -- which keeps running
	// regardless of whether a game is active, exactly like any other
	// menu widget.
	blockerFrame = MainMenu::main_menu_frame->addFrame("hud_editor_poc_blocker");
	blockerFrame->setOwner(playernum);
	blockerFrame->setSize(SDL_Rect{0, 0, Frame::virtualScreenX, Frame::virtualScreenY});
	blockerFrame->setActualSize(blockerFrame->getSize());
	blockerFrame->setColor(0);
	blockerFrame->setBorder(0);
	blockerFrame->setTickCallback(&HudEditorPoC::tickCallback);

	// Starting position: whatever was last saved, or the same default
	// the real HP bar uses if nothing was ever saved yet.
	SDL_Rect startPos{kDefaultHpBarX, kDefaultHpBarY, kHpBarWidth, kHpBarHeight};
	startPos = applySavedHpBarPosition(startPos);

	// The placeholder itself: a plain, clearly-labeled red box (not a
	// game asset -- avoids depending on how an unfamiliar sprite
	// happens to look once tinted). This is a self-contained
	// recreation, never the real in-game hpFrame.
	hpPreviewFrame = blockerFrame->addFrame("hud_editor_poc_hp_preview");
	hpPreviewFrame->setOwner(playernum);
	hpPreviewFrame->setSize(startPos);
	hpPreviewFrame->setHollow(false);
	hpPreviewFrame->setColor(makeColor(190, 30, 30, 255));
	hpPreviewFrame->setBorder(3);
	hpPreviewFrame->setBorderColor(makeColor(255, 215, 0, 255));

	auto hpLabel = hpPreviewFrame->addField("hud_editor_poc_hp_preview_label", 32);
	hpLabel->setSize(SDL_Rect{0, 0, kHpBarWidth, kHpBarHeight});
	hpLabel->setText("BARRA DE VIDA");
	hpLabel->setFont("fonts/pixel_maz.ttf#12#2");
	hpLabel->setJustify(Field::justify_t::CENTER);
	hpLabel->setTextColor(makeColor(255, 255, 255, 255));

	// Minimal toolbar: a single fixed panel, top-left of the screen,
	// with one button. Deliberately drawn with flat colors only (no
	// image assets) to avoid depending on any texture path.
	toolbarFrame = blockerFrame->addFrame("hud_editor_poc_toolbar");
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
	if (blockerFrame)
	{
		// removing the blocker also removes hpPreviewFrame and
		// toolbarFrame, both of which are its children
		blockerFrame->removeSelf();
		blockerFrame = nullptr;
	}
	hpPreviewFrame = nullptr;
	toolbarFrame = nullptr;

	// Reactivar todo lo que se deshabilito en createOverlay() para
	// bloquear los clicks hacia el menu de pausa/principal.
	if (MainMenu::main_menu_frame)
	{
		for (Frame* child : MainMenu::main_menu_frame->getFrames())
		{
			child->setDisabled(false);
		}
	}
}

void HudEditorPoC::tickCallback(Widget& widget)
{
	const int playernum = widget.getOwner();
	if (playernum < 0 || playernum >= MAXPLAYERS || !players[playernum])
	{
		return;
	}

	HudEditorPoC& self = players[playernum]->hudEditor;
	if (!self.active || !self.blockerFrame || !self.hpPreviewFrame)
	{
		return;
	}

	// El boton B del gamepad virtual (binding "MenuCancel", el mismo
	// que usa el resto de los menus del juego para "atras/cancelar")
	// tambien sirve para guardar y salir del editor.
	if (Input::inputs[playernum].consumeBinaryToggle("MenuCancel"))
	{
		self.exit(playernum, true);
		return;
	}

	const bool mouseDown = inputs.bMouseLeft(playernum);

	if (self.dragging)
	{
		if (!mouseDown)
		{
			self.dragging = false;
		}
		else
		{
			// getRelativeMousePosition() returns the cursor position in
			// the SAME coordinate space as blockerFrame's own children's
			// SDL_Rect (i.e. the same space hpPreviewFrame->getSize()
			// lives in), so no manual scale conversion is required here.
			const SDL_Rect cursorRel = self.blockerFrame->getRelativeMousePosition(true);
			SDL_Rect newPos = self.hpPreviewFrame->getSize();
			newPos.x = cursorRel.x - self.grabOffsetX;
			newPos.y = cursorRel.y - self.grabOffsetY;
			self.hpPreviewFrame->setSize(newPos);
		}
	}
	else if (mouseDown && !self.wasMouseDownLastTick && self.hpPreviewFrame->capturesMouseInRealtimeCoords())
	{
		// bMouseLeft() is a level (held) signal, not an edge signal,
		// so the edge is computed manually via wasMouseDownLastTick.
		const SDL_Rect cursorRel = self.blockerFrame->getRelativeMousePosition(true);
		const SDL_Rect hpPosNow = self.hpPreviewFrame->getSize();
		self.grabOffsetX = cursorRel.x - hpPosNow.x;
		self.grabOffsetY = cursorRel.y - hpPosNow.y;
		self.dragging = true;
	}

	self.wasMouseDownLastTick = mouseDown;
}

void HudEditorPoC::savePosition()
{
	if (!hpPreviewFrame)
	{
		return;
	}

	const SDL_Rect pos = hpPreviewFrame->getSize();

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
		// caller already computed. This is the "usar la posicion
		// original si el archivo no existe" requirement, satisfied
		// with no special-casing at the call site -- and it applies
		// equally to the real game's HP bar and to the editor's own
		// placeholder, since both call this same function.
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
	(void)button;

	Player::soundActivate();

	const int player = MainMenu::getMenuOwner();
	if (player < 0 || player >= MAXPLAYERS || !players[player])
	{
		return;
	}

	players[player]->hudEditor.enter(player);
}
