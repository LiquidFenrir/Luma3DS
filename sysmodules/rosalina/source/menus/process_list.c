/*
*   This file is part of Luma3DS
*   Copyright (C) 2016-2017 Aurora Wright, TuxSH
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*   Additional Terms 7.b and 7.c of GPLv3 apply to this file:
*       * Requiring preservation of specified reasonable legal notices or
*         author attributions in that material or in the Appropriate Legal
*         Notices displayed by works containing it.
*       * Prohibiting misrepresentation of the origin of that material,
*         or requiring that modified versions of such material be marked in
*         reasonable ways as different from the original version.
*/

#include <3ds.h>
#include "menus/process_list.h"
#include "memory.h"
#include "csvc.h"
#include "draw.h"
#include "menu.h"
#include "utils.h"
#include "fmt.h"
#include "gdb/server.h"
#include "minisoc.h"
#include <arpa/inet.h>
#include <sys/socket.h>

typedef struct ProcessInfo
{
    u32 pid;
    u64 titleId;
    char name[8];
    bool isZombie;
} ProcessInfo;

static ProcessInfo infos[0x40] = {0}, infosPrev[0x40] = {0};
extern GDBServer gdbServer;

static inline int ProcessListMenu_FormatInfoLine(char *out, const ProcessInfo *info)
{
    const char *checkbox;
    u32 id;
    for(id = 0; id < MAX_DEBUG && (!(gdbServer.ctxs[id].flags & GDB_FLAG_SELECTED) || gdbServer.ctxs[id].pid != info->pid); id++);
    checkbox = !gdbServer.super.running ? "" : (id < MAX_DEBUG ? "(x) " : "( ) ");

    char commentBuf[23 + 1] = { 0 }; // exactly the size of "Remote: 255.255.255.255"
    memset(commentBuf, ' ', 23);

    if(info->isZombie)
        memcpy(commentBuf, "Zombie", 7);

    else if(gdbServer.super.running && id < MAX_DEBUG)
    {
        if(gdbServer.ctxs[id].state >= GDB_STATE_CONNECTED && gdbServer.ctxs[id].state < GDB_STATE_CLOSING)
        {
            u8 *addr = (u8 *)&gdbServer.ctxs[id].super.addr_in.sin_addr;
            checkbox = "(A) ";
            sprintf(commentBuf, "Remote: %hhu.%hhu.%hhu.%hhu", addr[0], addr[1], addr[2], addr[3]);
        }
        else
        {
            checkbox = "(W) ";
            sprintf(commentBuf, "Port: %d", GDB_PORT_BASE + id);
        }
    }

    return sprintf(out, "%s%-4u    %-8.8s    %s", checkbox, info->pid, info->name, commentBuf); // Theoritically PIDs are 32-bit ints, but we'll only justify 4 digits
}

