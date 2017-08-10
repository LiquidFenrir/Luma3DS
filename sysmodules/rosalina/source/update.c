#include <3ds.h>
#include "update.h"
#include "menu.h"
#include "memory.h"
#include "fmt.h"

static MyThread updateThread;
static u8 ALIGN(8) updateThreadStack[THREAD_STACK_SIZE];

static char releaseTagName[10] = {0};
static char currentVersionString[10] = {0};

MyThread *updateCreateThread(void)
{
	if(R_FAILED(MyThread_Create(&updateThread, updateThreadMain, updateThreadStack, THREAD_STACK_SIZE, 0x3F, CORE_SYSTEM)))
		svcBreak(USERBREAK_PANIC);
	return	&updateThread;
}

// Thanks Jess <3
void AtoW(char* in, u16* out) {
	int i;
	for (i = 0; i < strlen(in); i++)
		out[i] = in[i];
}

void addNotif(const char * title, const char * message)
{
	u16 titleBytes[256] = {0};
	AtoW((char*)title, titleBytes);

	u16 messageBytes[256] = {0};
	AtoW((char*)message, messageBytes);

	newsInit();
	NEWS_AddNotification(titleBytes, strlen(title), messageBytes, strlen(message), NULL, 0, false);
	newsExit();
}

Result setupContext(httpcContext * context, const char * url, u32 * size)
{
	Result ret = 0;
	u32 statuscode = 0;

	ret = httpcOpenContext(context, HTTPC_METHOD_GET, url, 1);
	if (ret != 0) {
		httpcCloseContext(context);
		return ret;
	}

	ret = httpcAddRequestHeaderField(context, "User-Agent", "MultiUpdater");
	if (ret != 0) {
		httpcCloseContext(context);
		return ret;
	}

	ret = httpcSetSSLOpt(context, SSLCOPT_DisableVerify);
	if (ret != 0) {
		httpcCloseContext(context);
		return ret;
	}

	ret = httpcAddRequestHeaderField(context, "Connection", "Keep-Alive");
	if (ret != 0) {
		httpcCloseContext(context);
		return ret;
	}

	ret = httpcBeginRequest(context);
	if (ret != 0) {
		httpcCloseContext(context);
		return ret;
	}

	ret = httpcGetResponseStatusCode(context, &statuscode);
	if (ret != 0) {
		httpcCloseContext(context);
		return ret;
	}

	if ((statuscode >= 301 && statuscode <= 303) || (statuscode >= 307 && statuscode <= 308)) {
		char newurl[0x1000] = {0}; // One 4K page for new URL

		ret = httpcGetResponseHeader(context, "Location", newurl, 0x1000);
		if (ret != 0) {
			httpcCloseContext(context);
			return ret;
		}

		httpcCloseContext(context); // Close this context before we try the next

		//fuck local redirections

		ret = setupContext(context, newurl, size);
		return ret;
	}

	if (statuscode != 200) {
		httpcCloseContext(context);
		return -1;
	}

	ret = httpcGetDownloadSizeState(context, NULL, size);
	if (ret != 0) {
		httpcCloseContext(context);
		return ret;
	}

	return 0;
}

void getApiResponse(const char * url, u8 * buf)
{
	Result ret = 0;

	ret = httpcInit(0);

	httpcContext context;
	u32 contentsize = 0, readsize = 0;

	ret = setupContext(&context, url, &contentsize);

	bool done_one = false;
	u8 useless_buf[0x1000];
	do {
		if (!done_one) {
			ret = httpcDownloadData(&context, buf, 0x1000, &readsize);
			done_one = true;
		}
		else {
			ret = httpcDownloadData(&context, useless_buf, 0x1000, &readsize);
		}
	} while (ret == (Result)HTTPC_RESULTCODE_DOWNLOADPENDING);

	httpcCloseContext(&context);

	httpcExit();
}

//use with https://api.github.com/repos/AuroraWright/Luma3DS/releases/latest
void getReleaseTagName(const char * apiresponse) {
	char * tagstring = "\"tag_name\":\"";
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

void getVersion(void) {
	s64 out;
	u32 version;

	svcGetSystemInfo(&out, 0x10000, 0);
	version = (u32)out;

	if (GET_VERSION_REVISION(version) != 0)
		sprintf(currentVersionString, "v%u.%u.%u", GET_VERSION_MAJOR(version), GET_VERSION_MINOR(version), GET_VERSION_REVISION(version));
	else
		sprintf(currentVersionString, "v%u.%u", GET_VERSION_MAJOR(version), GET_VERSION_MINOR(version));
}

u32 waitForInternet(void) 
{
	acInit();
	u32 wifiStatus;
	ACU_GetWifiStatus(&wifiStatus);
	while (wifiStatus == 0) {
		ACU_GetWifiStatus(&wifiStatus);
		svcSleepThread(300000 * 10000LL);
	}
	acExit();
	return wifiStatus;
}

void updateThreadMain(void)
{
	waitForInternet();
	u64 lastCheck = 0;
	u64 currentTime = osGetTime();
	while(!terminationRequest) 
	{
		currentTime = osGetTime();
		if (lastCheck + (8.64 * 10000000) < currentTime)
		{
			//currentTime = osGetTime();
			char apiresponse[0x1000] = {0};
			getApiResponse("http://api.github.com/repos/AuroraWright/Luma3DS/releases/latest", (u8*)apiresponse);
			getReleaseTagName(apiresponse);
			getVersion();

			//if the download failed, the releaseTagName will be blank, don't send a notif in that case 
			if (releaseTagName[0] != '\0' && strcmp(currentVersionString, releaseTagName)) {
				char messageString[128] = {0};

				sprintf(messageString, "Your Luma is out of date!\nCurrent Luma Version: %s\nMost recent version: %s", currentVersionString, releaseTagName);

				addNotif("Luma3DS update available!", messageString);
			}
			lastCheck = osGetTime();
		}
		svcSleepThread(50 * 1000 * 1000LL);
	}
}
