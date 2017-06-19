#pragma once

#include <3ds/types.h>
#include "MyThread.h"

MyThread *updateCreateThread(void);

void updateThreadMain(void);