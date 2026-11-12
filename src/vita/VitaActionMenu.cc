// Context action menu for the tactical view: triangle opens a 3x3 grid around the cursor, eight actions
// (one per direction) with Cancel in the middle. D-pad / left stick pick, the confirm button runs, circle or
// triangle closes; tapping an entry on the touch screen runs it too.
// The actions depend on what is under the cursor: ground, own merc, enemy, other person, door.
// Every action goes through the game's own UI events, hotkeys or menus, so rules (APs, range...) are the game's.
#include "VitaTactical.h"
#include "Animation_Control.h"
#include "Animation_Data.h"
#include "Button_System.h"
#include "Cursors.h"
#include "Isometric_Utils.h"
#include "Font_Control.h"
#include "Handle_UI.h"
#include "Interface.h"
#include "Interface_Control.h"
#include "Interface_Items.h"
#include "JAScreens.h"
#include "Overhead.h"
#include "Render_Dirty.h"
#include "RenderWorld.h"
#include "Soldier_Control.h"
#include "Soldier_Find.h"
#include "Structure.h"
#include "Structure_Internals.h"
#include "UILayout.h"
#include "Video.h"

#include <SDL3/SDL_keycode.h>
#include <algorithm>
#include <cmath>

namespace {

struct Action
{
	char const* label = nullptr; // nullptr = empty slot
	void (*run)() = nullptr;
	bool enabled = true;
};

// slot order: up, up-right, right, down-right, down, down-left, left, up-left
int const DIR_X[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
int const DIR_Y[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

Action       g_slots[8];
GUIButtonRef g_buttons[8];
GUIButtonRef g_cancel;
int          g_hx, g_hy;  // highlighted cell, -1..1 (0,0 = Cancel)
int          g_x0, g_y0, g_w, g_h; // menu bounds on screen
MOUSE_REGION g_overlay;   // swallows clicks on the world while the wheel is up
bool         g_open;
SOLDIERTYPE* g_target;    // person under the cursor when the wheel opened
int          g_door_slot = -1;  // door action waiting for the door menu
int          g_door_wait;       // frames left to wait for it

int SlotOf(int dx, int dy)
{
	for (int i = 0; i < 8; ++i) if (DIR_X[i] == dx && DIR_Y[i] == dy) return i;
	return -1;
}

void SelectTarget()
{
	if (g_target && g_target != GetSelectedMan()) SelectSoldier(g_target, SELSOLDIER_NONE);
}

// switch to a cursor mode, then click the target under the cursor
void ModeClick(UIEventKind const e) { guiPendingOverrideEvent = e; VitaQueueClick(); }

// movement speed/stance as the movement menu sets it (UIHandleMovementMenu), then click the tile to go there
void MoveRun()
{
	SOLDIERTYPE* const sel = GetSelectedMan();
	if (!sel) return;
	if (sel->usUIMovementMode != WALKING && sel->usUIMovementMode != RUNNING) {
		UIHandleSoldierStanceChange(sel, ANIM_STAND);
		sel->fUIMovementFast = 1;
	} else {
		sel->fUIMovementFast = 1;
		sel->usUIMovementMode = RUNNING;
		gfPlotNewMovement = TRUE;
	}
	ModeClick(A_CHANGE_TO_MOVE);
}
void MoveStance(INT8 const stance)
{
	if (SOLDIERTYPE* const sel = GetSelectedMan()) UIHandleSoldierStanceChange(sel, stance);
	ModeClick(A_CHANGE_TO_MOVE);
}
void MoveWalk()  { MoveStance(ANIM_STAND);  }
void MoveSneak() { MoveStance(ANIM_CROUCH); }
void MoveCrawl() { MoveStance(ANIM_PRONE);  }

void PickUp()  { ModeClick(HC_ON_TERRAIN); }
void Look()    { ModeClick(LC_CHANGE_TO_LOOK); }
void Attack()  { ModeClick(M_CHANGE_TO_ACTION); }
void Talk()    { ModeClick(T_CHANGE_TO_TALKING); }
void Climb()   { VitaTapKey(SDLK_J, SDL_KMOD_NONE); }
void Stealth() { VitaTapKey(SDLK_Z, SDL_KMOD_NONE); }
void Burst()   { VitaTapKey(SDLK_B, SDL_KMOD_NONE); }

void Stand()   { SelectTarget(); VitaTapKey(SDLK_S, SDL_KMOD_NONE); }
void Crouch()  { SelectTarget(); VitaTapKey(SDLK_C, SDL_KMOD_NONE); }
void Prone()   { SelectTarget(); VitaTapKey(SDLK_P, SDL_KMOD_NONE); }
void Reload()  { SelectTarget(); VitaTapKey(SDLK_R, SDL_KMOD_LALT); }
void Bandage() { VitaTapKey(SDLK_A, SDL_KMOD_NONE); }
void Swap()    { VitaTapKey(SDLK_X, SDL_KMOD_NONE); } // swaps the selected merc with the one under the cursor
void Assign()  { PopupAssignmentMenuInTactical(); }
void Inventory()
{
	if (gfTacticalPanelHidden) ToggleTacticalPanelHidden();
	SetNewPanel(g_target);
}

// doors: walk up with the hand cursor; the game then pops its door menu, where we press the chosen entry
template<int slot> void Door() { ModeClick(HC_ON_TERRAIN); g_door_slot = slot; g_door_wait = 30 * 30; }

void Fill(SOLDIERTYPE* const sel)
{
	for (auto& a : g_slots) a = Action{};
	auto set = [](int i, char const* label, void (*run)(), bool enabled = true) { g_slots[i] = Action{ label, run, enabled }; };

	GridNo const grid = guiCurrentCursorGridNo;
	SOLDIERTYPE* const t = g_target;

	if (t && guiUIFullTargetFlags & OWNED_MERC) {
		bool const vehicle = t->uiStatusFlags & SOLDIER_VEHICLE;
		set(0, "Stand", Stand, !vehicle);
		set(1, "Reload", Reload, !vehicle);
		set(2, "Crouch", Crouch, !vehicle);
		set(3, "Prone", Prone, !vehicle);
		set(4, "Auto bandage", Bandage);
		set(5, "Swap places", Swap, t != sel && !vehicle);
		set(6, "Assignment", Assign, !vehicle);
		set(7, "Inventory", Inventory, !vehicle);
	} else if (t && guiUIFullTargetFlags & ENEMY_MERC) {
		set(0, "Attack", Attack);
		set(1, "Burst on/off", Burst);
		set(2, "Look", Look);
	} else if (t) {
		set(0, "Talk", Talk);
		set(1, "Look", Look);
		set(3, "Attack", Attack);
	} else if (grid != NOWHERE && FindStructure(grid, STRUCTURE_ANYDOOR)) {
		set(0, "Open",      Door<VITA_DOOR_OPEN>);
		set(1, "Examine",   Door<VITA_DOOR_EXAMINE>);
		set(2, "Lockpick",  Door<VITA_DOOR_LOCKPICK>);
		set(3, "Kick",      Door<VITA_DOOR_BOOT>);
		set(4, "Use key",   Door<VITA_DOOR_KEY>);
		set(5, "Untrap",    Door<VITA_DOOR_UNTRAP>);
		set(6, "Crowbar",   Door<VITA_DOOR_CROWBAR>);
		set(7, "Explosive", Door<VITA_DOOR_EXPLOSIVE>);
	} else {
		set(0, "Run",     MoveRun);
		set(1, "Walk",    MoveWalk);
		set(2, "Sneak",   MoveSneak);
		set(3, "Crawl",   MoveCrawl);
		set(4, "Pick up", PickUp);
		set(5, "Look",    Look);
		set(6, "Climb",   Climb);
		set(7, "Stealth", Stealth);
	}
}

void Close()
{
	if (!g_open) return;
	for (auto& b : g_buttons) if (b) RemoveButton(b);
	if (g_cancel) RemoveButton(g_cancel);
	MSYS_RemoveRegion(&g_overlay);
	g_open = false;
	SetRenderFlags(RENDER_FLAG_FULL);
}

void Run(int const i)
{
	void (*run)() = i >= 0 && g_slots[i].label && g_slots[i].enabled ? g_slots[i].run : nullptr;
	Close();
	if (run) run();
}

void Highlight()
{
	int const sel = SlotOf(g_hx, g_hy);
	auto mark = [](GUIButtonRef const& b, bool const on) {
		if (!b || on == b->Clicked()) return;
		b->uiFlags = on ? b->uiFlags | BUTTON_CLICKED_ON : b->uiFlags & ~BUTTON_CLICKED_ON;
		MarkAButtonDirty(b);
	};
	for (int i = 0; i < 8; ++i) mark(g_buttons[i], i == sel);
	mark(g_cancel, sel < 0);
}

// touch: tapping an entry runs it, tapping Cancel closes
void EntryCallback(GUI_BUTTON* const btn, UINT32 const reason)
{
	if (!(reason & MSYS_CALLBACK_REASON_POINTER_UP)) return;
	int const i = btn->GetUserData();
	if (i < 0) Close(); else Run(i);
}

} // namespace


bool VitaActionMenuIsOpen() { return g_open; }

bool VitaActionMenuOpen()
{
	SOLDIERTYPE* const sel = GetSelectedMan();
	if (g_open || !sel || gpItemPointer) return false;
	switch (gCurrentUIMode)
	{
		case IDLE_MODE: case ACTION_MODE: case HANDCURSOR_MODE: case LOOKCURSOR_MODE: case TALKCURSOR_MODE: case MOVE_MODE: break;
		default: return false;
	}

	g_target = gUIFullTarget;
	Fill(sel);

	// 3x3 grid centred on the cursor, kept on screen
	int const w = 96, h = 20, gap = 4;
	int const x0 = std::clamp(gusMouseXPos - (3 * w + 2 * gap) / 2, 0, SCREEN_WIDTH  - (3 * w + 2 * gap));
	int const y0 = std::clamp(gusMouseYPos - (3 * h + 2 * gap) / 2, 0, SCREEN_HEIGHT - (3 * h + 2 * gap));
	g_x0 = x0; g_y0 = y0; g_w = 3 * w + 2 * gap; g_h = 3 * h + 2 * gap;
	auto make = [&](char const* label, int dx, int dy, int user) {
		GUIButtonRef const b = CreateTextButton(label, FONT12ARIAL, FONT_MCOLOR_WHITE, FONT_MCOLOR_BLACK,
			x0 + (dx + 1) * (w + gap), y0 + (dy + 1) * (h + gap), w, h, MSYS_PRIORITY_HIGHEST, EntryCallback);
		b->SetUserData(user);
		return b;
	};
	for (int i = 0; i < 8; ++i) {
		Action const& a = g_slots[i];
		if (!a.label) continue;
		g_buttons[i] = make(a.label, DIR_X[i], DIR_Y[i], i);
		if (!a.enabled) DisableButton(g_buttons[i]);
	}
	g_cancel = make("Cancel", 0, 0, -1);
	MSYS_DefineRegion(&g_overlay, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, MSYS_PRIORITY_HIGHEST - 1, CURSOR_NORMAL, MSYS_NO_CALLBACK, MSYS_NO_CALLBACK);
	g_open = true;
	g_hx = g_hy = 0;
	Highlight();
	return true;
}

void VitaActionMenuMove(int dx, int dy)
{
	if (!g_open) return;
	g_hx = std::clamp(g_hx + dx, -1, 1);
	g_hy = std::clamp(g_hy + dy, -1, 1);
	Highlight();
}

void VitaActionMenuPoint(int dx, int dy)
{
	if (!g_open) return;
	g_hx = dx; g_hy = dy;
	Highlight();
}

void VitaActionMenuConfirm() { if (g_open) Run(SlotOf(g_hx, g_hy)); }
void VitaActionMenuCancel()  { Close(); }

void VitaActionMenuTick()
{
	if (g_open && guiCurrentScreen != GAME_SCREEN) Close();
	if (g_open) { // the world (moving mercs...) redraws under it every frame: keep it on top, as RenderMovementMenu does
		for (auto& b : g_buttons) if (b) MarkAButtonDirty(b);
		if (g_cancel) MarkAButtonDirty(g_cancel);
		InvalidateRegion(g_x0, g_y0, g_x0 + g_w, g_y0 + g_h);
	}
	if (g_door_slot >= 0) {
		if (VitaDoorMenuIsOpen()) { VitaDoorMenuActivate(g_door_slot); g_door_slot = -1; }
		else if (--g_door_wait <= 0) g_door_slot = -1; // never got there (blocked, cancelled...)
	}
}