static void ProcessListMenu_MemoryViewer(const ProcessInfo *info)
{
    s64 startAddress, textTotalRoundedSize, rodataTotalRoundedSize, dataTotalRoundedSize;
    u32 totalSize;
    const u32 destAddress = 0x00100000;
    Handle processHandle;

    Result res = OpenProcessByName(info->name, &processHandle);

    if (R_SUCCEEDED(res))
    {
        svcGetProcessInfo(&textTotalRoundedSize, processHandle, 0x10002);
        svcGetProcessInfo(&rodataTotalRoundedSize, processHandle, 0x10003);
        svcGetProcessInfo(&dataTotalRoundedSize, processHandle, 0x10004);

        totalSize = (u32)(textTotalRoundedSize + rodataTotalRoundedSize + dataTotalRoundedSize);

        svcGetProcessInfo(&startAddress, processHandle, 0x10005);

        res = svcMapProcessMemoryEx(processHandle, destAddress, (u32)startAddress, totalSize);

        if (R_SUCCEEDED(res))
        {
            #define ROWS_PER_SCREEN 0x10
            #define BYTES_PER_ROW 0x10
            #define VIEWER_PAGE_SIZE (ROWS_PER_SCREEN*BYTES_PER_ROW)

            enum MenuModes {
                MENU_MODE_NORMAL = 0,
                MENU_MODE_GOTO,
                MENU_MODE_SEARCH,

                MENU_MODE_MAX,
            };

            typedef struct {
                u32 selected;
                u8 * buf;

                u32 starti;
                u32 max;

                bool editing;
            } MenuData;

            MenuData menus[MENU_MODE_MAX] = {0};
            int menuMode = MENU_MODE_NORMAL;

            // Editing
            void selectedByteIncrement(void) { menus[menuMode].buf[menus[menuMode].selected]++; }
            void selectedByteDecrement(void) { menus[menuMode].buf[menus[menuMode].selected]--; }

            void selectedByteAdd0x10(void) { menus[menuMode].buf[menus[menuMode].selected] += 0x10; }
            void selectedByteSub0x10(void) { menus[menuMode].buf[menus[menuMode].selected] -= 0x10; }
            // ------------------------------------------

            // Movement
            #define SELECTED_DEC(decval) if(menus[menuMode].selected >= decval) menus[menuMode].selected -= decval
            #define SELECTED_INC(incval) if(menus[menuMode].selected < (menus[menuMode].max - incval)) menus[menuMode].selected += incval

            void selectedMoveLeft(void) { SELECTED_DEC(1); }
            void selectedMoveRight(void) { SELECTED_INC(1); }

            void selectedMoveUp(void) { SELECTED_DEC(BYTES_PER_ROW); }
            void selectedMoveDown(void) { SELECTED_INC(BYTES_PER_ROW); }

            void selectedMovePageUp(void) {    SELECTED_DEC(VIEWER_PAGE_SIZE); }
            void selectedMovePageDown(void) { SELECTED_INC(VIEWER_PAGE_SIZE); }
            // ------------------------------------------

            // Viewing
            menus[MENU_MODE_NORMAL].buf = (u8*)destAddress;
            menus[MENU_MODE_NORMAL].max = totalSize/BYTES_PER_ROW;
            // ------------------------------------------

            // Jumping
            u32 gotoAddress = 0;

            void finishJumping(void) { menus[MENU_MODE_NORMAL].selected = __builtin_bswap32(gotoAddress); }

            menus[MENU_MODE_GOTO].buf = (u8*)&gotoAddress;
            menus[MENU_MODE_GOTO].max = sizeof(u32);
            // ------------------------------------------

            // Searching
            #define searchPatternSize menus[MENU_MODE_SEARCH].max
            #define searchPatternMaxSize (u32)VIEWER_PAGE_SIZE
            u8 searchPattern[searchPatternMaxSize] = {0};

            void searchPatternEnlarge(void) { if(searchPatternSize < (searchPatternMaxSize - 1)) searchPatternSize++; }
            void searchPatternReduce(void) { if(searchPatternSize > 0) searchPatternSize--; }

            void finishSearching(void)
            {
                u8 * startpos = menus[MENU_MODE_NORMAL].buf + menus[MENU_MODE_NORMAL].selected;
                menus[MENU_MODE_NORMAL].selected = (u32)memsearch(startpos, searchPattern, totalSize, searchPatternSize) - destAddress;
            }

            menus[MENU_MODE_SEARCH].buf = searchPattern;
            menus[MENU_MODE_SEARCH].max = 1;
            // ------------------------------------------

            void drawMenu(void)
            {
                Draw_Lock();
                Draw_DrawString(10, 10, COLOR_TITLE, "Memory viewer");
                Draw_DrawString(10, 26, COLOR_WHITE, "Use D-PAD to navigate.");

                for (u32 row = menus[menuMode].starti; row < (menus[menuMode].starti + ROWS_PER_SCREEN); row++)
                {
                    u32 offset = row - menus[menuMode].starti;
                    u32 y = 44 + offset*12;

                    offset = row*BYTES_PER_ROW;
                    Draw_DrawFormattedString(10, y, COLOR_TITLE, "%.8lx | ", offset);

                    for (int cursor = 0; cursor < BYTES_PER_ROW; cursor++)
                    {
                        offset += cursor;
                        u32 x = 10+66 + cursor*14 + (cursor >= BYTES_PER_ROW/2)*10;

                        if(offset < menus[menuMode].max)
                        {
                            Draw_DrawFormattedString(x, y,
                            offset == menus[menuMode].selected ? (menus[menuMode].editing ? COLOR_RED : COLOR_GREEN) : COLOR_WHITE,
                            "%.2x",
                            menus[menuMode].buf[offset]);
                        }
                        else
                            Draw_DrawString(x, y, COLOR_WHITE, "  ");
                    }
                }

                Draw_FlushFramebuffer();
                Draw_Unlock();
            }

            void clearMenu(void)
            {
                Draw_Lock();
                Draw_ClearFramebuffer();
                Draw_FlushFramebuffer();
                Draw_Unlock();
            }

            clearMenu();

            do
            {
                //handle scrolling
                for (u32 i = 0; i < menus[MENU_MODE_NORMAL].max; i++)
                {
                    if (menus[MENU_MODE_NORMAL].max <= ROWS_PER_SCREEN)
                        break;

                    u32 scroll = menus[MENU_MODE_NORMAL].starti;
                    u32 selectedRow = (menus[MENU_MODE_NORMAL].selected - (menus[MENU_MODE_NORMAL].selected % BYTES_PER_ROW))/BYTES_PER_ROW;

                    if (scroll > selectedRow)
                        scroll--;

                    if ((i <= selectedRow) && \
                       ((selectedRow - scroll) >= ROWS_PER_SCREEN) && \
                       (scroll != (menus[MENU_MODE_NORMAL].max - ROWS_PER_SCREEN)))
                        scroll++;

                    menus[MENU_MODE_NORMAL].starti = scroll;
                }

                drawMenu();

                u32 pressed = waitInputWithTimeout(1000);

                if(pressed & BUTTON_A)
                    menus[menuMode].editing = !menus[menuMode].editing;
                else if(pressed & BUTTON_X)
                {
                    if(menuMode == MENU_MODE_GOTO)
                    {
                        menuMode = MENU_MODE_NORMAL;
                        finishJumping();
                    }
                    else
                    {
                        menuMode = MENU_MODE_GOTO;
                    }
                }
                else if(pressed & BUTTON_Y)
                {
                    if(menuMode == MENU_MODE_SEARCH)
                    {
                        menuMode = MENU_MODE_NORMAL;
                        finishSearching();
                    }
                    else
                    {
                        menuMode = MENU_MODE_SEARCH;
                    }
                }

                if(menus[menuMode].editing)
                {
                    if(pressed & BUTTON_LEFT)
                    {
                        selectedByteDecrement();
                    }
                    else if(pressed & BUTTON_RIGHT)
                    {
                        selectedByteIncrement();
                    }
                    else if(pressed & BUTTON_UP)
                    {
                        selectedByteAdd0x10();
                    }
                    else if(pressed & BUTTON_DOWN)
                    {
                        selectedByteSub0x10();
                    }
                }
                else
                {
                    if(pressed & BUTTON_LEFT)
                    {
                        selectedMoveLeft();
                    }
                    else if(pressed & BUTTON_RIGHT)
                    {
                        selectedMoveRight();
                    }
                    else if(pressed & BUTTON_UP)
                    {
                        selectedMoveUp();
                    }
                    else if(pressed & BUTTON_DOWN)
                    {
                        selectedMoveDown();
                    }

                    else if(pressed & BUTTON_L1)
                    {
                        if(menuMode == MENU_MODE_NORMAL)
                            selectedMovePageUp();
                        else if(menuMode == MENU_MODE_SEARCH)
                            searchPatternReduce();
                    }
                    else if(pressed & BUTTON_R1)
                    {
                        if(menuMode == MENU_MODE_NORMAL)
                            selectedMovePageDown();
                        else if(menuMode == MENU_MODE_SEARCH)
                            searchPatternEnlarge();
                    }
                }

                if(pressed & BUTTON_B) // go back to the list
                    break;

                if(menus[menuMode].selected >= menus[menuMode].max)
                    menus[menuMode].selected = menus[menuMode].max - 1;
            }
            while(!terminationRequest);

            clearMenu();

            svcUnmapProcessMemoryEx(processHandle, destAddress, totalSize);
            svcCloseHandle(processHandle);
        }
    }
}

