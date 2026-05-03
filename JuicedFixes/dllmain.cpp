#include "IniReader.h"
#include "injector/injector.hpp"

#include "input.h"
#include "xinput.h"

#define JOY_BTN_Y      0
#define JOY_BTN_X      1
#define JOY_BTN_A      2
#define JOY_BTN_B      3
#define JOY_DPAD_DOWN  4
#define JOY_DPAD_UP    5
#define JOY_DPAD_LEFT  6
#define JOY_DPAD_RIGHT 7
#define JOY_BTN_LS     8
#define JOY_BTN_RS     9
#define JOY_BTN_LT     10
#define JOY_BTN_RT     11
#define JOY_BTN_START  12
#define JOY_BTN_BACK   13

#define AXIS_LT       0x01000000
#define AXIS_RT       0x02000000
#define AXIS_LX       0x03000000
#define AXIS_LY       0x04000000
#define AXIS_RX       0x05000000
#define AXIS_RY       0x06000000

#define CHECK_TIMEOUT 1000 // Check controllers every n ms

int pressed[14];

// Patches
bool PatchVirtualMemory = false;
bool PatchCalendarCrash = false;
bool PatchInput = false;

int MenuCodes[16];
int RaceCodes[20];


int JoyBtnToXInputBtn(int joyBtn)
{
	switch (joyBtn)
	{
	case JOY_BTN_A:
		return  XINPUT_GAMEPAD_A;
	case JOY_BTN_B:
		return XINPUT_GAMEPAD_B;
	case JOY_BTN_Y:
		return XINPUT_GAMEPAD_Y;
	case JOY_BTN_X:
		return XINPUT_GAMEPAD_X;
	case JOY_DPAD_DOWN:
		return XINPUT_GAMEPAD_DPAD_DOWN;
	case JOY_DPAD_LEFT:
		return XINPUT_GAMEPAD_DPAD_LEFT;
	case JOY_DPAD_RIGHT:
		return XINPUT_GAMEPAD_DPAD_RIGHT;
	case JOY_DPAD_UP:
		return XINPUT_GAMEPAD_DPAD_UP;
	case JOY_BTN_START:
		return XINPUT_GAMEPAD_START;
	case JOY_BTN_BACK:
		return XINPUT_GAMEPAD_BACK;
	case JOY_BTN_LS:
		return XINPUT_GAMEPAD_LEFT_SHOULDER;
	case JOY_BTN_RS:
		return XINPUT_GAMEPAD_RIGHT_SHOULDER;
	case JOY_BTN_LT:
		return XINPUT_GAMEPAD_LEFT_THUMB;
	case JOY_BTN_RT:
		return XINPUT_GAMEPAD_RIGHT_THUMB;
	}
	return 0;
}


XINPUT_STATE states[4];
bool         connected[4];
int          timeout = 0;

XINPUT_STATE check;

/*
*  Buttons check order
*  Deatils on the exact order are in joystick.h
*/
int checkOrder[] =
{
	XINPUT_GAMEPAD_Y,
	XINPUT_GAMEPAD_X,
	XINPUT_GAMEPAD_A,
	XINPUT_GAMEPAD_B,
	XINPUT_GAMEPAD_DPAD_DOWN,
	XINPUT_GAMEPAD_DPAD_UP,
	XINPUT_GAMEPAD_DPAD_LEFT,
	XINPUT_GAMEPAD_DPAD_RIGHT,
	XINPUT_GAMEPAD_LEFT_SHOULDER,
	XINPUT_GAMEPAD_RIGHT_SHOULDER,
	XINPUT_GAMEPAD_LEFT_THUMB,
	XINPUT_GAMEPAD_RIGHT_THUMB,
	XINPUT_GAMEPAD_START,
	XINPUT_GAMEPAD_BACK
};

float throttle = 0.0f;
float brake = 0.0f;
float steering = 0.0f;
float LY = 0.0f;
float RY = 0.0f;
float RX = 0.0f;

int controlType = 1;

void CheckControllers(void)
{
	XINPUT_STATE state;
	for (int i = 0; i < 4; i++)
	{
		if (connected[i])
			continue;
		if (XInputGetState(i, &state) == ERROR_SUCCESS)
		{
			connected[i] = TRUE;
			states[i] = state;
		}
	}
}

float GetButtonAsAxle(int joyBtn, int joyIdx)
{
	if (joyBtn > 13 || joyBtn < 0)
		return 0.f;
	bool actuallyPressed = states[joyIdx].Gamepad.wButtons & JoyBtnToXInputBtn(joyBtn);
	return actuallyPressed ? 1.0f : 0.0f;
}

