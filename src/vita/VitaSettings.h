#pragma once
// Vita-only input settings, edited on the "Vita" page of the options screen.
#include "ScreenIDs.h"

struct VitaSettings
{
	enum { CONFIRM_SYSTEM, CONFIRM_CROSS, CONFIRM_CIRCLE };
	enum { REAR_OFF, REAR_RELATIVE, REAR_ABSOLUTE };
	int confirm = CONFIRM_SYSTEM; // which button is left click
	int rear    = REAR_RELATIVE;
	int stick_speed = 1; // 0..3: slow, normal, fast, very fast
	int rear_speed  = 1; // 0..3, relative mode only
};

extern VitaSettings gVitaSettings;

void LoadVitaSettings();
ScreenID VitaSettingsScreenHandle();
