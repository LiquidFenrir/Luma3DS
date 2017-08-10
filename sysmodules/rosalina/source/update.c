#include <3ds.h>
#include "update.h"
#include "menu.h"
#include "memory.h"
#include "fmt.h"
#include "cybertrust.h"
#include "digicert.h"

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
void AtoW(char* in, u16* out)
{
	int i;
	for(i = 0; i < strlen(in); i++)
		out[i] = in[i];
}

void addNotif(const char * title, const char * message, bool update)
{
	u16 titleBytes[256] = {0};
	AtoW((char*)title, titleBytes);
	u32 titleLength = strlen(title);

	u16 messageBytes[256] = {0};
	AtoW((char*)message, messageBytes);
	u32 messageLength = strlen(message);

	if(update)
	{
		//most of this below is taken from ctrulib with small modifications

		Handle newsHandle;
		srvGetServiceHandle(&newsHandle, "news:s");

		NotificationHeader header = {0};
		header.dataSet = true;
		header.unread = true;
		header.enableJPEG = false;
		header.isSpotPass = true;
		header.isOptedOut = false;

		header.processID = 0x0004000000585900; //miniLumaUpdater titleid
		header.time = osGetTime();

		memcpy(header.title, titleBytes, (titleLength < 32 ? titleLength + 1 : 32) * sizeof(u16));

		u32 * cmdbuf = getThreadCommandBuffer();

		cmdbuf[0] = IPC_MakeHeader(0x1, 3, 6); // 0x100C6
		cmdbuf[1] = sizeof(NotificationHeader);
		cmdbuf[2] = (messageLength + 1) * sizeof(u16);
		cmdbuf[3] = 0; //imageSize
		cmdbuf[4] = IPC_Desc_Buffer(sizeof(NotificationHeader), IPC_BUFFER_R);
		cmdbuf[5] = (u32) &header;
		cmdbuf[6] = IPC_Desc_Buffer((messageLength + 1) * sizeof(u16), IPC_BUFFER_R);
		cmdbuf[7] = (u32) messageBytes;
		cmdbuf[8] = IPC_Desc_Buffer(0, IPC_BUFFER_R); //imageSize, again
		cmdbuf[9] = (u32) NULL; //pointer to imageData

		svcSendSyncRequest(newsHandle);
		svcCloseHandle(newsHandle);
	}
	else
	{
		newsInit();
		NEWS_AddNotification(titleBytes, titleLength, messageBytes, messageLength, NULL, 0, false);
		newsExit();
	}
}

Result setupContext(httpcContext * context, const char * url, u32 * size)
{
	Result ret = 0;
	u32 statuscode = 0;

	ret = httpcOpenContext(context, HTTPC_METHOD_GET, url, 1);
	if(ret != 0)
	{
		httpcCloseContext(context);
		return ret;
	}

	ret = httpcAddRequestHeaderField(context, "User-Agent", "LumaSelfCheck");
	if(ret != 0)
	{
		httpcCloseContext(context);
		return ret;
	}

	ret = httpcAddTrustedRootCA(context, cybertrust_cer, cybertrust_cer_len);
	if(ret != 0)
	{
		httpcCloseContext(context);
		return ret;
	}

	ret = httpcAddTrustedRootCA(context, digicert_cer, digicert_cer_len);
	if(ret != 0)
	{
		httpcCloseContext(context);
		return ret;
	}

	ret = httpcAddRequestHeaderField(context, "Connection", "Keep-Alive");
	if(ret != 0)
	{
		httpcCloseContext(context);
		return ret;
	}

	ret = httpcBeginRequest(context);
	if(ret != 0)
	{
		httpcCloseContext(context);
		return ret;
	}

	ret = httpcGetResponseStatusCode(context, &statuscode);
	if(ret != 0)
	{
		httpcCloseContext(context);
		return ret;
	}

	if((statuscode >= 301 && statuscode <= 303) || (statuscode >= 307 && statuscode <= 308))
	{
		char newurl[0x1000] = {0}; // One 4K page for new URL

		ret = httpcGetResponseHeader(context, "Location", newurl, 0x1000);
		if(ret != 0)
		{
			httpcCloseContext(context);
			return ret;
		}

		httpcCloseContext(context); // Close this context before we try the next
		ret = setupContext(context, newurl, size);
		return ret;
	}

	if(statuscode != 200)
	{
		httpcCloseContext(context);
		return -1;
	}

	ret = httpcGetDownloadSizeState(context, NULL, size);
	if(ret != 0)
	{
		httpcCloseContext(context);
		return ret;
	}

	return 0;
}