int GetButtonAsOrder(int joyBtn, int joyIdx)
{
	if (joyBtn > 13 || joyBtn < 0)
		return 0;
	bool actuallyPressed = !!(states[joyIdx].Gamepad.wButtons & JoyBtnToXInputBtn(joyBtn));

	if (actuallyPressed)
	{
		// In order to work as driver controls, the press must be reported for at least 2 ticks.
		if (pressed[joyBtn] < 2)
		{
			pressed[joyBtn]++;
			return true;
		}
		else {
			return false;
		}
	}
	else {
		pressed[joyBtn] = 0;
	}
	return false;
}

int GetButtonState(int joyBtn, int joyIdx)
{
	if (joyBtn > 13 || joyBtn < 0)
		return 0;
	bool actuallyPressed = !!(states[joyIdx].Gamepad.wButtons & JoyBtnToXInputBtn(joyBtn));
	if (controlType == ControlType::Menu && joyBtn > 3)
		return actuallyPressed;

	if (pressed[joyBtn])
	{
		pressed[joyBtn] = actuallyPressed;
		return false;
	}
	if (actuallyPressed)
	{
		pressed[joyBtn] = true;
		return true;
	}
	pressed[joyBtn] = false;
	return false;
}

float NormDeadzone(float value, float deadzone)
{
	if (fabs(value) < deadzone)
		return 0.0f;
	return (value > 0 ? (value - deadzone) : (value + deadzone)) * (1.0 / (1.0 - deadzone));
}

void RaiseEvents(void)
{
	XINPUT_STATE state;
	for (int i = 0; i < 4; i++)
	{
		if (!connected[i])
			continue;

		if (XInputGetState(i, &state) != ERROR_SUCCESS)
		{
			connected[i] = FALSE;
			continue;
		}

		float deadzone = 0.2;

		throttle = ((float)state.Gamepad.bRightTrigger / 255.0);
		brake = ((float)state.Gamepad.bLeftTrigger / 255.0);

		float normLX = fmaxf(-1, (float)state.Gamepad.sThumbLX / 32767.0);
		float normLY = fmaxf(-1, (float)state.Gamepad.sThumbLY / 32767.0);
		float normRX = fmaxf(-1, (float)state.Gamepad.sThumbRX / 32767.0);
		float normRY = fmaxf(-1, (float)state.Gamepad.sThumbRY / 32767.0);

		steering = NormDeadzone(normLX, deadzone);
		LY = NormDeadzone(normLY, deadzone);
		RX = NormDeadzone(normRX, deadzone);
		RY = NormDeadzone(normRY, deadzone);

		states[i] = state;
	}
}

void joystick_init(void)
{
	CheckControllers();
}

void joystick_cycle(int passed)
{
	timeout += passed;
	if (timeout > CHECK_TIMEOUT)
	{
		timeout -= CHECK_TIMEOUT;
		CheckControllers();
	}
	RaiseEvents();
}

DWORD WINAPI Background(LPVOID unused)
{

	while (true)
	{
		joystick_cycle(1);
		Sleep(1);
	}
}

char rets[]
{
	0xC3,
	0xC3,
	0xC3,
	0xC3,
	0xC3,
	0xC3,
	0xC3,
	0xC3,
	0xC3,
	0xC3,
	0xC3,
	0xC3,
	0xC3,
	0xC3,
	0xC3,
	0xC3,
	0xC3,
	0xC3,
	0xC3,
	0xC3,
	0xC3,
};

void FixCrashOnCalendar()
{
	char crashFix[] =
	{
		0xB0, 0x00, 0x90
	};
	injector::WriteMemoryRaw(0x004C14CF, crashFix, sizeof(crashFix), true); // Fix crash on calendar
}

void FixVirtualMemory()
{
	char memFix[] =
	{
		0xEB, 0x20
	};

	// Fix "Juiced requires virtual memory to be enabled"
	injector::MakeNOP(0x0059BE39, 2, true);
	injector::WriteMemoryRaw(0x0059BE40, memFix, sizeof(memFix), true);
}

extern "C" void /*__declspec(dllexport)*/ __cdecl SetControlType(char* fmt, int i)
{
	__asm push eax
	controlType = i;
	__asm pop eax
}

