#include "pd_backend.h"
#include "pd_menu.h"
#include "c64_vice_connection.h"
#include <stdlib.h>

#ifndef _WIN32
#include <unistd.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <stdbool.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static char s_recvBuffer[512 * 1024];

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

enum
{
    C64_VICE_MENU_ATTACH_TO_VICE,
    C64_VICE_MENU_DETACH_FROM_VICE,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct Regs6510
{
    uint16_t pc;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t sp;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void sleepMs(int ms)
{
#ifdef _MSC_VER
    Sleep(ms);
#else
    usleep((unsigned int)(ms * 1000));
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct PluginData
{
    struct VICEConnection* conn;
	struct Regs6510 regs;
	bool hasUpdatedRegistes;
	bool hasUpdatedExceptionLocation;
    PDDebugState state;
} PluginData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void connectToLocalHost(PluginData* data)
{
    struct VICEConnection* conn = 0;

    // Kill the current connection if we have one

    if (data->conn)
    {
        VICEConnection_destroy(data->conn);
        data->conn = 0;
    }

    conn = VICEConnection_create(VICEConnectionType_Connect, 6510);

    if (!VICEConnection_connect(conn, "localhost", 6510))
    {
        VICEConnection_destroy(conn);

        data->conn = 0;
        data->state = PDDebugState_noTarget;

        return;
    }

    data->conn = conn;
    data->state = PDDebugState_running;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* createInstance(ServiceFunc* serviceFunc)
{
    (void)serviceFunc;

    PluginData* data = malloc(sizeof(PluginData));
    memset(data, 0, sizeof(PluginData));

    data->state = PDDebugState_noTarget;

    connectToLocalHost(data);

    return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void destroyInstance(void* userData)
{
    PluginData* plugin = (PluginData*)userData;

    if (plugin->conn)
        VICEConnection_destroy(plugin->conn);

    free(plugin);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void onMenu(PluginData* data, PDReader* reader)
{
    uint32_t menuId;

    PDRead_findU32(reader, &menuId, "menu_id", 0);

    switch (menuId)
    {
        case C64_VICE_MENU_ATTACH_TO_VICE:
        {
            connectToLocalHost(data);
            break;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void sendCommand(PluginData* data, const char* command)
{
    int len = (int)strlen(command);

	if (!data->conn)
		return;

	printf("send command %s\n", command);

    VICEConnection_send(data->conn, command, len, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int getData(PluginData* data, char** resBuffer, int* len)
{
    const int maxTry = 100;
    int res = 0;

	if (!data->conn)
		return 0;

	char* resData = (char*)&s_recvBuffer;
	int lenCount = 0;

    for (int i = 0; i < maxTry; ++i)
    {
		bool gotData = false;

        while (VICEConnection_pollRead(data->conn))
        {
            res = VICEConnection_recv(data->conn, resData, ((int)sizeof(s_recvBuffer)) - lenCount, 0);

            if (res == 0)
            	break;

			gotData = true;

			resData += res;
			lenCount += res;

			sleepMs(1);
        }

        if (gotData)
		{
            *len = lenCount;
            *resBuffer = (char*)&s_recvBuffer;

            return 1;
		}

        // Got some data so read it back

        sleepMs(1);
    }

    // got no data

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void writeRegister(PDWriter* writer, const char* name, uint8_t size, uint16_t reg, uint8_t readOnly)
{
    PDWrite_arrayEntryBegin(writer);
    PDWrite_string(writer, "name", name);
    PDWrite_u8(writer, "size", size);

    if (readOnly)
        PDWrite_u8(writer, "read_only", 1);

    if (size == 2)
        PDWrite_u16(writer, "register", reg);
    else
        PDWrite_u8(writer, "register", (uint8_t)reg);

    PDWrite_arrayEntryEnd(writer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void parseRegisters(struct Regs6510* regs, char* str)
{
    // Format from VICE looks like this:
    // (C:$e5cf)   ADDR AC XR YR SP 00 01 NV-BDIZC LIN CYC  STOPWATCH
    //           .;e5cf 00 00 0a f3 2f 37 00100010 000 001    3400489
    //

    const char* pch = strtok(str, " \t\n");

    // Skip the layout (hard-coded and ugly but as registers are fixed this should be fine
    // but needs to be fixed if VICE changes the layout

    for (int i = 0; i < 12; ++i)
        pch = strtok(0, " \t\n");

    regs->pc = (uint16_t)strtol(&pch[2], 0, 16); pch = strtok(0, " \t");
    regs->a = (uint8_t)strtol(pch, 0, 16); pch = strtok(0, " \t");
    regs->x = (uint8_t)strtol(pch, 0, 16); pch = strtok(0, " \t");
    regs->y = (uint8_t)strtol(pch, 0, 16); pch = strtok(0, " \t");
    regs->sp = (uint8_t)strtol(pch, 0, 16); pch = strtok(0, " \t");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void getRegisters(PluginData* data)
{
    char* res = 0;
    int len = 0;

    sendCommand(data, "registers\n");

    if (!getData(data, &res, &len))
        return;

    data->state = PDDebugState_stopException;

    parseRegisters(&data->regs, res);

    data->hasUpdatedRegistes = true;
    data->hasUpdatedExceptionLocation = true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void getDisassembly(PluginData* data, PDReader* reader, PDWriter* writer)
{
    char* res = 0;
    int len = 0;

	char gitDisAsmCommand[512];

	uint64_t addressStart = 0;
	uint32_t instructionCount = 0; 	

    PDRead_findU64(reader, &addressStart, "address_start", 0);
    PDRead_findU32(reader, &instructionCount, "instruction_count", 0);

	// assume that one instruction is 3 bytes which is high but that gives us more data back than we need which is
	// better than too little

	sprintf(gitDisAsmCommand, "disass $%04x $%04x\n", (uint16_t)addressStart, (uint16_t)(addressStart + instructionCount * 3));

    sendCommand(data, gitDisAsmCommand);

    if (!getData(data, &res, &len))
        return;

    // parse the buffer

	char* pch = strtok(res, "\n");

	PDWrite_eventBegin(writer, PDEventType_setRegisters);
	PDWrite_arrayBegin(writer, "registers");

	while (pch)
	{
		// expected format of each line:
		// .C:080e  A9 22       LDA #$22

		if (pch[0] != '.')
			break;

		uint16_t address = (uint16_t)strtol(&pch[3], 0, 16);

		PDWrite_arrayEntryBegin(writer);
		PDWrite_u16(writer, "address", address);
		PDWrite_string(writer, "line", &pch[9]);

		PDWrite_arrayEntryEnd(writer);

		pch = strtok(0, "\n");
	}

	PDWrite_arrayEnd(writer);
	PDWrite_eventEnd(writer);

    printf("dis data\n");
    printf("%s\n", res);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void processEvents(PluginData* data, PDReader* reader, PDWriter* writer)
{
    uint32_t event;

    while ((event = PDRead_getEvent(reader)))
    {
        switch (event)
        {
            //case PDEventType_getExceptionLocation : setExceptionLocation(plugin, writer); break;
            //case PDEventType_getCallstack : setCallstack(plugin, writer); break;

			case PDEventType_getRegisters:
			{
                if (!data->hasUpdatedRegistes)
                    getRegisters(data);

                break;
			}

			case PDEventType_getDisassembly:
			{
				getDisassembly(data, reader, writer);
				break;
			}

            case PDEventType_menuEvent:
			{
                onMenu(data, reader);
                break;
			}
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint16_t findRegisterInString(const char* str, const char* needle)
{
	const char* offset = strstr(str, needle);

	size_t needleLength = strlen(needle);

	if (!offset)
	{
		printf("findRegisterInString: Unable to find %s in %s\n", needle, str);
		return 0;
	}

	offset += needleLength;

	return (uint16_t)strtol(offset, 0, 16);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void onStep(PluginData* plugin)
{
    char* res = 0;
    int len = 0;

    sendCommand(plugin, "n\n");

    if (!getData(plugin, &res, &len))
        return;

    // return data from VICE is of the follwing format:
    // .C:0811  EE 20 D0    INC $D020      - A:00 X:17 Y:17 SP:f6 ..-.....   19262882

    plugin->regs.pc = (uint16_t)strtol(&res[3], 0, 16);
    plugin->regs.a = (uint8_t)findRegisterInString(res, "A:");
    plugin->regs.x = (uint8_t)findRegisterInString(res, "X:");
    plugin->regs.y = (uint8_t)findRegisterInString(res, "Y:");
    plugin->regs.sp = (uint8_t)findRegisterInString(res, "SP:");

    plugin->hasUpdatedRegistes = true;
    plugin->hasUpdatedExceptionLocation = true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void onAction(PluginData* plugin, PDAction action)
{
	switch (action)
	{
		case PDAction_none:
			break;
		
		case PDAction_stop:
		{
			sendCommand(plugin, "break\n");
			break;
		}

		case PDAction_break:
		{
			sendCommand(plugin, "break\n");
			break;
		}

		case PDAction_run:
		{
			if (plugin->state != PDDebugState_running)
				sendCommand(plugin, "ret\n");

			break;
		}

		case PDAction_step:
		{
			onStep(plugin);
			break;
		}

		case PDAction_stepOver:
		case PDAction_stepOut:
		case PDAction_custom:
		{
			break;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static PDDebugState update(void* userData, PDAction action, PDReader* reader, PDWriter* writer)
{
    PluginData* plugin = (PluginData*)userData;

    plugin->hasUpdatedRegistes = false;
    plugin->hasUpdatedExceptionLocation = false;

	onAction(plugin, action);

    processEvents(plugin, reader, writer);

    if (plugin->hasUpdatedRegistes)
	{
		PDWrite_eventBegin(writer, PDEventType_setRegisters);
		PDWrite_arrayBegin(writer, "registers");

		writeRegister(writer, "pc", 2, plugin->regs.pc, 1);
		writeRegister(writer, "sp", 1, plugin->regs.sp, 0);
		writeRegister(writer, "a", 1, plugin->regs.a, 0);
		writeRegister(writer, "x", 1, plugin->regs.x, 0);
		writeRegister(writer, "y", 1, plugin->regs.y, 0);

		PDWrite_arrayEnd(writer);
		PDWrite_eventEnd(writer);
	}

	if (plugin->hasUpdatedExceptionLocation)
	{
		PDWrite_eventBegin(writer,PDEventType_setExceptionLocation); 
		PDWrite_u16(writer, "address", plugin->regs.pc);
		PDWrite_u8(writer, "address_size", 2);
		PDWrite_eventEnd(writer);
	}

    return plugin->state;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static PDMenuItem s_menu0[] =
{
    { "Attach to VICE", C64_VICE_MENU_ATTACH_TO_VICE, 0, 0, 0 },
    { "Detach from VICE", C64_VICE_MENU_DETACH_FROM_VICE, 0, 0, 0 },
    { 0, 0, 0, 0, 0 },
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static PDMenu s_menus[] =
{
    { "C64 VICE", (PDMenuItem*)&s_menu0 },
    { 0, 0 },
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static PDMenu* createMenu()
{
    return (PDMenu*)&s_menus;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static PDBackendPlugin plugin =
{
    "C64 VICE Debugger",
    createInstance,
    destroyInstance,
    createMenu,
    update,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

PD_EXPORT void InitPlugin(RegisterPlugin* registerPlugin, void* privateData)
{
    registerPlugin(PD_BACKEND_API_VERSION, &plugin, privateData);
}


