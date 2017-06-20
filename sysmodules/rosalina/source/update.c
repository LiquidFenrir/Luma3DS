#include <3ds.h>
#include "update.h"
#include "menu.h"
#include "memory.h"
#include "fmt.h"

static MyThread updateThread;
static u8 ALIGN(8) updateThreadStack[THREAD_STACK_SIZE];

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

void updateThreadMain(void)
{
	newsInit();
	while(!terminationRequest) 
	{
		if ((HID_PAD & (BUTTON_UP | BUTTON_R1 | BUTTON_START)) == (BUTTON_UP | BUTTON_R1 | BUTTON_START))
		{
			char messageString[128];

			s64 out;
			u32 version;

			svcGetSystemInfo(&out, 0x10000, 0);
			version = (u32)out;

			sprintf(messageString, "Current Luma Version: v%u.%u.%u", GET_VERSION_MAJOR(version), GET_VERSION_MINOR(version), GET_VERSION_REVISION(version));

			u16 messageBytes[256];

			AtoW(messageString, messageBytes);

			NEWS_AddNotification(u"Test", 4, messageBytes, strlen(messageString), NULL, 0, false);
		}
        svcSleepThread(50 * 1000 * 1000LL);
	}
	newsExit();
}