extern "C" float __declspec(dllexport) __stdcall GetAxle()
{
	__asm push ecx
	int a, b;
	__asm mov a, edi
	__asm mov b, esi
	__asm pop ecx
	if (controlType == ControlType::Menu)
	{
		switch (a)
		{
		case AxlesMenu::LookDown:
		case AxlesMenu::LookUp:
			return RY;
		case AxlesMenu::LookLeft:
		case AxlesMenu::LookRight:
			return RX;
		case 0:
			return steering;
		case 1:
			return throttle;
		}
	}
	else
	{
		switch (a)
		{
		case AxlesRace::Steering:
			return steering;
		case AxlesRace::Throttle:
			return throttle;
		case AxlesRace::Brake:
			return brake;
		case AxlesRace::Reverse:
			return GetButtonAsAxle(RaceCodes[17], 0);
		case AxlesRace::Handbrake:
			return GetButtonAsAxle(RaceCodes[16], 0);
		case AxlesRace::LookAround:
			return RX;
		case AxlesRace::LookAroundY:
			return RY;
		}
	}
	return 0.0f;
}

void RaceEnd()
{
	SetControlType(nullptr, ControlType::Menu);
}

void RaceEndv1()
{
	SetControlType(nullptr, ControlType::Menu);
	__asm mov ecx, dword ptr ds : [0x760C94]
}

void RaceStart()
{
	SetControlType(nullptr, ControlType::Race);
}

int ProcessMenuInput(int key)
{
	switch (key)
	{
	case MenuButtons::Down:
	case MenuButtons::DownDigital:
		return GetButtonState(MenuCodes[key], 0) | LY < 0 ? 1 : 0;
	case MenuButtons::Up:
	case MenuButtons::UpDigital:
		return GetButtonState(MenuCodes[key], 0) | LY > 0 ? 1 : 0;
	case MenuButtons::Left:
	case MenuButtons::LeftDigital:
		return GetButtonState(MenuCodes[key], 0) | steering < 0 ? 1 : 0;
	case MenuButtons::Right:
	case MenuButtons::RightDigital:
		return GetButtonState(MenuCodes[key], 0) | steering > 0 ? 1 : 0;
	default:
		return GetButtonState(MenuCodes[key], 0);
	}
	return 0;
}

int ProcessRaceInput(int key)
{
	switch ((RaceButtons)key)
	{
	case RaceButtons::Horn:
	case RaceButtons::LookBack:
	case RaceButtons::Nitro:
		return GetButtonAsAxle(RaceCodes[key], 0);
		break;
	case RaceButtons::Driver1Up:
	case RaceButtons::Driver2Up:
	case RaceButtons::Driver1Down:
	case RaceButtons::Driver2Down:
	case RaceButtons::Driver3Up:
	case RaceButtons::Driver3Down:
		return GetButtonAsOrder(RaceCodes[key], 0);
		break;
	default:
		return GetButtonState(RaceCodes[key], 0);
		break;
	}
	return 0;
}

extern "C" int __declspec(dllexport) __stdcall GetButton()
{
	int a, b;
	__asm mov a, eax
	__asm mov b, edi
	return controlType == ControlType::Race ? ProcessRaceInput(a) : ProcessMenuInput(a);
}

