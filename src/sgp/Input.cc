#include "Types.h"
#include "Input.h"
#include "English.h"
#include "Timer.h"
#include "Video.h"
#include "UILayout.h"

#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>
#include <string_theory/string>

#include <bitset>

#define BUTTON_REPEAT_TIMEOUT		250
#define BUTTON_REPEAT_TIME			50
#define TOUCH_REPEAT_TIMEOUT		750
#define TOUCH_REPEAT_TIME 			100

// The gfKeyState table is used to track which of the keys is up or down at any one time. This is used while polling
// the interface.  true = pressed, false = not pressed.
static std::bitset<2 * SDL_SCANCODE_COUNT> gfKeyState;
static BOOLEAN fCursorWasClipped = FALSE;
static SGPRect gCursorClipRect;

static void QueuePointerEvent(UINT16 eventType, UINT32 param);

// Bitset to keep track of current mouse buttons and touch finger
static std::bitset<6> ButtonStates = 0;

// Struct to keep track of a single button or touch state and emit its events
// The first template parameter indexes into button state
template<
	size_t StateIndex,
	UINT32 BUTTON,
	UINT16 DOWN_EVENT = MOUSE_BUTTON_DOWN,
	UINT16 UP_EVENT = MOUSE_BUTTON_UP,
	UINT16 REPEAT_EVENT = MOUSE_BUTTON_REPEAT,
	UINT32 REPEAT_TIMEOUT = BUTTON_REPEAT_TIMEOUT,
	UINT32 REPEAT_TIME = BUTTON_REPEAT_TIME
>
struct ButtonState {
	private:
		UINT32 repeatTimer;

	public:
		void handleDown() {
			repeatTimer = GetClock() + REPEAT_TIMEOUT;
			ButtonStates[StateIndex] = TRUE;
			QueuePointerEvent(DOWN_EVENT, BUTTON);
		}

		void handleUp() {
			repeatTimer = 0;
			ButtonStates[StateIndex] = FALSE;
			QueuePointerEvent(UP_EVENT, BUTTON);
		}

		void detectRepeat(UINT32 uiTimer) {
			if (isDown()) {
				if (repeatTimer > 0 && repeatTimer <= uiTimer)
				{
					QueuePointerEvent(REPEAT_EVENT, BUTTON);
					repeatTimer = uiTimer + REPEAT_TIME;
				}
			} else {
				repeatTimer = 0;
			}
		}

		BOOLEAN isDown() const {
			return ButtonStates[StateIndex];
		}
};

static ButtonState<0, MOUSE_BUTTON_LEFT> gLeftButtonState;
static ButtonState<1, MOUSE_BUTTON_RIGHT> gRightButtonState;
static ButtonState<2, MOUSE_BUTTON_MIDDLE> gMiddleButtonState;
static ButtonState<3, MOUSE_BUTTON_X1> gX1ButtonState;
static ButtonState<4, MOUSE_BUTTON_X2> gX2ButtonState;
static ButtonState<5, 0, TOUCH_FINGER_DOWN, TOUCH_FINGER_UP, TOUCH_FINGER_REPEAT, TOUCH_REPEAT_TIMEOUT, TOUCH_REPEAT_TIME> gMainFingerState;

UINT16  gusMouseXPos;       // X position of the mouse on screen
UINT16  gusMouseYPos;       // y position of the mouse on screen

BOOLEAN gfIsUsingTouch;
SDL_FingerID gMainFingerId;

// The queue structures are used to track input events using queued events

static InputAtom gEventQueue[256];
static UINT16    gusQueueCount;
static UINT16    gusHeadIndex;
static UINT16    gusTailIndex;


static void QueuePointerEvent(UINT16 eventType, UINT32 param)
{
	// Can we queue up one more event, if not, the event is lost forever
	if (gusQueueCount == lengthof(gEventQueue)) return;

	gEventQueue[gusTailIndex].usEvent = eventType;
	gEventQueue[gusTailIndex].usParam = param;

	gusQueueCount++;

	gusTailIndex = (gusTailIndex + 1) % lengthof(gEventQueue);
}


