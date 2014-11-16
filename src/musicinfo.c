/* MusicInfo plugin for pidgin
	Displays whatever is currently playing in Winamp
	in the user's profile, away message, or available message in Pidgin

	Written by Reuben Balik
	Copyright 2010 By Reuben Balik

	Please ask permission before using any of this code
	for any other project/product.
	This code/plugin is provided as-is with no warranty of any kind.
	Use at your own risk.

	Special thanks to Leonardo Fernandes for some of the code.
	Special thanks to George Slavov for help with Unicode stuff
	
	Last Update: October 3, 2010
*/

#include "musicinfo.h"
#include "internal.h"
#include "status.h"
#include "connection.h"
#include "signals.h"
#include "version.h"
#include "notify.h"
#include "savedstatuses.h"

#include "debug.h"
#include "wa_ipc.h"

#include <string.h>
#include <windows.h>


//these are all the strings that the plugin searches for
static char artist_replace[] = "%ar%";
static char title_replace[] = "%so%";
static char start_songText[] = "(mi)";
static char end_songText[] = "(/mi)";
static char start_noSongText[] = "(ns)";
static char end_noSongText[] = "(/ns)";

//window classes
static wchar_t winamp_wnd[] = L"Winamp v1.x";

static int timeoutint = 0;
static GList* accounts = NULL;
static gboolean under_recursion = TRUE;

static gboolean privateMode = FALSE;

static const char pluginTitle[] = "MusicInfo";
static const char pModeOn[] = "MusicInfo Privacy Mode is now ON";
static const char pModeOn_sub[] = "Your buddies will not be able to see what you are listening to.";
static const char pModeOff[] = "MusicInfo Privacy Mode is now OFF";
static const char pModeOff_sub[] = "Your buddies will be able to see what you are listening to.";

//Struct to hold account variables
typedef struct {
	char* prevInfo;
	PurpleConnection* c;
} AcctVar;

// getSong gets the name of the song and artist from the title of the winamp window
// and puts them in the buffers pointed to by title and artist
// len is the size of the title and artist buffers in bytes
// getSong returns FALSE if Winamp/foobar is closed or not playing, TRUE if it successfully got a song
static gboolean getSong(char** title, char** artist)
{
	wchar_t* wide_title;
	char* this_title;
	int count, playing, len;
	int titleSize = 0;
	HWND hwndPlayer;
	char *firsthyphen, *secondhyphen, *startString;

	if(privateMode)
	{
		return FALSE;
	}
	
	//get handle to winamp window
	hwndPlayer = FindWindow(winamp_wnd, NULL);
	if(hwndPlayer == NULL)
		return FALSE;

	playing = SendMessage(hwndPlayer, WM_WA_IPC, 0, IPC_ISPLAYING);
	switch (playing) {
	case 1: // PLAYING
		break;
	case 0: // STOPPED
	case 3: // PAUSED
	default:
		purple_debug_info("MusicInfo", "winamp isn't playing\n");
		return FALSE;
	}

	titleSize = GetWindowTextLength(hwndPlayer) + 1;
	if(titleSize > 0)
	{
		wide_title = g_malloc(titleSize * sizeof(wchar_t));	
		GetWindowText(hwndPlayer,wide_title,titleSize);
	}
	else
	{
		purple_debug_info("MusicInfo","title size was 0\n");
		return FALSE;
	}
		
	//convert to UTF8
	len = WideCharToMultiByte(CP_UTF8, 0, wide_title, -1, NULL, 0, NULL, NULL);
	this_title = g_malloc(len * sizeof(char));
	WideCharToMultiByte(CP_UTF8, 0, wide_title, -1, this_title, len, NULL, NULL);
	purple_debug_info("MusicInfo", "len=%d,this_title=%s\n", len, this_title); 
	g_free(wide_title);

	firsthyphen = g_strstr_len(this_title, len, " - ");

	if(firsthyphen == NULL)
	{
		purple_debug_info("MusicInfo","first hyphen was 0\n");
		g_free(this_title);
		return FALSE;
	}
	
	secondhyphen = g_strrstr_len(firsthyphen, len-(firsthyphen-this_title), " - ");

	startString = g_strstr_len(this_title, len, ".") + 2;

	count = 0;
	
	if(firsthyphen != secondhyphen) //we have an artist
	{
		len = firsthyphen - startString;
		*artist = g_strndup(startString, len);
		firsthyphen += 3;
	}
	else //no artist
	{
		*artist = g_strdup("Unknown");
		secondhyphen = firsthyphen;
		firsthyphen = startString;
	}

	//title should be between firsthyphen and secondhyphen
	purple_debug_info("MusicInfo", "artist is %s\n", *artist);
	len = secondhyphen - firsthyphen;
	*title = g_strndup(firsthyphen, len);
	purple_debug_info("MusicInfo", "title is %s\n", *title);

	g_free(this_title);

	return TRUE;
}