void ReadConfig()
{
	CIniReader iniReader("fixes.ini");

	PatchInput = iniReader.ReadBoolean("Fixes", "PatchInput", false);
	PatchVirtualMemory = iniReader.ReadBoolean("Fixes", "PatchVirtualMemory", false);
	PatchCalendarCrash = iniReader.ReadBoolean("Fixes", "PatchCalendarCrash", false);

	MenuCodes[0] = iniReader.ReadInteger("MenuControls", "Left", JOY_DPAD_LEFT);
	//MenuCodes[1] = iniReader.ReadInteger("MenuControls", "LeftDifital", JOY_DPAD_LEFT);
	MenuCodes[2] = iniReader.ReadInteger("MenuControls", "Right", JOY_DPAD_RIGHT);
	//MenuCodes[3] = iniReader.ReadInteger("MenuControls", "RightDigital", JOY_DPAD_LEFT);
	MenuCodes[4] = iniReader.ReadInteger("MenuControls", "Up", JOY_DPAD_UP);
	//MenuCodes[5] = iniReader.ReadInteger("MenuControls", "UpDigital", JOY_DPAD_UP);
	MenuCodes[6] = iniReader.ReadInteger("MenuControls", "Down", JOY_DPAD_DOWN);
	//MenuCodes[7] = iniReader.ReadInteger("MenuControls", "DownDigital", JOY_DPAD_DOWN);
	MenuCodes[8] = iniReader.ReadInteger("MenuControls", "Accept", JOY_BTN_A);
	MenuCodes[9] = iniReader.ReadInteger("MenuControls", "Menu9", 110);
	MenuCodes[10] = iniReader.ReadInteger("MenuControls", "PageDown", JOY_BTN_Y);
	MenuCodes[11] = iniReader.ReadInteger("MenuControls", "Tab", JOY_BTN_X);
	MenuCodes[12] = iniReader.ReadInteger("MenuControls", "Back", JOY_BTN_B);
	MenuCodes[13] = iniReader.ReadInteger("MenuControls", "Back2", 110);
	MenuCodes[14] = iniReader.ReadInteger("MenuControls", "Menu14", 110);
	MenuCodes[15] = iniReader.ReadInteger("MenuControls", "Menu15", 110);

	RaceCodes[0] = iniReader.ReadInteger("RaceControls", "Race0", 110);
	RaceCodes[1] = iniReader.ReadInteger("RaceControls", "Pause", JOY_BTN_START);
	RaceCodes[2] = iniReader.ReadInteger("RaceControls", "ChangeView", JOY_BTN_BACK);
	RaceCodes[3] = iniReader.ReadInteger("RaceControls", "Nitro", JOY_BTN_X);
	RaceCodes[4] = iniReader.ReadInteger("RaceControls", "Horn", JOY_DPAD_UP);
	RaceCodes[5] = iniReader.ReadInteger("RaceControls", "LookBack", JOY_DPAD_DOWN);
	RaceCodes[6] = iniReader.ReadInteger("RaceControls", "GearDown", JOY_BTN_B);
	RaceCodes[7] = iniReader.ReadInteger("RaceControls", "GearUp", JOY_BTN_A);
	RaceCodes[8] = iniReader.ReadInteger("RaceControls", "Skip", 110);
	RaceCodes[9] = iniReader.ReadInteger("RaceControls", "Driver1Up", 110);
	RaceCodes[10] = iniReader.ReadInteger("RaceControls", "Driver2Up", 110);
	RaceCodes[11] = iniReader.ReadInteger("RaceControls", "Driver1Down", 110);
	RaceCodes[12] = iniReader.ReadInteger("RaceControls", "Driver2Down", 110);
	RaceCodes[13] = iniReader.ReadInteger("RaceControls", "Driver3Up", 110);
	RaceCodes[14] = iniReader.ReadInteger("RaceControls", "Driver3Down", 110);
	RaceCodes[15] = iniReader.ReadInteger("RaceControls", "Race15", 110);
	RaceCodes[16] = iniReader.ReadInteger("RaceControls", "Handbrake", 110);
	RaceCodes[17] = iniReader.ReadInteger("RaceControls", "Reverse", 110);
	RaceCodes[18] = iniReader.ReadInteger("RaceControls", "Axle2", 110);
	RaceCodes[19] = iniReader.ReadInteger("RaceControls", "Axle3", 110);
}

int WINAPI DllMain(HMODULE hInstance, DWORD reason, LPVOID lpReserved)
{
	int version = 0;

	if (reason == DLL_PROCESS_ATTACH)
	{
		char VersionOneReturn[] = {
			0x90
		};
		uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
		IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)(base);
		IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);

		if ((base + nt->OptionalHeader.AddressOfEntryPoint + (0x400000 - base)) == 0x00CD6FB0 || (base + nt->OptionalHeader.AddressOfEntryPoint + (0x400000 - base)) == 0x005CD316) {  // Version Check. Credit goes to thelink2012 and MWisBest.
			version = 1;
		}
		else {
			version = 0;
		}

		// Clear controller mapping
		memset(MenuCodes, 110, sizeof(MenuCodes));
		memset(RaceCodes, 110, sizeof(RaceCodes));

		for (int i = 0; i < 14; i++)
		{
			pressed[i] = 0;
		}

		ReadConfig();

		controlType = ControlType::Menu;
		if (PatchCalendarCrash) FixCrashOnCalendar();
		if (PatchVirtualMemory && version == 0) FixVirtualMemory();

		if (PatchInput)
		{
			if (version == 1) {
				injector::MakeJMP(0x00401640, GetAxle);
				injector::MakeJMP(0x004015D0, GetButton);
				injector::MakeCALL(0x004632DE, RaceStart);	// Switch control type for race
				injector::MakeCALL(0x004525FC, RaceEndv1);	// Switch control type for race end
				injector::WriteMemoryRaw(0x00452601, VersionOneReturn, sizeof(VersionOneReturn), true);
				injector::MakeCALL(0x005BD47E, SetControlType); // Switch control type for pause menu
			}
			else {
				injector::MakeJMP(0x00401640, GetAxle);
				injector::MakeJMP(0x004015D0, GetButton);
				injector::MakeCALL(0x0046175E, RaceStart);	// Switch control type for race
				injector::MakeCALL(0x00450B3B, RaceEnd);	// Switch control type for race end
				injector::MakeCALL(0x005BB0FE, SetControlType); // Switch control type for pause menu
			}

		}

		CreateThread(0, 0, Background, NULL, 0, NULL);
	}
	return TRUE;
}