static void QueueKeyEvent(UINT16 ubInputEvent, SDL_Keycode Key, SDL_Keymod Mod, ST::utf32_buffer codepoints)
{
	// Can we queue up one more event, if not, the event is lost forever
	if (gusQueueCount == lengthof(gEventQueue)) return;

	UINT16 ModifierState = 0;
	if (Mod & SDL_KMOD_SHIFT) ModifierState |= SHIFT_DOWN;
	if (Mod & SDL_KMOD_CTRL)  ModifierState |= CTRL_DOWN;
	if (Mod & SDL_KMOD_ALT)   ModifierState |= ALT_DOWN;
	gEventQueue[gusTailIndex].usKeyState = ModifierState;
	gEventQueue[gusTailIndex].usEvent = ubInputEvent;
	gEventQueue[gusTailIndex].usParam = Key;
	gEventQueue[gusTailIndex].codepoints = std::move(codepoints);

	gusQueueCount++;

	gusTailIndex = (gusTailIndex + 1) % lengthof(gEventQueue);
}

void SetSafeMousePosition(int x, int y) {
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x > SCREEN_WIDTH) x = SCREEN_WIDTH;
	if (y > SCREEN_HEIGHT) y = SCREEN_HEIGHT;

	gusMouseXPos = x;
	gusMouseYPos = y;
}

void HandleSingleClicksAndButtonRepeats();

BOOLEAN DequeueSpecificEvent(InputAtom* Event, UINT32 uiMaskFlags)
{
	// Is there an event to dequeue
	if (gusQueueCount > 0)
	{
		// Check if it has the masks!
		if (gEventQueue[gusHeadIndex].usEvent & uiMaskFlags)
		{
			return DequeueEvent(Event);
		}
	}

	return FALSE;
}

BOOLEAN DequeueEvent(InputAtom* Event)
{
	if (gusQueueCount == 0) return FALSE;

	*Event = gEventQueue[gusHeadIndex];
	gusHeadIndex = (gusHeadIndex + 1) % lengthof(gEventQueue);
	gusQueueCount--;
	return TRUE;
}

#if defined(WITH_MAEMO) || defined __APPLE__
static BOOLEAN g_down_right;
#endif

// vita: front touch is a plain mouse (SDL's synthesized mouse events), not upstream's phone touch mode,
// which hides pointer cursors and uses two-tap actions
#ifdef __vita__
#define IGNORE_TOUCH_MOUSE(ev) (void)0
#define IGNORE_FINGER() return
#else
#define IGNORE_TOUCH_MOUSE(ev) if ((ev)->which == SDL_TOUCH_MOUSEID) return
#define IGNORE_FINGER() (void)0
#endif

void MouseMove(const SDL_MouseMotionEvent* event) {
	IGNORE_TOUCH_MOUSE(event);
	gfIsUsingTouch = FALSE;

	SetSafeMousePosition(event->x, event->y);
}

void MouseButtonDown(const SDL_MouseButtonEvent* BtnEv)
{
	IGNORE_TOUCH_MOUSE(BtnEv);
	gfIsUsingTouch = FALSE;

	SetSafeMousePosition(BtnEv->x, BtnEv->y);
	switch (BtnEv->button)
	{
		case SDL_BUTTON_LEFT:
		{
#if defined WITH_MAEMO
			/* If the menu button (mapped to F4) is pressed, then treat the event as
			 * right click */
			const bool* const key_state = SDL_GetKeyboardState(NULL);
			g_down_right = key_state[SDL_SCANCODE_F4];
			if (g_down_right) goto right_button;
#endif
#if defined(__APPLE__)
			const bool* const key_state = SDL_GetKeyboardState(NULL);
			g_down_right = key_state[SDL_SCANCODE_LGUI] || key_state[SDL_SCANCODE_RGUI];
			if (g_down_right) goto right_button;
#endif
			gLeftButtonState.handleDown();
			break;
		}

		case SDL_BUTTON_RIGHT:
#if defined(WITH_MAEMO) || defined(__APPLE__)
right_button:
#endif
			gRightButtonState.handleDown();
			break;

		case SDL_BUTTON_MIDDLE:
			gMiddleButtonState.handleDown();
			break;

		case SDL_BUTTON_X1:
			gX1ButtonState.handleDown();
			break;

		case SDL_BUTTON_X2:
			gX2ButtonState.handleDown();
			break;
	}
}