// replaces the first instance of replaceStr in text with replaceWith
// max is the size of text
// all strings should already be in utf8 but i'm not actually using the utf8 functions
// because this is easier just going by bytes instead of characters
static gboolean replaceText(char** text, char* replaceStr, char* replaceWith)
{
	char *pos, *foundAt, *replacePos;
	int cur;
	char* newString;

	foundAt = strstr(*text, replaceStr);

	if(foundAt == NULL)
	{
		return FALSE;
	}
	newString = g_malloc(sizeof(char)*(strlen(*text) + strlen(replaceWith)));

	pos = *text;
	cur = 0;

	while(pos < foundAt)
	{
		newString[cur] = *pos;
		pos++;
		cur++;
	}

	replacePos = replaceWith;
	while(*replacePos != '\0')
	{
		newString[cur] = *replacePos;
		replacePos++;
		cur++;
	}

	pos+=strlen(replaceStr);

	while(*pos != '\0')
	{
		newString[cur] = *pos;
		pos++;
		cur++;
	}
	newString[cur] = '\0';
	
	g_free(*text);
	*text = newString;

	return TRUE;
}

//hides all text from startReplace to endReplace
//text must be NULL terminated, text is replaced with a pointer to a new character buffer
static void hideBetween(char** text, char* startReplace, char* endReplace)
{
	char *hidepos, *endhidepos, *pos;
	char* newString;
	int n;

	pos = *text;
	hidepos = strstr(*text, startReplace);
	if(hidepos == NULL)
	{
		return;
	}
	endhidepos = strstr(hidepos + 1, endReplace);
	if(endhidepos == NULL)
	{
		return;
	}
	endhidepos += strlen(endReplace);
	newString = g_malloc(strlen(*text)*sizeof(char));
	
	n = 0;
	while(pos < hidepos)
	{
		newString[n] = *pos;
		n++;
		pos++;
	}

	pos = endhidepos;
	while(*pos != '\0')
	{
		newString[n] = *pos;
		n++;
		pos++;
	}
	newString[n] = '\0';

	g_free(*text);
	*text = newString;
}

//gets the text of whatever the user sets the status to. (not necessarily what status is on the server)
//client_text and server_text are freed if it returns false
static gboolean getStatusText(char** client_text, char** server_text, PurpleAccount *account, const char** sid)
{
	PurpleStatus* status;
	PurpleSavedStatus* savedStatus;
	PurpleSavedStatusSub* substat;
	const char* statusMessage;

	status = purple_account_get_active_status(account);
	if (status == NULL)
       	{
		purple_debug_fatal("MusicInfo", "Account with NULL status\n");
		return FALSE;
	}

	*sid = purple_status_get_id(status);
	statusMessage = purple_status_get_attr_string(status, "message");
	if(statusMessage != NULL)
	{
		*server_text = g_strdup(statusMessage);
	}
	else
	{
		*server_text = g_strdup("\0");
	}


	savedStatus = purple_savedstatus_get_current();
	if (savedStatus == NULL) 
	{
		purple_debug_fatal("MusicInfo", "Account with NULL saved status\n");
		g_free(*server_text);
		return FALSE;
	}
	
	statusMessage = NULL;
	if(purple_savedstatus_has_substatuses(savedStatus))
	{
		substat = purple_savedstatus_get_substatus(savedStatus, account);
		if(substat != NULL)
			statusMessage = purple_savedstatus_substatus_get_message(substat);
	}
	
	if(statusMessage == NULL)
	{
		statusMessage = purple_savedstatus_get_message(savedStatus);
	}
		
	if(statusMessage == NULL)
	{
		g_free(*server_text);
		return FALSE;
	}
	
	*client_text = g_strdup(statusMessage);
	return TRUE;
}