void getApiResponse(const char * url, u8 * buf, u32 bufsize)
{
	Result ret = 0;

	ret = httpcInit(0);

	httpcContext context;
	u32 contentsize = 0, readsize = 0;

	ret = setupContext(&context, url, &contentsize);

	bool done_one = false;
	u8 useless;
	do
	{
		if(!done_one)
		{
			ret = httpcDownloadData(&context, buf, bufsize, &readsize);
			done_one = true;
		}
		else
		{
			ret = httpcDownloadData(&context, &useless, sizeof(u8), &readsize);
		}
	}
	while(ret == (Result)HTTPC_RESULTCODE_DOWNLOADPENDING);

	httpcCloseContext(&context);
	httpcExit();
}

//use with https://api.github.com/repos/AuroraWright/Luma3DS/releases/latest
void getReleaseTagName(const char * apiresponse)
{
	char * tagstring = "\"tag_name\":\"";
	char * endstring = "\",";

	char *tagstart, *tagend;

	if((tagstart = strstr(apiresponse, tagstring)) != NULL)
	{
		if((tagend = strstr(tagstart, endstring)) != NULL)
		{
			tagstart += strlen(tagstring);
			int len = tagend-tagstart;
			memcpy(releaseTagName, tagstart, len);
		}
	}
}

void getVersion(void)
{
	s64 out;
	u32 version;

	svcGetSystemInfo(&out, 0x10000, 0);
	version = (u32)out;

	if(GET_VERSION_REVISION(version) != 0)
		sprintf(currentVersionString, "v%u.%u.%u", GET_VERSION_MAJOR(version), GET_VERSION_MINOR(version), GET_VERSION_REVISION(version));
	else
		sprintf(currentVersionString, "v%u.%u", GET_VERSION_MAJOR(version), GET_VERSION_MINOR(version));
}

u32 waitForInternet(void) 
{
	acInit();
	u32 wifiStatus;
	ACU_GetWifiStatus(&wifiStatus);
	while(wifiStatus == 0)
	{
		ACU_GetWifiStatus(&wifiStatus);
		svcSleepThread((u64)3 * 1000 * 1000 * 1000); //3 seconds
	}
	acExit();
	return wifiStatus;
}

void updateThreadMain(void)
{
	waitForInternet();
	u64 currentTime, lastCheck = 0;
	while(!terminationRequest) 
	{
		currentTime = osGetTime();
		//1 day in milliseconds
		if(lastCheck + (24 * 60 * 60 * 1000) < currentTime)
		{
			char apiresponse[0x200] = {0};
			getApiResponse("https://api.github.com/repos/AuroraWright/Luma3DS/releases/latest", (u8*)apiresponse, 0x200);
			getReleaseTagName(apiresponse);
			getVersion();

			//if the download failed, the releaseTagName will be blank, don't send a notif in that case 
			if(releaseTagName[0] != '\0' && strcmp(currentVersionString, releaseTagName))
			{
				char messageString[128] = {0};

				sprintf(messageString, "Your Luma3DS is out of date!\nInstalled Luma3DS version: %s\nMost recent version: %s", currentVersionString, releaseTagName);

				addNotif("Luma3DS update available!", messageString, true);
			}
			lastCheck = currentTime;
		}
		svcSleepThread((u64)50 * 1000 * 1000); //50 ms
	}
}
