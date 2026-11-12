// Tile-based cursor helpers for the tactical view (gamepad play).
// The game picks the tile under the cursor with a flat screen->cell mapping (GetMouseWorldCoords,
// ignoring terrain/roof height); everything here uses the same mapping so it agrees with the game.
#include "VitaTactical.h"
#include "Input.h"
#include "Isometric_Utils.h"
#include "RenderWorld.h"
#include "UILayout.h"
#include "WorldDef.h"

#include <algorithm>

// screen point -> (col,row) of the tile the game would pick there
static bool TileAt(int x, int y, INT16& col, INT16& row)
{
	INT16 cx, cy;
	FromScreenToCellCoordinates(x - g_ui.m_tacticalMapCenterX, y - g_ui.m_tacticalMapCenterY + 10, &cx, &cy);
	int const wx = gsRenderCenterX + cx, wy = gsRenderCenterY + cy;
	if (wx < 0 || wx >= WORLD_COORD_COLS || wy < 0 || wy >= WORLD_COORD_ROWS) return false;
	col = wx / CELL_X_SIZE;
	row = wy / CELL_Y_SIZE;
	return true;
}

// tile -> the screen point that the game maps back to that tile's centre
static void TileCenter(INT16 col, INT16 row, int& x, int& y)
{
	INT16 cx, cy, sx, sy;
	ConvertGridNoToCenterCellXY(MAPROWCOLTOPOS(row, col), &cx, &cy);
	CellXYToScreenXY(cx, cy, &sx, &sy);
	x = sx;
	y = sy - 10; // inverse of the +10 in GetMouseWorldCoords
}

static bool InViewport(int x, int y)
{
	return x >= gsVIEWPORT_START_X && x < gsVIEWPORT_END_X && y >= gsVIEWPORT_WINDOW_START_Y && y < gsVIEWPORT_WINDOW_END_Y;
}

bool VitaSnapToTile(int& x, int& y)
{
	INT16 col, row;
	if (!InViewport(x, y) || !TileAt(x, y, col, row)) return false;
	int tx, ty;
	TileCenter(col, row, tx, ty);
	if (!InViewport(tx, ty)) return false;
	x = tx; y = ty;
	return true;
}

void VitaStepTile(int sx, int sy)
{
	// screen directions are grid diagonals: right = (+1,-1), down = (+1,+1) in (col,row)
	int const x0 = gusMouseXPos, y0 = gusMouseYPos;
	INT16 col, row;
	if (!InViewport(x0, y0) || !TileAt(x0, y0, col, row)) {
		// not over the map (e.g. on the panel): plain pixel step
		SetSafeMousePosition(std::clamp(x0 + sx * 40, 0, SCREEN_WIDTH - 1), std::clamp(y0 + sy * 20, 0, SCREEN_HEIGHT - 1));
		return;
	}
	INT16 const ncol = col + sx + sy, nrow = row - sx + sy;
	if (ncol < 0 || ncol >= WORLD_COLS || nrow < 0 || nrow >= WORLD_ROWS) return;
	int tx, ty;
	TileCenter(ncol, nrow, tx, ty);

	// keep a tile's margin from the view edge; beyond that scroll the world instead of moving the cursor
	int const mx = 40, my = 40;
	if (tx < gsVIEWPORT_START_X + mx || tx >= gsVIEWPORT_END_X - mx ||
	    ty < gsVIEWPORT_WINDOW_START_Y + my || ty >= gsVIEWPORT_WINDOW_END_Y - my) {
		// pan offsets follow drag direction; whole-tile steps (40/20) are multiples of the scroll step (20)
		gsScrollXOffset -= sx * 40;
		gsScrollYOffset -= sy * 20;
		int cx = x0, cy = y0;
		if (VitaSnapToTile(cx, cy)) SetSafeMousePosition(cx, cy);
		return;
	}
	SetSafeMousePosition(tx, ty);
}


// L/R: previous/next merc in team panel order and centre on him (same as Space, which only goes forward)
#include "Handle_UI.h"
#include "Interface_Items.h"
#include "Interface_Panels.h"
#include "Overhead.h"
#include "Soldier_Control.h"

void VitaSelectAdjacentMerc(int dir)
{
	if (gTacticalStatus.uiFlags & ENGAGED_IN_CONV || InKeyRingPopup()) return;
	SOLDIERTYPE* const sel = GetSelectedMan();
	if (!sel) return;
	SOLDIERTYPE* target = FindNextMercInTeamPanel(sel);
	if (dir < 0) {
		// previous = the merc whose "next" is sel, found by walking the cycle (at most a team's worth)
		SOLDIERTYPE* s = sel;
		for (int i = 0; i < 32; ++i) {
			SOLDIERTYPE* const n = FindNextMercInTeamPanel(s);
			if (n == sel) { target = s; break; }
			if (n == s) break; // only one selectable merc
			s = n;
		}
	}
	HandleLocateSelectMerc(target, true);
	LocateSoldier(GetSelectedMan(), SETLOCATOR);
	guiPendingOverrideEvent = M_ON_TERRAIN;
}


