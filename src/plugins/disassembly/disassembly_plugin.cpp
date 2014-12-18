#include <pd_view.h>
#include <pd_backend.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct Line
{
    const char* text;
    bool breakpoint;        // TODO: Flags
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct DissassemblyData
{
    char* code;
    Line* lines;
    int lineCount;
    uint64_t pc;
    uint64_t location;
    uint8_t locationSize;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void splitIntoLines(DissassemblyData* data, char* code, size_t size)
{
    int lineCount = 0;
    Line* lines;

    char* target = code;
    char* targetEnd = code + size;

    free(data->lines);

    // so this is really waste of memory but will do for now

    data->lines = lines = (Line*)malloc(sizeof(Line) * size);
    memset(lines, 0, sizeof(Line) * size);

    lines[0].text = target;
    lineCount++;

    while (target < targetEnd)
    {
        char c = *target;

        if (*target == '\r')
        {
            *target++ = 0;
            c = *target;
        }

        if (*target == '\n')
        {
            *target = 0;
            lines[lineCount++].text = target + 1;
        }

        target++;
    }

    *target++ = 0;
    data->lineCount = lineCount;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* createInstance(PDUI* uiFuncs, ServiceFunc* serviceFunc)
{
    (void)serviceFunc;
    DissassemblyData* userData = (DissassemblyData*)malloc(sizeof(DissassemblyData));
    memset(userData, 0, sizeof(DissassemblyData));

    (void)uiFuncs;
    (void)serviceFunc;

    return userData;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void destroyInstance(void* userData)
{
    free(userData);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void setDisassemblyCode(DissassemblyData* data, PDReader* inEvents)
{
    const char* stringBuffer;

    PDRead_findString(inEvents, &stringBuffer, "string_buffer", 0);

    //printf("Got disassembly\n");
    //printf("disassembly %s\n", stringBuffer);

    free(data->code);

    if (!stringBuffer)
        return;

    data->code = strdup(stringBuffer);

    splitIntoLines(data, data->code, strlen(stringBuffer));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void renderUI(DissassemblyData* data, PDUI* uiFuncs)
{
    if (!data->code)
        return;

    uiFuncs->text("");  // TODO: Temporary

    char tempBuffer[64];
    sprintf(tempBuffer, "%04X", (uint16_t)data->pc);

    for (int i = 0; i < data->lineCount; ++i)
    {
        if (strstr(data->lines[i].text, tempBuffer))
        {
            PDRect rect;
            PDVec2 pos = uiFuncs->getCursorPos();
            rect.x = pos.x;
            rect.y = pos.y - 11;
            rect.width = -1;
            rect.height = 14;
            uiFuncs->fillRect(rect, PD_COLOR_32(200, 0, 0, 127));
        }

        uiFuncs->text(data->lines[i].text);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void updateRegisters(DissassemblyData* data, PDReader* reader)
{
    PDReaderIterator it;

    if (PDRead_findArray(reader, &it, "registers", 0) == PDReadStatus_notFound)
        return;

    while (PDRead_getNextEntry(reader, &it))
    {
        const char* name = "";

        PDRead_findString(reader, &name, "name", it);

        if (!strcmp(name, "pc"))
        {
            PDRead_findU64(reader, &data->pc, "register", it);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int update(void* userData, PDUI* uiFuncs, PDReader* inEvents, PDWriter* writer)
{
    uint32_t event;

    DissassemblyData* data = (DissassemblyData*)userData;

    while ((event = PDRead_getEvent(inEvents)) != 0)
    {
        switch (event)
        {
            case PDEventType_setDisassembly:
            {
                setDisassemblyCode(data, inEvents);
                break;
            }

            case PDEventType_setExceptionLocation:
            {
                PDRead_findU64(inEvents, &data->location, "address", 0);
                PDRead_findU8(inEvents, &data->locationSize, "address_size", 0);
                break;
            }

            case PDEventType_setRegisters:
                updateRegisters(data, inEvents); break;

        }
    }

    renderUI(data, uiFuncs);

    // Temporary req

    PDWrite_eventBegin(writer, PDEventType_getDisassembly);
    PDWrite_u64(writer, "address_start", 0);
    PDWrite_u32(writer, "instruction_count", (uint32_t)10);
    PDWrite_eventEnd(writer);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static PDViewPlugin plugin =
{
    "Disassembly",
    createInstance,
    destroyInstance,
    update,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C"
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    PD_EXPORT void InitPlugin(RegisterPlugin* registerPlugin, void* privateData)
    {
        registerPlugin(PD_VIEW_API_VERSION, &plugin, privateData);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

}

