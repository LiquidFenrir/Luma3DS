#include <3ds.h>
#include "update.h"
#include "menu.h"
#include "memory.h"

static MyThread updateThread;
static u8 ALIGN(8) updateThreadStack[THREAD_STACK_SIZE];

MyThread *updateCreateThread(void)
{
	if(R_FAILED(MyThread_Create(&updateThread, updateThreadMain, updateThreadStack, THREAD_STACK_SIZE, 0x3F, CORE_SYSTEM)))
		svcBreak(USERBREAK_PANIC);
	return	&updateThread;
}

void updateThreadMain(void)
{
	newsInit();
	while(!terminationRequest) 
	{
		if ((HID_PAD & (BUTTON_UP | BUTTON_R1 | BUTTON_START)) == (BUTTON_UP | BUTTON_R1 | BUTTON_START))
		{
			NEWS_AddNotification(u"test", 4, u"test test", 9, NULL, 0, false);
		}
        svcSleepThread(50 * 1000 * 1000LL);
	}
	newsExit();
}