// ---- touch shortcut buttons beside the bottom panel (wide screens leave room on both sides) ----
// They live and die with the panel (Interface.cc). Clicks only queue an action, run from VitaSideButtonsTick,
// because some of them rebuild the panel and so remove the very button being clicked.
#include "Button_System.h"
#include "Directories.h"
#include "Font.h"
#include "Font_Control.h"
#include "Interface.h"
#include "JAScreens.h"

namespace {
enum SideAction { SA_NONE, SA_END_TURN, SA_MAP, SA_OPTIONS, SA_SAVE, SA_LOAD, SA_HIDE,
                  SA_RELOAD, SA_BURST, SA_CLIMB, SA_STEALTH, SA_ROOF, SA_NEXT };
struct SideButton { char const* label; SideAction action; };
// two columns x three rows per side
SideButton const LEFT_SIDE[]  = { { "End turn", SA_END_TURN }, { "Map", SA_MAP }, { "Options", SA_OPTIONS },
                                  { "Save", SA_SAVE }, { "Load", SA_LOAD }, { "Hide panel", SA_HIDE } };
SideButton const RIGHT_SIDE[] = { { "Reload", SA_RELOAD }, { "Burst", SA_BURST }, { "Climb", SA_CLIMB },
                                  { "Stealth", SA_STEALTH }, { "Roof view", SA_ROOF }, { "Next merc", SA_NEXT } };
GUIButtonRef g_side[12];
BUTTON_PICS* g_side_pics; // tactical overhead-map buttons (69x27, same metal style as the panel)
SideAction   g_side_pending = SA_NONE;

void SideCallback(GUI_BUTTON* const btn, UINT32 const reason)
{
	if (reason & MSYS_CALLBACK_REASON_POINTER_UP) g_side_pending = SideAction(btn->GetUserData());
}
}

void VitaCreateSideButtons()
{
	int const side_w = INTERFACE_START_X;
	if (side_w < 120) return; // 640-wide screens: no room
	// bottom 120px: with the (shorter) team panel the world is drawn down to INTERFACE_START_Y
	int const top = INTERFACE_START_Y, h_all = SCREEN_HEIGHT - top;
	g_side_pics = LoadButtonImage(INTERFACEDIR "/overheaduibuttons.sti", 0, 1);
	int const bw = 69, bh = 27; // the image's size; centred in a 2x3 grid of cells
	int const cw = side_w / 2, ch = h_all / 3;
	int n = 0;
	auto place = [&](SideButton const (&list)[6], int const x0) {
		for (int i = 0; i < 6; ++i) {
			int const x = x0 + (i % 2) * cw + (cw - bw) / 2, y = top + (i / 2) * ch + (ch - bh) / 2;
			GUIButtonRef const b = CreateIconAndTextButton(g_side_pics, list[i].label, FONT12ARIAL,
				FONT_MCOLOR_WHITE, DEFAULT_SHADOW, FONT_MCOLOR_WHITE, DEFAULT_SHADOW, x, y, MSYS_PRIORITY_HIGH, SideCallback);
			b->SetUserData(list[i].action);
			g_side[n++] = b;
		}
	};
	place(LEFT_SIDE, 0);
	place(RIGHT_SIDE, INTERFACE_START_X + g_ui.m_teamPanelWidth);
}

void VitaRemoveSideButtons()
{
	for (auto& b : g_side) if (b) RemoveButton(b);
	if (g_side_pics) { UnloadButtonImage(g_side_pics); g_side_pics = nullptr; }
}

void VitaSideButtonsTick()
{
	SideAction const a = g_side_pending;
	g_side_pending = SA_NONE;
	if (a == SA_NONE || guiCurrentScreen != GAME_SCREEN) return;
	switch (a)
	{
		case SA_END_TURN: VitaTapKey(SDLK_D, SDL_KMOD_NONE);  break;
		case SA_MAP:      VitaTapKey(SDLK_M, SDL_KMOD_NONE);  break;
		case SA_OPTIONS:  VitaTapKey(SDLK_O, SDL_KMOD_NONE);  break;
		case SA_SAVE:     VitaTapKey(SDLK_S, SDL_KMOD_LCTRL); break;
		case SA_LOAD:     VitaTapKey(SDLK_L, SDL_KMOD_LCTRL); break;
		case SA_HIDE:     ToggleTacticalPanelHidden();        break;
		case SA_RELOAD:   VitaTapKey(SDLK_R, SDL_KMOD_LALT);  break;
		case SA_BURST:    VitaTapKey(SDLK_B, SDL_KMOD_NONE);  break;
		case SA_CLIMB:    VitaTapKey(SDLK_J, SDL_KMOD_NONE);  break;
		case SA_STEALTH:  VitaTapKey(SDLK_Z, SDL_KMOD_NONE);  break;
		case SA_ROOF:     VitaTapKey(SDLK_TAB, SDL_KMOD_NONE); break;
		case SA_NEXT:     VitaTapKey(SDLK_SPACE, SDL_KMOD_NONE); break;
		default: break;
	}
}