void MouseButtonUp(const SDL_MouseButtonEvent* BtnEv)
{
	IGNORE_TOUCH_MOUSE(BtnEv);
	gfIsUsingTouch = false;

	SetSafeMousePosition(BtnEv->x, BtnEv->y);
	switch (BtnEv->button)
	{
		case SDL_BUTTON_LEFT:
		{
#if defined(WITH_MAEMO) || defined(__APPLE__)
			if (g_down_right) goto right_button;
#endif
			gLeftButtonState.handleUp();
			break;
		}

		case SDL_BUTTON_RIGHT:
#if defined WITH_MAEMO || defined(__APPLE__)
right_button:
#endif
			gRightButtonState.handleUp();
			break;

		case SDL_BUTTON_MIDDLE:
			gMiddleButtonState.handleUp();
			break;

		case SDL_BUTTON_X1:
			gX1ButtonState.handleUp();
			break;

		case SDL_BUTTON_X2:
			gX2ButtonState.handleUp();
			break;
	}
}

void MouseWheelScroll(const SDL_MouseWheelEvent* WheelEv)
{
	gfIsUsingTouch = false;

	if (WheelEv->y > 0)
	{
		QueuePointerEvent(MOUSE_WHEEL_UP, 0);
	}
	else
	{
		QueuePointerEvent(MOUSE_WHEEL_DOWN, 0);
	}
}

void FingerMove(const SDL_TouchFingerEvent* event) {
	IGNORE_FINGER();
	#ifdef SDL_MOUSE_TOUCHID
	if (event->touchID == SDL_MOUSE_TOUCHID) return;
	#endif
	if (event->fingerID != gMainFingerId) return;
	gfIsUsingTouch = true;

	SetSafeMousePosition((int)event->x, (int)event->y);

	QueuePointerEvent(TOUCH_FINGER_MOVE, 0);
}

void FingerDown(const SDL_TouchFingerEvent* event) {
	IGNORE_FINGER();
	#ifdef SDL_MOUSE_TOUCHID
	if (event->touchID == SDL_MOUSE_TOUCHID) return;
	#endif

	gMainFingerId = event->fingerID;
	gfIsUsingTouch = true;

	SetSafeMousePosition((int)event->x, (int)event->y);

	gMainFingerState.handleDown();
}

void FingerUp(const SDL_TouchFingerEvent* event) {
	IGNORE_FINGER();
	#ifdef SDL_MOUSE_TOUCHID
	if (event->touchID == SDL_MOUSE_TOUCHID) return;
	#endif
	if (event->fingerID != gMainFingerId) return;
	gfIsUsingTouch = true;

	SetSafeMousePosition((int)event->x, (int)event->y);

	gMainFingerState.handleUp();
}


// Remap SDL keycodes with bit 30 set to the range 512..1023
// Necessary to be able to use the keycode as an index for the gfKeyState bitset.
static SDL_Keycode RemapKeycode(SDL_Keycode const key)
{
	return (key & SDLK_SCANCODE_MASK)
		? (key & ~SDLK_SCANCODE_MASK) + SDL_SCANCODE_COUNT
		: key;
}


bool _KeyDown(SDL_Keycode const keycode)
{
	return gfKeyState[RemapKeycode(keycode)];
}


