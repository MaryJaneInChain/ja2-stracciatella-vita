#pragma once
// Tile-based cursor for gamepad play in the tactical view (see VitaTactical.cc).

// If (x,y) is over the map, move it to the centre of the tile under it. Returns false (unchanged) otherwise.
bool VitaSnapToTile(int& x, int& y);

// Move the cursor one tile in screen direction sx/sy (-1, 0, 1); scrolls the map near the view edge.
void VitaStepTile(int sx, int sy);

// Context action menu (VitaActionMenu.cc): triangle opens a 3x3 grid (8 actions, Cancel in the middle).
bool VitaActionMenuOpen();               // false if not possible right now
bool VitaActionMenuIsOpen();
void VitaActionMenuMove(int dx, int dy);  // D-pad: step the highlight within the grid
void VitaActionMenuPoint(int dx, int dy); // stick: highlight the cell in that direction (-1..1 each)
void VitaActionMenuConfirm();            // run the highlighted entry (Cancel = close)
void VitaActionMenuCancel();
void VitaActionMenuTick();               // once per frame: pending door action etc.

// touch shortcut buttons beside the bottom panel (VitaTactical.cc), created/removed with the panel
void VitaCreateSideButtons();
void VitaRemoveSideButtons();
void VitaSideButtonsTick(); // once per frame: runs a clicked button's action

// door-menu entries the wheel can pre-pick (Interface.cc)
enum { VITA_DOOR_OPEN, VITA_DOOR_EXAMINE, VITA_DOOR_LOCKPICK, VITA_DOOR_BOOT,
       VITA_DOOR_KEY, VITA_DOOR_UNTRAP, VITA_DOOR_CROWBAR, VITA_DOOR_EXPLOSIVE };
bool VitaDoorMenuIsOpen();
bool VitaDoorMenuActivate(int slot);

// synthetic input (Input.cc)
#include <SDL3/SDL_keycode.h>
void VitaQueueClick();                         // left click at the cursor a few frames from now
void VitaTapKey(SDL_Keycode key, SDL_Keymod mod); // key down+up, as from a keyboard

// Select the previous (dir < 0) or next merc in team panel order and centre the view on him.
void VitaSelectAdjacentMerc(int dir);