static inline void ProcessListMenu_HandleSelected(const ProcessInfo *info)
{
    if(!gdbServer.super.running || info->isZombie)
        ProcessListMenu_MemoryViewer(info);

    u32 id;
    for(id = 0; id < MAX_DEBUG && (!(gdbServer.ctxs[id].flags & GDB_FLAG_SELECTED) || gdbServer.ctxs[id].pid != info->pid); id++);

    GDBContext *ctx = &gdbServer.ctxs[id];

    if(id < MAX_DEBUG)
    {
        if(ctx->flags & GDB_FLAG_USED)
        {
            RecursiveLock_Lock(&ctx->lock);
            ctx->super.should_close = true;
            RecursiveLock_Unlock(&ctx->lock);

            while(ctx->super.should_close)
                svcSleepThread(12 * 1000 * 1000LL);
        }
        else
        {
            RecursiveLock_Lock(&ctx->lock);
            ctx->flags &= ~GDB_FLAG_SELECTED;
            RecursiveLock_Unlock(&ctx->lock);
        }
    }
    else
    {
        for(id = 0; id < MAX_DEBUG && gdbServer.ctxs[id].flags & GDB_FLAG_SELECTED; id++);
        if(id < MAX_DEBUG)
        {
            ctx = &gdbServer.ctxs[id];
            RecursiveLock_Lock(&ctx->lock);
            ctx->pid = info->pid;
            ctx->flags |= GDB_FLAG_SELECTED;
            RecursiveLock_Unlock(&ctx->lock);
        }
    }
}