// update the info for the connection gc
// prevInfo and prevStat are the previously set data for that account (so we don't keep setting the same thing over)
char* updateInfo(PurpleConnection *gc, char* prevInfo, char* title, char* artist, gboolean isPlaying, gboolean updateStatus)
{
	PurpleAccount* account;

	char* curAcct;
	char* statusText = NULL;
	char* servStatusText = NULL;
	const char* sid = NULL;
	const char* acctInfo = NULL;
	gboolean hasStatus = FALSE;

	account = purple_connection_get_account(gc);

	acctInfo = purple_account_get_user_info(account);

	if(acctInfo != NULL)
	{
		curAcct = g_strdup(acctInfo);
	}
	else
	{
		curAcct = g_strdup("\0");
	}

	hasStatus = getStatusText(&statusText, &servStatusText, account, &sid);

	if(isPlaying)
	{
		//replace the appropriate text
		replaceText(&curAcct, start_songText, "\0");
		replaceText(&curAcct, end_songText, "\0");
		replaceText(&curAcct, artist_replace, artist);
		replaceText(&curAcct, title_replace, title);

		//hide everything in the "no song" tag
		hideBetween(&curAcct, start_noSongText, end_noSongText);

		//and do it for the status
		if(hasStatus)
		{
			replaceText(&statusText, start_songText, "\0");
			replaceText(&statusText, end_songText, "\0");
			replaceText(&statusText, artist_replace, artist);
			replaceText(&statusText, title_replace, title);

			hideBetween(&statusText, start_noSongText, end_noSongText);
		}
	}
	else
	{
		//replace the appropriate text
		replaceText(&curAcct, start_noSongText, "\0");
		replaceText(&curAcct, end_noSongText, "\0");

		//hide everything in the song tag
		hideBetween(&curAcct, start_songText, end_songText);

		//and for the status:
		if(hasStatus)
		{
			replaceText(&statusText, start_noSongText, "\0");
			replaceText(&statusText, end_noSongText, "\0");

			hideBetween(&statusText, start_songText, end_songText);
		}
	}


	//this is so we don't update the server if we don't have to
	if(strcmp(curAcct, prevInfo) != 0)
	{
		serv_set_info(gc, curAcct);
	}
	if(hasStatus)
	{
		if(updateStatus || strcmp(statusText, servStatusText) != 0)
		{
			under_recursion = TRUE;
			purple_account_set_status(account, sid, TRUE, "message", statusText, NULL);
			under_recursion = FALSE;
		}
		g_free(statusText);
		g_free(servStatusText);
	}
	return curAcct;
}

// this is the function called by the timer
// it loops through all accounts
static gboolean callUpdateInfo()
{
	GList* cur;
	AcctVar* curAcct;
	gboolean isPlaying;
	char* prevInfo;
	char* title;
	char* artist;

	isPlaying = getSong(&title, &artist);

	cur = g_list_first(accounts);

	while(cur != NULL)
	{
		curAcct = (AcctVar*)cur->data;
		prevInfo = updateInfo(curAcct->c, curAcct->prevInfo,
			       title, artist, isPlaying, FALSE);
		g_free(curAcct->prevInfo);
		curAcct->prevInfo = prevInfo;
		cur = cur->next;
	}
	if(isPlaying)
	{
		g_free(title);
		g_free(artist);
	}
	return TRUE;
}

static void toggle_privacy(PurplePluginAction *action)
{
	privateMode = !privateMode;
	purple_debug_info("MusicInfo", "privacy mode switched");

	if(privateMode)
		purple_notify_info(NULL, pluginTitle, pModeOn,
			pModeOn_sub);
	else
		purple_notify_info(NULL, pluginTitle, pModeOff,
			pModeOff_sub);
}

static void online(PurpleConnection *gc)
{
	char* prevInfo;
	AcctVar* acctvar;

	prevInfo = g_strdup("\0");
	acctvar = (AcctVar*) g_malloc(sizeof(AcctVar));

	acctvar->prevInfo = prevInfo;
	acctvar->c = gc;

	accounts = g_list_append(accounts, (gpointer)acctvar);

	if(timeoutint == 0)
	{
		timeoutint = g_timeout_add(CHECK_INTERVAL, callUpdateInfo, NULL);
	}
}

