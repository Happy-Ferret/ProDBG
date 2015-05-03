#include <pd_view.h>
#include <pd_backend.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <scintilla/include/Scintilla.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct SourceCodeData
{
	char filename[4096];
	int line;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* readFileFromDisk(const char* file, size_t* size)
{
    FILE* f = fopen(file, "rb");
    uint8_t* data = 0;
    size_t s = 0, t = 0;

    *size = 0;

    if (!f)
        return 0;

    // TODO: Use fstat here?

    fseek(f, 0, SEEK_END);
    long ts = ftell(f);

    if (ts < 0)
    	goto end;

    s = (size_t)ts;

    data = (uint8_t*)malloc(s + 16);

    if (!data)
    	goto end;

    fseek(f, 0, SEEK_SET);

    t = fread(data, s, 1, f);
    (void)t;

    data[s] = 0;

    *size = s;

end:

    fclose(f);

    return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* createInstance(PDUI* uiFuncs, ServiceFunc* serviceFunc)
{
    (void)serviceFunc;
    (void)uiFuncs;
    SourceCodeData* userData = (SourceCodeData*)malloc(sizeof(SourceCodeData));
    memset(userData, 0, sizeof(SourceCodeData));

    // TODO: Temp testing code

    //parseFile(&userData->file, "examples/crashing_native/crash2.c");

    return userData;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void destroyInstance(void* userData)
{
    free(userData);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void setExceptionLocation(PDSCInterface* sourceFuncs, SourceCodeData* data, PDReader* inEvents)
{
    const char* filename;
    uint32_t line;

    // TODO: How to show this? Tell user to switch to disassembly view?

    if (PDRead_findString(inEvents, &filename, "filename", 0) == PDReadStatus_notFound)
        return;

    if (PDRead_findU32(inEvents, &line, "line", 0) == PDReadStatus_notFound)
        return;

    if (strcmp(filename, data->filename))
	{
		size_t size = 0;
		void* fileData = readFileFromDisk(filename, &size);

		if (fileData)
			PDUI_SCSendCommand(sourceFuncs, SCI_ADDTEXT, size, (intptr_t)fileData);
		else
			printf("Sourcecode_plugin: Unable to load %s\n", filename);

		free(fileData);

		strncpy(data->filename, filename, sizeof(data->filename));
		data->filename[sizeof(data->filename) - 1] = 0;
	}

	PDUI_SCSendCommand(sourceFuncs, SCI_GOTOLINE, (uintptr_t)line, 0);

	data->line = (int)line;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
static void showInUI(SourceCodeData* data, PDUI* uiFuncs)
{
    (void)data;
    //uiFuncs->columns(1, "sourceview", true);
    PDSCInterface* scFuncs = uiFuncs->scInputText("test", 800, 700, 0, 0);

	const char* testText = "Test\nTest2\nTest3\n\0";

	static bool hasSentText = false;

	if (!hasSentText)
	{
		PDUI_SCSendCommand(scFuncs, SCI_ADDTEXT, strlen(testText), (intptr_t)testText);
		hasSentText = true;
	}
}
*/

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void updateKeyboard(SourceCodeData* data, PDUI* uiFuncs)
{
    (void)data;
    (void)uiFuncs;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void toggleBreakpointCurrentLine(SourceCodeData* data, PDWriter* writer)
{
    (void)data;
    (void)writer;
    /*
       // TODO: Currenty we don't handly if we set breakpoints on a line we can't

       PDWrite_eventBegin(writer, PDEventType_setBreakpoint);
       PDWrite_string(writer, "filename", data->file.filename);
       PDWrite_u32(writer, "line", (unsigned int)data->cursorPos + 1);
       PDWrite_eventEnd(writer);

       data->file.lines[data->cursorPos].breakpoint = !data->file.lines[data->cursorPos].breakpoint;
     */
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int update(void* userData, PDUI* uiFuncs, PDReader* inEvents, PDWriter* writer)
{
    uint32_t event;

    (void)uiFuncs;

    SourceCodeData* data = (SourceCodeData*)userData;
    PDSCInterface* sourceFuncs = uiFuncs->scInputText("test", 800, 700, 0, 0);

    while ((event = PDRead_getEvent(inEvents)) != 0)
    {
        switch (event)
        {
            case PDEventType_setExceptionLocation:
            {
                setExceptionLocation(sourceFuncs, data, inEvents);
                break;
            }

            case PDEventType_toggleBreakpointCurrentLine:
            {
                toggleBreakpointCurrentLine(data, writer);
                break;
            }
        }
    }

    updateKeyboard(data, uiFuncs);

	PDUI_SCUpdate(sourceFuncs);
	PDUI_SCDraw(sourceFuncs);

    //showInUI(data, uiFuncs);

    PDWrite_eventBegin(writer, PDEventType_getExceptionLocation);
    PDWrite_eventEnd(writer);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static PDViewPlugin plugin =
{
    "Source Code View",
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