s32 ProcessListMenu_FetchInfo(void)
{
    u32 pidList[0x40];
    s32 processAmount;

    svcGetProcessList(&processAmount, pidList, 0x40);

    for(s32 i = 0; i < processAmount; i++)
    {
        Handle processHandle;
        Result res = svcOpenProcess(&processHandle, pidList[i]);
        if(R_FAILED(res))
            continue;

        infos[i].pid = pidList[i];
        svcGetProcessInfo((s64 *)&infos[i].name, processHandle, 0x10000);
        svcGetProcessInfo((s64 *)&infos[i].titleId, processHandle, 0x10001);
        infos[i].isZombie = svcWaitSynchronization(processHandle, 0) == 0;
        svcCloseHandle(processHandle);
    }

    return processAmount;
}

void RosalinaMenu_ProcessList(void)
{
    s32 processAmount = ProcessListMenu_FetchInfo();
    s32 selected = 0, page = 0, pagePrev = 0;
    nfds_t nfdsPrev;

    do
    {
        nfdsPrev = gdbServer.super.nfds;
        memcpy(infosPrev, infos, sizeof(infos));

        Draw_Lock();
        if(page != pagePrev)
            Draw_ClearFramebuffer();
        Draw_DrawString(10, 10, COLOR_TITLE, "Process list");

        if(gdbServer.super.running)
        {
            char ipBuffer[17];
            u32 ip = gethostid();
            u8 *addr = (u8 *)&ip;
            int n = sprintf(ipBuffer, "%hhu.%hhu.%hhu.%hhu", addr[0], addr[1], addr[2], addr[3]);
            Draw_DrawString(SCREEN_BOT_WIDTH - 10 - SPACING_X * n, 10, COLOR_WHITE, ipBuffer);
        }


        for(s32 i = 0; i < PROCESSES_PER_MENU_PAGE && page * PROCESSES_PER_MENU_PAGE + i < processAmount; i++)
        {
            char buf[65] = {0};
            ProcessListMenu_FormatInfoLine(buf, &infos[page * PROCESSES_PER_MENU_PAGE + i]);

            Draw_DrawString(30, 30 + i * SPACING_Y, COLOR_WHITE, buf);
            Draw_DrawCharacter(10, 30 + i * SPACING_Y, COLOR_TITLE, page * PROCESSES_PER_MENU_PAGE + i == selected ? '>' : ' ');
        }

        Draw_FlushFramebuffer();
        Draw_Unlock();

        if(terminationRequest)
            break;

        u32 pressed;
        do
        {
            pressed = waitInputWithTimeout(50);
            if(pressed != 0 || nfdsPrev != gdbServer.super.nfds)
                break;
            processAmount = ProcessListMenu_FetchInfo();
            if(memcmp(infos, infosPrev, sizeof(infos)) != 0)
                break;
        }
        while(pressed == 0 && !terminationRequest);

        if(pressed & BUTTON_B)
            break;
        else if(pressed & BUTTON_A)
            ProcessListMenu_HandleSelected(&infos[selected]);
        else if(pressed & BUTTON_DOWN)
            selected++;
        else if(pressed & BUTTON_UP)
            selected--;
        else if(pressed & BUTTON_LEFT)
            selected -= PROCESSES_PER_MENU_PAGE;
        else if(pressed & BUTTON_RIGHT)
        {
            if(selected + PROCESSES_PER_MENU_PAGE < processAmount)
                selected += PROCESSES_PER_MENU_PAGE;
            else if((processAmount - 1) / PROCESSES_PER_MENU_PAGE == page)
                selected %= PROCESSES_PER_MENU_PAGE;
            else
                selected = processAmount - 1;
        }

        if(selected < 0)
            selected = processAmount - 1;
        else if(selected >= processAmount)
            selected = 0;

        pagePrev = page;
        page = selected / PROCESSES_PER_MENU_PAGE;
    }
    while(!terminationRequest);
}
