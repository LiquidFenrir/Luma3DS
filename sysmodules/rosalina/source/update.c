#include <3ds.h>
#include "update.h"
#include "menu.h"
#include "memory.h"
#include "fmt.h"

static MyThread updateThread;
static u8 ALIGN(8) updateThreadStack[THREAD_STACK_SIZE];

static char releaseTagName[10] = {0};
static u32 releaseCommitHash = 0;

static char currentVersionString[10] = {0};
static u32 currentCommitHash = 0;

MyThread *updateCreateThread(void)
{
	if(R_FAILED(MyThread_Create(&updateThread, updateThreadMain, updateThreadStack, THREAD_STACK_SIZE, 0x3F, CORE_SYSTEM)))
		svcBreak(USERBREAK_PANIC);
	return	&updateThread;
}

//use with https://api.github.com/repos/AuroraWright/Luma3DS/releases/latest
void getReleaseTagName(const char * apiresponse) {
	char * tagstring = "\"tag_name\": \"";
	char * endstring = "\",";

	char *tagstart, *tagend;

	if ((tagstart = strstr(apiresponse, tagstring)) != NULL) {
		if ((tagend = strstr(tagstart, endstring)) != NULL) {
			tagstart += strlen(tagstring);
			int len = tagend-tagstart;
			memcpy(releaseTagName, tagstart, len);
		}
	}
}

//use with https://api.github.com/repos/AuroraWright/Luma3DS/tags
//and use getReleaseTagName before
void getReleaseCommitHash(const char * apiresponse) {
	char namestring[21] = {0};
	sprintf(namestring, "\"name\": \"%s\"", releaseTagName);
	char * shastring = "\"sha\": \"";

	char *namestart, *shastart;

	if ((namestart = strstr(apiresponse, namestring)) != NULL) {
		if ((shastart = strstr(namestart, shastring)) != NULL) {
			shastart += strlen(shastring);
			releaseCommitHash = (u32)xstrtoul(shastart, &shastart+8, 16, 0, 0);
		}
	}
}

void getVersion(void) {
	s64 out;
	u32 version;

	svcGetSystemInfo(&out, 0x10000, 0);
	version = (u32)out;

	if (GET_VERSION_REVISION(version) != 0)
		sprintf(currentVersionString, "v%u.%u.%u", GET_VERSION_MAJOR(version), GET_VERSION_MINOR(version), GET_VERSION_REVISION(version));
	else
		sprintf(currentVersionString, "v%u.%u", GET_VERSION_MAJOR(version), GET_VERSION_MINOR(version));

	svcGetSystemInfo(&out, 0x10000, 1);
	currentCommitHash = (u32)out;
}

void updateThreadMain(void)
{
	newsInit();
	while(!terminationRequest) 
	{
		if ((HID_PAD & (BUTTON_UP | BUTTON_R1 | BUTTON_START)) == (BUTTON_UP | BUTTON_R1 | BUTTON_START))
		{
			getVersion();
			
			char messageString[32] = {0};
			sprintf(messageString, "Current Luma Version: %s", currentVersionString);
			
			u16 messageBytes[64] = {0};
			size_t messageSize = utf8_to_utf16(messageBytes, (u8*)messageString, 31);

			NEWS_AddNotification(u"Test", 4, messageBytes, messageSize, NULL, 0, false);
			
		}
		svcSleepThread(50 * 1000 * 1000LL);
	}
	newsExit();
}