static void status_changed(PurpleAccount *account, PurpleStatus *old, PurpleStatus *news) 
{
	char *title, *artist, *prevInfo;
	gboolean isPlaying;
	GList* cur;
	AcctVar* curAcct;
	PurpleConnection* pc;

	if(under_recursion)
	{
		return;
	}
		
	pc = purple_account_get_connection(account);

	isPlaying = getSong(&title, &artist);

	cur = g_list_first(accounts);
	prevInfo = NULL;
	curAcct = NULL;
	//find the correct account so we can get the prevInfo
	while(cur != NULL)
	{
		curAcct = (AcctVar*)cur->data;
		if(curAcct->c == pc)
		{
			break;
		}
		cur = cur->next;
	}
	if(cur != NULL)
	{
		prevInfo = updateInfo(pc, curAcct->prevInfo, title, artist, isPlaying, TRUE);
		g_free(curAcct->prevInfo);
		curAcct->prevInfo = prevInfo;
	}
	if(isPlaying)
	{
		g_free(artist);
		g_free(title);
	}
}

static void signoff(PurpleConnection *gc)
{
	GList* cur;
	AcctVar* curAcct;

	cur = g_list_first(accounts);
	while(cur != NULL)
	{
		curAcct = (AcctVar*)cur->data;
		if( curAcct->c == gc)
		{
			g_free(curAcct->prevInfo);
			g_free(curAcct);

			accounts = g_list_delete_link(accounts, cur);
			break;
		}
		cur = cur->next;
	}

	if(g_list_length(accounts) == 0)
	{
		g_source_remove(timeoutint);
		timeoutint = 0;
	}

}

static gboolean plugin_load(PurplePlugin *plugin)
{
	purple_signal_connect(purple_connections_get_handle(), "signed-on",
			plugin, PURPLE_CALLBACK(online), NULL);

	purple_signal_connect(purple_connections_get_handle(), "signing-off",
			plugin, PURPLE_CALLBACK(signoff), NULL);

	purple_signal_connect(purple_accounts_get_handle(), "account-status-changed",
			plugin, PURPLE_CALLBACK(status_changed), NULL);

	under_recursion = TRUE;

	return TRUE;
}

static gboolean
plugin_unload(PurplePlugin *plugin)
{
	purple_signal_disconnect(purple_connections_get_handle(), "signed-on",
		plugin, PURPLE_CALLBACK(online));

	purple_signal_disconnect(purple_connections_get_handle(), "signing-off",
			plugin, PURPLE_CALLBACK(signoff));

	purple_signal_disconnect(purple_accounts_get_handle(), "account-status-changed",
		plugin, PURPLE_CALLBACK(status_changed));

	if(timeoutint > 0)
	{
		g_source_remove(timeoutint);
		timeoutint = 0;
	}

	return TRUE;
}

static GList *
miactions(PurplePlugin *plugin, gpointer context)
{
	GList *actlist = NULL;
	PurplePluginAction *act = NULL;

	act = purple_plugin_action_new(_("Toggle Privacy Mode"),
			toggle_privacy);

	actlist = g_list_append(actlist, act);

	return actlist;
}


static PurplePluginInfo info =
{
	PURPLE_PLUGIN_MAGIC,
	PURPLE_MAJOR_VERSION,
	PURPLE_MINOR_VERSION,
	PURPLE_PLUGIN_STANDARD,                             /**< type           */
	NULL,                                             /**< ui_requirement */
	0,                                                /**< flags          */
	NULL,                                             /**< dependencies   */
	PURPLE_PRIORITY_DEFAULT,                            /**< priority       */

	"gtk-win32-rbalik-musicinfo",                            /**< id             */
	"MusicInfo",				                   /**< name           */
	"1.5",                                          /**< version        */
	                                                  /**  summary        */
	"Share your musical taste with people on your buddy list",
	                                                  /**  description    */
	"Displays whatever song you are listening to in your info, away message, or available message.",
	"Reuben Balik <rsbalik@gmail.com>",       /**< author         */
	"http://www.pidginmusic.info",        /**< homepage       */

	plugin_load,                                      /**< load           */
	plugin_unload,                                             /**< unload         */
	NULL,                                             /**< destroy        */

	NULL,                                             /**< ui_info        */
	NULL,                                             /**< extra_info     */
	NULL,                                      /**< prefs_info     */
	miactions,

	//seems that pidgin added a few extra entries to this struct
	NULL,
	NULL,
	NULL,
	NULL,
};

static void
init_plugin(PurplePlugin *plugin)
{

}

PURPLE_INIT_PLUGIN(musicinfo, init_plugin, info)
