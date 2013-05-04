#include "PluginHandler.h"
#include <core/io/SharedObject.h> 
#include <ProDBGAPI.h>
#include <vector>

namespace prodbg
{

static std::vector<Plugin> s_plugins;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void registerPlugin(int type, void* data)
{
	Plugin plugin;

	plugin.data = data;
	plugin.type = (ProDBGPluginType)type;

	printf("Register plugin (type %d data %p)\n", type, data);

	s_plugins.push_back(plugin);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool PluginHandler_addPlugin(const char* plugin)
{
	Handle handle;
	void* (*initPlugin)(int version, ServiceFunc* serviceFunc, RegisterPlugin* registerPlugin);

	printf("Trying to load %s\n", plugin);

	if (!(handle = SharedObject_open(plugin)))
	{
		// TODO: Implment proper logging and output

		printf("Unable to open plugin %s\n", plugin);
		return false;
	}

	void* function = SharedObject_getSym(handle, "InitPlugin");

	if (!function)
	{
		printf("Unable to find InitPlugin function in plugin %s\n", plugin);
		SharedObject_close(handle);
		return false;
	}
		
	*(void **)(&initPlugin) = function;

	initPlugin(1, 0, registerPlugin);

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Plugin* PluginHandler_getPlugins(int* count)
{
	*count = (int)s_plugins.size();
	return &s_plugins[0];
}

}

