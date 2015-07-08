#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <3ds.h>

#include "gfx.h"
#include "menu.h"
#include "background.h"
#include "statusbar.h"
#include "filesystem.h"
#include "error.h"
#include "netloader.h"
#include "regionfree.h"
#include "boot.h"

bool brewMode = false;
u8 sdmcCurrent = 0;
u64 nextSdCheck = 0;

menu_s menu;
u32 wifiStatus = 0;
u8 batteryLevel = 5;
u8 charging = 0;
int rebootCounter;

static enum
{
	NETLOADER_INACTIVE,
	NETLOADER_ACTIVE,
	NETLOADER_ERROR,
} netloader_state = NETLOADER_INACTIVE;

int debugValues[100];

void drawDebug()
{
	char str[256];
	sprintf(str, "hello3 %d %d %d %d\n", debugValues[0], debugValues[1], debugValues[2], debugValues[3]);
	gfxDrawText(GFX_TOP, GFX_LEFT, NULL, str, 32, 100);
}

void renderFrame(u8 bgColor[3], u8 waterBorderColor[3], u8 waterColor[3])
{
	// background stuff
	drawBackground(bgColor, waterBorderColor, waterColor);

	// status bar
	drawStatusBar(wifiStatus, charging, batteryLevel);

	// current directory
	printDirectory();

	// debug text
	// drawDebug();

	//menu stuff
	if(rebootCounter<257)
	{
		//about to reboot
		drawError(GFX_BOTTOM,
			"Reboot",
			"    You're about to reboot your console into home menu.\n\n"
			"                                                                                            A : Proceed\n"
			"                                                                                            B : Cancel\n");
	}else if(!sdmcCurrent)
	{
		//no SD
		drawError(GFX_BOTTOM,
			"No SD detected",
			"    It looks like your 3DS doesn't have an SD inserted into it.\n"
			"    Please insert an SD card for optimal homebrew launcher performance !\n");
	}else if(sdmcCurrent<0)
	{
		//SD error
		drawError(GFX_BOTTOM,
			"SD Error",
			"    Something unexpected happened when trying to mount your SD card.\n"
			"    Try taking it out and putting it back in. If that doesn't work,\n"
			"please try again with another SD card.");
	}else if(netloader_state == NETLOADER_ACTIVE){
		char bof[256];
		u32 ip = gethostid();
		sprintf(bof,
			"    NetLoader Active\n"
			"    IP: %lu.%lu.%lu.%lu, Port: %d\n"
			"                                                                                            B : Cancel\n",
			ip & 0xFF, (ip>>8)&0xFF, (ip>>16)&0xFF, (ip>>24)&0xFF, NETLOADER_PORT);

		drawError(GFX_BOTTOM,
			"NetLoader",
			bof);
	}else if(netloader_state == NETLOADER_ERROR){
		netloader_draw_error();
	}else{
		//got SD
		drawMenu(&menu);
	}
}

bool secretCode(void)
{
	static const u32 secret_code[] =
	{
		KEY_UP,
		KEY_UP,
		KEY_DOWN,
		KEY_DOWN,
		KEY_LEFT,
		KEY_RIGHT,
		KEY_LEFT,
		KEY_RIGHT,
		KEY_B,
		KEY_A,
	};

	static u32 state   = 0;
	static u32 timeout = 30;
	u32 down = hidKeysDown();

	if(down & secret_code[state])
	{
		++state;
		timeout = 30;

		if(state == sizeof(secret_code)/sizeof(secret_code[0]))
		{
			state = 0;
			return true;
		}
	}

	if(timeout > 0 && --timeout == 0)
	state = 0;

	return false;
}

// handled in main
// doing it in main is preferred because execution ends in launching another 3dsx
void __appInit()
{
	srvInit();
}

// same
void __appExit()
{
	srvExit();
}