static void KeyChange(SDL_KeyboardEvent const* const event, bool const pressed)
{
	SDL_Keycode      key = event->key;
	SDL_Keymod const mod = event->mod;
	bool   const num = mod & SDL_KMOD_NUM;

	// Special handling for some keys which often are used for different
	// purposes for many keyboard layouts compared to the US keyboard layout
	// and which otherwise wouldn't be usable as hotkeys in tactical mode.
	//
	// Note: it is still possible to use the key as intended by its
	// keyboard layout when entering text like a save game description or
	// the IMP name because we are using SDL_TextInput events for those,
	// which are not affected by the KeyChange function. (Issue #1844)
	// Checked if neither alt, altgr, control, shift or the 'OS' key is
	// pressed. Num lock, caps lock and scroll lock can be active.
	if ((mod & (SDL_KMOD_ALT | SDL_KMOD_CTRL | SDL_KMOD_GUI | SDL_KMOD_MODE | SDL_KMOD_SHIFT)) == SDL_KMOD_NONE)
	{
		switch (event->scancode)
		{
			case SDL_SCANCODE_EQUALS:      key = SDLK_EQUALS;           break;
			case SDL_SCANCODE_SLASH:       key = SDLK_SLASH;            break;
			case SDL_SCANCODE_GRAVE:       key = SDLK_GRAVE;        break;
			default:                                                    break;
		}
	}

	switch (key)
	{
#if defined WITH_MAEMO
		/* Use the menu button (mapped to F4) as modifier for right click */
		case SDLK_F4: return;
#endif

		case SDLK_KP_0:         key = num ? SDLK_0      : SDLK_INSERT;   break;
		case SDLK_KP_1:         key = num ? SDLK_1      : SDLK_END;      break;
		case SDLK_KP_2:         key = num ? SDLK_2      : SDLK_DOWN;     break;
		case SDLK_KP_3:         key = num ? SDLK_3      : SDLK_PAGEDOWN; break;
		case SDLK_KP_4:         key = num ? SDLK_4      : SDLK_LEFT;     break;
		case SDLK_KP_5:
			if (!num) return;
			key = SDLK_5;
			break;
		case SDLK_KP_6:         key = num ? SDLK_6      : SDLK_RIGHT;    break;
		case SDLK_KP_7:         key = num ? SDLK_7      : SDLK_HOME;     break;
		case SDLK_KP_8:         key = num ? SDLK_8      : SDLK_UP;       break;
		case SDLK_KP_9:         key = num ? SDLK_9      : SDLK_PAGEUP;   break;
		case SDLK_KP_PERIOD:   key = num ? SDLK_PERIOD : SDLK_DELETE;   break;
		case SDLK_KP_DIVIDE:   key = SDLK_SLASH;                        break;
		case SDLK_KP_MULTIPLY: key = SDLK_ASTERISK;                     break;
		case SDLK_KP_MINUS:    key = SDLK_MINUS;                        break;
		case SDLK_KP_PLUS:     key = SDLK_PLUS;                         break;
		case SDLK_KP_ENTER:    key = SDLK_RETURN;                       break;

		default:
			if ((key & ~SDLK_SCANCODE_MASK) >= SDL_SCANCODE_COUNT) return;
			break;
	}

	UINT     event_type;
	bool key_state = _KeyDown(key);
	if (pressed)
	{
		event_type = key_state ? KEY_REPEAT : KEY_DOWN;
	}
	else
	{
		if (!key_state) return;
		event_type = KEY_UP;
	}
	gfKeyState[RemapKeycode(key)] = pressed;

	QueueKeyEvent(event_type, key, mod, {});
}


void KeyDown(const SDL_KeyboardEvent* event)
{
	switch (event->key)
	{
		case SDLK_LSHIFT:
		case SDLK_RSHIFT:
			gfKeyState.set(SHIFT);
			break;

		case SDLK_LCTRL:
		case SDLK_RCTRL:
			gfKeyState.set(CTRL);
			break;

		case SDLK_LALT:
		case SDLK_RALT:
		case SDLK_MODE:
			gfKeyState.set(ALT);
			break;

		case SDLK_PRINTSCREEN:
		case SDLK_SCROLLLOCK:
			break;

		default:
			KeyChange(event, true);
			break;
	}
}


void KeyUp(const SDL_KeyboardEvent* event)
{
	switch (event->key)
	{
		case SDLK_LSHIFT:
		case SDLK_RSHIFT:
			gfKeyState.reset(SHIFT);
			break;

		case SDLK_LCTRL:
		case SDLK_RCTRL:
			gfKeyState.reset(CTRL);
			break;

		case SDLK_LALT:
		case SDLK_RALT:
		case SDLK_MODE:
			gfKeyState.reset(ALT);
			break;

		case SDLK_SCROLLLOCK:
			SDL_SetWindowMouseGrab
			(
				GAME_WINDOW,
				!SDL_GetWindowMouseGrab(GAME_WINDOW)
			);
			break;

		case SDLK_RETURN:
			if (_KeyDown(ALT))
			{
				VideoToggleFullScreen();
				break;
			}
			/* FALLTHROUGH */

		default:
			KeyChange(event, false);
			break;
	}
}

void TextInput(const SDL_TextInputEvent* TextEv) {
	try {
		QueueKeyEvent(TEXT_INPUT, SDLK_UNKNOWN, SDL_KMOD_NONE, ST::string(TextEv->text).to_utf32());
	}
	catch (const ST::unicode_error&)
	{
		// ignore invalid inputs
		static bool warn = true;
		if (warn)
		{
			SLOGW("Received invalid utf-8 character.");
			warn = false;
		}
	}
}


SGPPoint GetMousePos()
{
	return { gusMouseXPos, gusMouseYPos };
}

bool IsMouseButtonDown(UINT32 mouseButton) {
	switch (mouseButton) {
		case MOUSE_BUTTON_LEFT:
			return gLeftButtonState.isDown();
		case MOUSE_BUTTON_RIGHT:
			return gRightButtonState.isDown();
		case MOUSE_BUTTON_MIDDLE:
			return gMiddleButtonState.isDown();
		case MOUSE_BUTTON_X1:
			return gX1ButtonState.isDown();
		case MOUSE_BUTTON_X2:
			return gX1ButtonState.isDown();
		default:
			SLOGE("Unknown button checked in IsMouseButtonDown: {}", mouseButton);
			return false;
	}
}

