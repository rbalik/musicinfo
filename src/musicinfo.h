/* MusicInfo plugin for pidgin
	Displays whatever is currently playing in Winamp
	in the user's profile, away message, or available message in Pidgin

	Written by Reuben Balik
	Copyright 2010 By Reuben Balik
	
	Please ask permission before using any of this code
	for any other project/product.
	This code/plugin is provided as-is with no warranty of any kind.
	Use at your own risk.
	
	Last Update: October 3, 2010
*/

#ifndef MUSICINFO_H
#define MUSICINFO_H

// Makes all the Windows API functions use wchar_t
#define UNICODE

#include "internal.h"
#include "status.h"
#include "connection.h"
#include "signals.h"
#include "version.h"
#include "notify.h"
#include "savedstatuses.h"

#include <string.h>
#include <windows.h>


//how often to check winamp window in ms
#define CHECK_INTERVAL 15000

//All text manipulation functions assume NULL terminated strings

static gboolean getSong(char** title, char** artist);
static gboolean replaceText(char** text, char* replaceStr, char* replaceWith);
static void hideBetween(char** text, char* startReplace, char* endReplace);
static gboolean getStatusText(char** client_text, char** server_text, PurpleAccount *account, const char** sid);
static char* updateInfo(PurpleConnection *gc, char* prevInfo, char* title, char* artist, gboolean isPlaying, gboolean updateStatus);
static gboolean callUpdateInfo();
static void toggle_privacy(PurplePluginAction *action);


#endif
