// "Vita" page of the options screen: input settings, saved to $HOME/.ja2/vita.ini.
// Every setting is a button that cycles through its values on click.
#include "VitaSettings.h"
#include "Button_System.h"
#include "Directories.h"
#include "Font.h"
#include "Font_Control.h"
#include "GameRes.h"
#include "Game_Clock.h"
#include "Input.h"
#include "Logger.h"
#include "Options_Screen.h"
#include "Render_Dirty.h"
#include "UILayout.h"
#include "Video.h"
#include "VObject.h"
#include "VSurface.h"
#include "WordWrap.h"

#include <string_theory/format>
#include <string_theory/string>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

VitaSettings gVitaSettings;

static ST::string IniPath()
{
	char const* home = getenv("HOME");
	return ST::format("{}/.ja2/vita.ini", home ? home : ".");
}

void LoadVitaSettings()
{
	FILE* f = fopen(IniPath().c_str(), "r");
	if (!f) return;
	char key[32];
	int v;
	while (fscanf(f, " %31[^=]=%d", key, &v) == 2) {
		ST::string const k(key);
		if      (k == "confirm")     gVitaSettings.confirm     = std::clamp(v, 0, 2);
		else if (k == "rear")        gVitaSettings.rear        = std::clamp(v, 0, 2);
		else if (k == "stick_speed") gVitaSettings.stick_speed = std::clamp(v, 0, 3);
		else if (k == "rear_speed")  gVitaSettings.rear_speed  = std::clamp(v, 0, 3);
	}
	fclose(f);
}

static void SaveVitaSettings()
{
	FILE* f = fopen(IniPath().c_str(), "w");
	if (!f) { SLOGW("Vita: cannot write {}", IniPath()); return; }
	fprintf(f, "confirm=%d\nrear=%d\nstick_speed=%d\nrear_speed=%d\n",
		gVitaSettings.confirm, gVitaSettings.rear, gVitaSettings.stick_speed, gVitaSettings.rear_speed);
	fclose(f);
}


// ---- the page ----

static char const* const SPEEDS[]  = { "Slow", "Normal", "Fast", "Very fast" };
static char const* const CONFIRM[] = { "System default", "Cross", "Circle" };
static char const* const REAR[]    = { "Off", "Touchpad (drag)", "Direct (absolute)" };

struct Row
{
	char const* label;
	char const* const* values;
	int count;
	int* value; // points into gVitaSettings
};

static Row const ROWS[] = {
	{ "Left click button",   CONFIRM, 3, &gVitaSettings.confirm },
	{ "Stick cursor speed",  SPEEDS, 4, &gVitaSettings.stick_speed },
	{ "Rear touchpad",       REAR, 3, &gVitaSettings.rear },
	{ "Rear touchpad speed", SPEEDS, 4, &gVitaSettings.rear_speed },
};
#define NUM_ROWS (sizeof(ROWS) / sizeof(*ROWS))

#define ROW_LABEL_X (265 + STD_SCREEN_X)
#define ROW_BTN_X   (430 + STD_SCREEN_X)
#define ROW_BTN_W   160
#define ROW_BTN_H   22
#define ROW_Y       (100 + STD_SCREEN_Y)
#define ROW_GAP     45
#define DONE_BTN_X  (469 + STD_SCREEN_X)
#define DONE_BTN_Y  (438 + STD_SCREEN_Y)

static bool         s_entry = true;
static ScreenID     s_next  = VITA_SETTINGS_SCREEN;
static SGPVObject*  s_background;
static SGPVObject*  s_addons;
static BUTTON_PICS* s_done_pics;
static GUIButtonRef s_done;
static GUIButtonRef s_rows[NUM_ROWS];

static void Leave() { s_next = OPTIONS_SCREEN; }

static void RowCallback(GUI_BUTTON* btn, UINT32 reason)
{
	if (!(reason & MSYS_CALLBACK_REASON_POINTER_UP)) return;
	Row const& r = ROWS[btn->GetUserData()];
	*r.value = (*r.value + 1) % r.count;
	btn->SpecifyText(r.values[*r.value]);
}

static void DoneCallback(GUI_BUTTON*, UINT32 reason)
{
	if (reason & MSYS_CALLBACK_REASON_POINTER_UP) Leave();
}

static void Render()
{
	BltVideoObject(FRAME_BUFFER, s_background, 0, STD_SCREEN_X, STD_SCREEN_Y);
	BltVideoObject(FRAME_BUFFER, s_addons, 1, STD_SCREEN_X, STD_SCREEN_Y + 434); // bottom button bar
	DrawTextToScreen("VITA SETTINGS", ROW_LABEL_X, ROW_Y - 40, 0, FONT14ARIAL, FONT_MCOLOR_WHITE, FONT_MCOLOR_BLACK, LEFT_JUSTIFIED);
	for (UINT32 i = 0; i < NUM_ROWS; ++i)
		DrawTextToScreen(ROWS[i].label, ROW_LABEL_X, ROW_Y + i * ROW_GAP + 5, 0, FONT12ARIAL, OPT_BUTTON_ON_COLOR, FONT_MCOLOR_BLACK, LEFT_JUSTIFIED);
	InvalidateScreen();
}

static void Enter()
{
	PauseGame();
	s_next = VITA_SETTINGS_SCREEN;
	s_background = AddVideoObjectFromFile(INTERFACEDIR "/optionscreenbase.sti");
	s_addons     = AddVideoObjectFromFile(MLG_OPTIONHEADER);
	s_done_pics  = LoadButtonImage(INTERFACEDIR "/optionscreenaddons.sti", 2, 3);
	s_done = CreateIconAndTextButton(s_done_pics, "Done", OPT_BUTTON_FONT, OPT_BUTTON_ON_COLOR, DEFAULT_SHADOW,
		OPT_BUTTON_OFF_COLOR, DEFAULT_SHADOW, DONE_BTN_X, DONE_BTN_Y, MSYS_PRIORITY_HIGH, DoneCallback);
	for (UINT32 i = 0; i < NUM_ROWS; ++i) {
		Row const& r = ROWS[i];
		s_rows[i] = CreateTextButton(r.values[*r.value], FONT12ARIAL, FONT_MCOLOR_WHITE, FONT_MCOLOR_BLACK,
			ROW_BTN_X, ROW_Y + i * ROW_GAP, ROW_BTN_W, ROW_BTN_H, MSYS_PRIORITY_HIGH, RowCallback);
		s_rows[i]->SetUserData(i);
	}
	Render();
}

static void Exit()
{
	SaveVitaSettings();
	for (auto& b : s_rows) RemoveButton(b);
	RemoveButton(s_done);
	UnloadButtonImage(s_done_pics);
	DeleteVideoObject(s_background);
	DeleteVideoObject(s_addons);
	UnPauseGame();
}

ScreenID VitaSettingsScreenHandle()
{
	if (s_entry) {
		Enter();
		s_entry = false;
	}

	RestoreBackgroundRects();

	InputAtom e;
	while (DequeueSpecificEvent(&e, KEYBOARD_EVENTS))
		if (e.usEvent == KEY_DOWN && (e.usParam == SDLK_ESCAPE || e.usParam == SDLK_RETURN)) Leave();

	MarkButtonsDirty();
	RenderButtons();
	SaveBackgroundRects();
	RenderFastHelp();

	if (s_next != VITA_SETTINGS_SCREEN) {
		Exit();
		s_entry = true;
	}
	return s_next;
}