bool IsMainFingerDown() {
	return gMainFingerState.isDown();
}

bool IsUsingTouch() {
	return gfIsUsingTouch;
}

void RestrictMouseToXYXY(UINT16 usX1, UINT16 usY1, UINT16 usX2, UINT16 usY2)
{
	SGPRect TempRect;
	TempRect.iLeft   = usX1;
	TempRect.iTop    = usY1;
	TempRect.iRight  = usX2;
	TempRect.iBottom = usY2;
	RestrictMouseCursor(&TempRect);
}


void RestrictMouseCursor(const SGPRect* pRectangle)
{
	// Make a copy of our rect....
	gCursorClipRect = *pRectangle;
#if 1 // XXX TODO0000 Should probably removed completly. Confining the mouse cursor is The Wrong Thing(tm)
#else
	ClipCursor((RECT*)pRectangle);
#endif
	fCursorWasClipped = TRUE;
}


void FreeMouseCursor(void)
{
#if 1 // XXX TODO0000
#else
	ClipCursor(NULL);
#endif
	fCursorWasClipped = FALSE;
}

void GetRestrictedClipCursor(SGPRect* pRectangle)
{
#if 1 // XXX TODO0000
	pRectangle->iLeft   = 0;
	pRectangle->iTop    = 0;
	pRectangle->iRight  = SCREEN_WIDTH;
	pRectangle->iBottom = SCREEN_HEIGHT;
#else
	GetClipCursor((RECT *) pRectangle );
#endif
}


BOOLEAN IsCursorRestricted(void)
{
	return fCursorWasClipped;
}


void SimulateMouseMovement( UINT32 uiNewXPos, UINT32 uiNewYPos )
{
	// Never move mouse on touch (it's useless and will cause a mouse event which we dont want on touch devices)
	if (gfIsUsingTouch) {
		return;
	}

	float wx, wy;
	if (SDL_RenderCoordinatesToWindow(GameRenderer, (float)uiNewXPos, (float)uiNewYPos, &wx, &wy)) {
		SDL_WarpMouseInWindow(GAME_WINDOW, (int)wx, (int)wy);
	} else {
		// Fallback: use raw coordinates if conversion fails
		SDL_WarpMouseInWindow(GAME_WINDOW, uiNewXPos, uiNewYPos);
	}
}


void DequeueAllInputEvents(void)
{
	InputAtom InputEvent;
	while (DequeueEvent(&InputEvent))
	{
		//dont do anything
	}
}


void HandleSingleClicksAndButtonRepeats()
{
	UINT32 uiTimer = GetClock();

	// Is there a touch finger to repeat
	gMainFingerState.detectRepeat(uiTimer);
	gLeftButtonState.detectRepeat(uiTimer);
	gRightButtonState.detectRepeat(uiTimer);
	gMiddleButtonState.detectRepeat(uiTimer);
	gX1ButtonState.detectRepeat(uiTimer);
	gX2ButtonState.detectRepeat(uiTimer);
}


#ifdef __vita__
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_timer.h>
#include <psp2/apputil.h>
#include <psp2/system_param.h>
#include <psp2/touch.h>
#include "VitaSettings.h"
#include "VitaTactical.h"
#include "ScreenIDs.h"
extern INT16 gsScrollXOffset, gsScrollYOffset; // RenderWorld.cc: pan-mode scroll offsets
extern ScreenID guiCurrentScreen;
void ToggleTacticalPanelHidden(); // Interface.cc
#include <cmath>
#include <algorithm>
#include "Logger.h"