int main()
{
	aptInit();
	gfxInitDefault();
	initFilesystem();
	openSDArchive();
	hidInit(NULL);
	acInit();
	ptmInit();
	regionFreeInit();
	netloader_init();

	initBackground();
	initErrors();
	initMenu(&menu);

	u8 sdmcPrevious = 0;
	FSUSER_IsSdmcDetected(NULL, &sdmcCurrent);
	if(sdmcCurrent == 1)
	{
		scanHomebrewDirectory(&menu);
	}
	sdmcPrevious = sdmcCurrent;
	nextSdCheck = osGetTime()+250;

	srand(svcGetSystemTick());

	rebootCounter=257;

	while(aptMainLoop())
	{
		if (nextSdCheck < osGetTime())
		{
			FSUSER_IsSdmcDetected(NULL, &sdmcCurrent);

			if(sdmcCurrent == 1 && (sdmcPrevious == 0 || sdmcPrevious < 0))
			{
				closeSDArchive();
				openSDArchive();
				scanHomebrewDirectory(&menu);
			}
			else if(sdmcCurrent < 1 && sdmcPrevious == 1)
			{
				clearMenuEntries(&menu);
			}
			sdmcPrevious = sdmcCurrent;
			nextSdCheck = osGetTime()+250;
		}

		ACU_GetWifiStatus(NULL, &wifiStatus);
		PTMU_GetBatteryLevel(NULL, &batteryLevel);
		PTMU_GetBatteryChargeState(NULL, &charging);
		hidScanInput();

		updateBackground();

		if(netloader_state == NETLOADER_ACTIVE){
			if(hidKeysDown()&KEY_B){
				netloader_deactivate();
				netloader_state = NETLOADER_INACTIVE;
			}else{
				int rc = netloader_loop();
				if(rc > 0)
				{
					netloader_boot = true;
					break;
				}else if(rc < 0){
					netloader_state = NETLOADER_ERROR;
				}
			}
		}else if(netloader_state == NETLOADER_ERROR){
			if(hidKeysDown()&KEY_B)
				netloader_state = NETLOADER_INACTIVE;
		}else if(rebootCounter==256){
			if(hidKeysDown()&KEY_A)
			{
				//reboot
				aptOpenSession();
					APT_HardwareResetAsync(NULL);
				aptCloseSession();
				rebootCounter--;
			}else if(hidKeysDown()&KEY_B)
			{
				rebootCounter++;
			}
		}else if(rebootCounter==257){
			if(hidKeysDown()&KEY_START)rebootCounter--;
			if(hidKeysDown()&KEY_Y)
			{
				if(netloader_activate() == 0)
					netloader_state = NETLOADER_ACTIVE;
			}
			if(secretCode())brewMode = true;
			else if(hidKeysDown()&KEY_B) {
					changeDirectory("..");
					clearMenuEntries(&menu);
					initMenu(&menu);
					scanHomebrewDirectory(&menu);
			}
			else if(updateMenu(&menu)) {
				menuEntry_s* me=getMenuEntry(&menu, menu.selectedEntry);
				if(me->type == MENU_ENTRY_FOLDER) {
					changeDirectory(me->executablePath);
					clearMenuEntries(&menu);
					initMenu(&menu);
					scanHomebrewDirectory(&menu);
				}
				else
					break;
			}
		}

		if(brewMode)renderFrame(BGCOLOR, BEERBORDERCOLOR, BEERCOLOR);
		else renderFrame(BGCOLOR, WATERBORDERCOLOR, WATERCOLOR);

		if(rebootCounter<256)
		{
			if(rebootCounter<0)rebootCounter=0;
			gfxFadeScreen(GFX_TOP, GFX_LEFT, rebootCounter);
			gfxFadeScreen(GFX_BOTTOM, GFX_BOTTOM, rebootCounter);
			if(rebootCounter>0)rebootCounter-=6;
		}

		gfxFlushBuffers();
		gfxSwapBuffers();

		gspWaitForVBlank();
	}

	// cleanup whatever we have to cleanup
	netloader_exit();
	ptmExit();
	acExit();
	hidExit();
	gfxExit();
	exitFilesystem();
	closeSDArchive();
	aptExit();

	// check for netloader
	if (netloader_boot)
	{
		regionFreeExit();
		return bootApp(netloadedPath);
	}

	menuEntry_s* menuEntry = getMenuEntry(&menu, menu.selectedEntry);
	if(regionFreeAvailable && menuEntry == &regionfreeEntry)
	{
		regionFreeRun();
		return 0;
	}

	regionFreeExit();
	return bootApp(menuEntry->executablePath);
}
