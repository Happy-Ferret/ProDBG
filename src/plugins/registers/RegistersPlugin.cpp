#include <PDView.h>
#include <stdlib.h>
#include <stdio.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct RegistersData
{
	int temp;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* createInstance(ServiceFunc* serviceFunc)
{
	(void)serviceFunc;
	RegistersData* userData = (RegistersData*)malloc(sizeof(RegistersData));
	return userData;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void destroyInstance(void* userData)
{
	free(userData);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int update(void* userData, PDReader* inEvents, PDWriter* outEvents)
{
	(void)userData;
	(void)inEvents;
	(void)outEvents;

	return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static PDViewPlugin plugin =
{
    0,    // version
    "Registers",
    createInstance,
    destroyInstance,
    update,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C"
{

PD_EXPORT void InitPlugin(int version, ServiceFunc* serviceFunc, RegisterPlugin* registerPlugin)
{
    registerPlugin(PD_VIEW_API_VERSION, &plugin);
}

}