// Rear touchpad = laptop-style touchpad: moves the cursor only, never clicks (clicks are on the buttons).
// Only the first finger that lands outside the side grip zones is tracked.
// Absolute mode maps the area between the grip zones onto the whole screen.
// ponytail: fixed 15% grip zones, no two-finger scroll yet
static void VitaPollRearTouch()
{
	if (gVitaSettings.rear == VitaSettings::REAR_OFF) return;

	static bool inited;
	static SceTouchPanelInfo info;
	static int finger_id = -1, last_x, last_y;
	static float rem_x, rem_y;
	if (!inited) {
		inited = true;
		sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);
		sceTouchGetPanelInfo(SCE_TOUCH_PORT_BACK, &info);
		SLOGI("Vita rear touch area {}-{} x {}-{}", info.minAaX, info.maxAaX, info.minAaY, info.maxAaY);
	}

	SceTouchData td;
	if (sceTouchPeek(SCE_TOUCH_PORT_BACK, &td, 1) < 1) return;

	SceTouchReport const* cur = nullptr;
	for (unsigned i = 0; i < td.reportNum; ++i)
		if (td.report[i].id == finger_id) cur = &td.report[i];

	int const grip = (info.maxAaX - info.minAaX) * 15 / 100;
	if (cur && gVitaSettings.rear == VitaSettings::REAR_ABSOLUTE) {
		int const x0 = info.minAaX + grip, x1 = info.maxAaX - grip;
		int const x = std::clamp((cur->x - x0) * SCREEN_WIDTH / (x1 - x0), 0, SCREEN_WIDTH - 1);
		int const y = std::clamp((cur->y - info.minAaY) * SCREEN_HEIGHT / (info.maxAaY - info.minAaY), 0, SCREEN_HEIGHT - 1);
		SetSafeMousePosition(x, y);
		return;
	}
	if (!cur) { // lost (or never had) our finger: pick a new one outside the grip zones
		finger_id = -1;
		for (unsigned i = 0; i < td.reportNum; ++i) {
			int const x = td.report[i].x;
			if (x > info.minAaX + grip && x < info.maxAaX - grip) {
				finger_id = td.report[i].id;
				last_x = x; last_y = td.report[i].y;
				rem_x = rem_y = 0;
				break;
			}
		}
		return;
	}

	int const dx = cur->x - last_x, dy = cur->y - last_y;
	last_x = cur->x; last_y = cur->y;
	if (dx == 0 && dy == 0) return;

	// panel is ~1920 units wide vs 640 game pixels; mild acceleration for fast swipes
	float const dist = std::sqrt(float(dx * dx + dy * dy));
	static float const gains[] = { 0.2f, 0.35f, 0.5f, 0.7f };
	float const gain = gains[gVitaSettings.rear_speed] * (1.0f + std::min(dist / 40.0f, 2.0f));
	rem_x += dx * gain; rem_y += dy * gain;
	int const mx = (int)rem_x, my = (int)rem_y;
	rem_x -= mx; rem_y -= my;
	if (mx || my) SetSafeMousePosition(std::clamp(gusMouseXPos + mx, 0, SCREEN_WIDTH - 1),
	                                   std::clamp(gusMouseYPos + my, 0, SCREEN_HEIGHT - 1));
}

static int s_click_frames; // pending synthetic left click (see VitaQueueClick)

void VitaQueueClick() { s_click_frames = 5; } // let the UI switch cursor mode first, then click down/up

void VitaTapKey(SDL_Keycode const key, SDL_Keymod const mod)
{
	SDL_KeyboardEvent ev{};
	ev.key = key;
	ev.scancode = SDL_GetScancodeFromKey(key, nullptr);
	ev.mod = mod;
	ev.type = SDL_EVENT_KEY_DOWN; ev.down = true;  KeyDown(&ev);
	ev.type = SDL_EVENT_KEY_UP;   ev.down = false; KeyUp(&ev);
}

// Vita buttons -> mouse/keys. Called once per game cycle from MainLoop.
// ponytail: fixed mapping, no per-screen hotkey layers yet (see HOTKEYS.md)
void VitaPollGamepad()
{
	static SDL_Gamepad* pad;
	static bool inited, enter_is_circle, prev[SDL_GAMEPAD_BUTTON_COUNT];
	static float cx, cy;
	static Uint64 last;

	if (!inited) {
		inited = true;
		LoadVitaSettings();
		SceAppUtilInitParam ip{}; SceAppUtilBootParam bp{};
		sceAppUtilInit(&ip, &bp); // may already be initialised by SDL; result ignored
		int v = SCE_SYSTEM_PARAM_ENTER_BUTTON_CROSS;
		sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, &v);
		enter_is_circle = v == SCE_SYSTEM_PARAM_ENTER_BUTTON_CIRCLE;
		int n = 0;
		SDL_JoystickID* ids = SDL_GetGamepads(&n);
		if (n > 0) pad = SDL_OpenGamepad(ids[0]);
		SDL_free(ids);
		if (!pad) SLOGW("Vita: no gamepad: {}", SDL_GetError());
		last = SDL_GetTicks();
	}
	Uint64 const now = SDL_GetTicks();
	float const dt = (now - last) / 1000.0f;
	last = now;

	VitaPollRearTouch();
	if (!pad) return;

	// confirm button (system setting, or forced on the Vita settings page) = left click, the other = right click
	bool const circle_left = gVitaSettings.confirm == VitaSettings::CONFIRM_SYSTEM ? enter_is_circle
	                       : gVitaSettings.confirm == VitaSettings::CONFIRM_CIRCLE;
	SDL_GamepadButton const left  = circle_left ? SDL_GAMEPAD_BUTTON_EAST  : SDL_GAMEPAD_BUTTON_SOUTH;
	SDL_GamepadButton const right = circle_left ? SDL_GAMEPAD_BUTTON_SOUTH : SDL_GAMEPAD_BUTTON_EAST;

	// presses taken by the action menu: their releases must not reach the game (a lone button-up is a click)
	static bool eaten[SDL_GAMEPAD_BUTTON_COUNT];
	auto edge = [&](SDL_GamepadButton b) { // -1 released, 0 unchanged, 1 pressed
		bool const now_down = SDL_GetGamepadButton(pad, b);
		int e = now_down == prev[b] ? 0 : now_down ? 1 : -1;
		prev[b] = now_down;
		if (e < 0 && eaten[b]) { eaten[b] = false; e = 0; }
		return e;
	};

	float const ax = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
	float const ay = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
	float const mag = std::sqrt(ax * ax + ay * ay);

	VitaActionMenuTick();
	VitaSideButtonsTick();
	if (s_click_frames > 0) { // after a cursor mode switch: click down, then up
		--s_click_frames;
		if (s_click_frames == 2) gLeftButtonState.handleDown();
		if (s_click_frames == 0) gLeftButtonState.handleUp();
	}

	// action menu (VitaActionMenu.cc) is up: it takes the D-pad, left stick and face buttons
	if (VitaActionMenuIsOpen()) {
		int e[SDL_GAMEPAD_BUTTON_COUNT];
		for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
			e[b] = edge(SDL_GamepadButton(b));
			if (e[b] > 0) eaten[b] = true;
		}
		if (mag > 0.5f) VitaActionMenuPoint(ax >  0.38f * mag ? 1 : ax < -0.38f * mag ? -1 : 0,
		                                    ay >  0.38f * mag ? 1 : ay < -0.38f * mag ? -1 : 0);
		if (e[SDL_GAMEPAD_BUTTON_DPAD_UP]    > 0) VitaActionMenuMove( 0, -1);
		if (e[SDL_GAMEPAD_BUTTON_DPAD_DOWN]  > 0) VitaActionMenuMove( 0,  1);
		if (e[SDL_GAMEPAD_BUTTON_DPAD_LEFT]  > 0) VitaActionMenuMove(-1,  0);
		if (e[SDL_GAMEPAD_BUTTON_DPAD_RIGHT] > 0) VitaActionMenuMove( 1,  0);
		if (e[left] > 0) VitaActionMenuConfirm();
		else if (e[right] > 0 || e[SDL_GAMEPAD_BUTTON_NORTH] > 0 || e[SDL_GAMEPAD_BUTTON_START] > 0) VitaActionMenuCancel();
		cx = cy = 0;
		return;
	}

	// left stick -> cursor; quadratic curve for precision, full tilt crosses the screen in ~1s
	if (mag > 0.2f) {
		if (cx == 0 && cy == 0) { cx = gusMouseXPos; cy = gusMouseYPos; }
		static float const speeds[] = { 400.0f, 700.0f, 1000.0f, 1400.0f };
		float const speed = speeds[gVitaSettings.stick_speed] * ((mag - 0.2f) / 0.8f) * ((mag - 0.2f) / 0.8f) / mag;
		cx = std::clamp(cx + ax * speed * dt, 0.0f, (float)SCREEN_WIDTH  - 1);
		cy = std::clamp(cy + ay * speed * dt, 0.0f, (float)SCREEN_HEIGHT - 1);
		int px = (int)cx, py = (int)cy;
		if (guiCurrentScreen == GAME_SCREEN) VitaSnapToTile(px, py); // over the map: hop tile to tile
		SetSafeMousePosition(px, py);
	} else {
		cx = cy = 0; // resync with touch/other moves next time
	}

	// right stick -> scroll the tactical map, through the same offsets as middle-button pan mode
	if (guiCurrentScreen == GAME_SCREEN) {
		static float rem_x, rem_y;
		float const rx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.0f;
		float const ry = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.0f;
		float const rmag = std::sqrt(rx * rx + ry * ry);
		if (rmag > 0.2f) {
			float const k = (rmag - 0.2f) / 0.8f;
			float const speed = 900.0f * k * k / rmag; // pixels/s at full tilt
			rem_x -= rx * speed * dt; // pan offsets follow drag direction: negative = view moves right
			rem_y -= ry * speed * dt;
			INT16 const ix = (INT16)rem_x, iy = (INT16)rem_y;
			rem_x -= ix; rem_y -= iy;
			// clamped: at the map edge the world can't scroll and offsets would pile up
			gsScrollXOffset = std::clamp(gsScrollXOffset + ix, -60, 60);
			gsScrollYOffset = std::clamp(gsScrollYOffset + iy, -60, 60);
		} else {
			rem_x = rem_y = 0;
		}
	}

	struct { SDL_GamepadButton b; SDL_Keycode k; } static const keys[] = {
		{ SDL_GAMEPAD_BUTTON_START,      SDLK_ESCAPE },
		{ SDL_GAMEPAD_BUTTON_NORTH,      SDLK_RETURN }, // triangle
		{ SDL_GAMEPAD_BUTTON_WEST,       SDLK_SPACE  }, // square
		{ SDL_GAMEPAD_BUTTON_DPAD_UP,    SDLK_UP     },
		{ SDL_GAMEPAD_BUTTON_DPAD_DOWN,  SDLK_DOWN   },
		{ SDL_GAMEPAD_BUTTON_DPAD_LEFT,  SDLK_LEFT   },
		{ SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDLK_RIGHT  },
	};

	if (int e = edge(left))  { if (e > 0) gLeftButtonState.handleDown();  else gLeftButtonState.handleUp(); }
	if (int e = edge(right)) { if (e > 0) gRightButtonState.handleDown(); else gRightButtonState.handleUp(); }
	// Select: hide/show the tactical bottom panel (world view takes the whole screen)
	if (edge(SDL_GAMEPAD_BUTTON_BACK) > 0 && guiCurrentScreen == GAME_SCREEN) ToggleTacticalPanelHidden();
	// L/R: previous/next merc, centred
	if (edge(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)  > 0 && guiCurrentScreen == GAME_SCREEN) VitaSelectAdjacentMerc(-1);
	if (edge(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) > 0 && guiCurrentScreen == GAME_SCREEN) VitaSelectAdjacentMerc(+1);

	// tactical: triangle opens the action menu; D-pad steps the cursor to the adjacent tile in screen direction
	// (VitaStepTile), repeating while held. Map scrolling is on the right stick there.
	// Everywhere else triangle is Enter and the D-pad the arrow keys.
	static bool sent[SDL_GAMEPAD_BUTTON_COUNT]; // key-down delivered, so the matching key-up is owed
	static Uint64 repeat_at[SDL_GAMEPAD_BUTTON_COUNT];
	for (auto const& m : keys) {
		int const e = edge(m.b);
		if (m.b == SDL_GAMEPAD_BUTTON_NORTH && guiCurrentScreen == GAME_SCREEN && !sent[m.b]) {
			if (e > 0) VitaActionMenuOpen();
			continue;
		}
		bool const dpad = m.b >= SDL_GAMEPAD_BUTTON_DPAD_UP && m.b <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
		if (dpad && guiCurrentScreen == GAME_SCREEN && !sent[m.b]) {
			bool step = false;
			if (e > 0) { step = true; repeat_at[m.b] = now + 350; }
			else if (prev[m.b] && now >= repeat_at[m.b]) { step = true; repeat_at[m.b] = now + 120; }
			if (step) VitaStepTile(m.k == SDLK_LEFT ? -1 : m.k == SDLK_RIGHT ? 1 : 0,
			                       m.k == SDLK_UP   ? -1 : m.k == SDLK_DOWN  ? 1 : 0);
			continue;
		}
		if (e == 0 || (e < 0 && !sent[m.b])) continue;
		SDL_KeyboardEvent ev{};
		ev.type = e > 0 ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
		ev.key = m.k;
		ev.scancode = SDL_GetScancodeFromKey(m.k, nullptr);
		ev.down = e > 0;
		sent[m.b] = e > 0;
		if (e > 0) KeyDown(&ev); else KeyUp(&ev);
	}
}
#endif
