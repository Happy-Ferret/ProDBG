#include "plugin_handler.h"
#include "log.h"
#include "core.h"
#include "core/alloc.h"
#include <pd_common.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <foundation/fs.h>
#include <foundation/library.h>
#include <foundation/array.h>


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TODO: Move this to some general configuration about plugins and types

static const char* s_pluginTypes[] =
{
	"ProDBG View",
	"ProDBG Backend",
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

enum
{
	PRODBG_VIEW_PLUGIN,
	PRODBG_BACKEND_PLUGIN,
	PRODBG_PLUGIN_COUNT,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static PluginData** s_plugins[PRODBG_PLUGIN_COUNT];
static const char** s_searchPaths;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PluginHandler_addSearchPath(const char* path)
{
	array_push(s_searchPaths, path);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static PluginData* findPlugin(PluginData** plugins, const char* pluginFile, const char* pluginName)
{
    int count = array_size(plugins);

    for (int i = 0; i < count; ++i)
    {
        PluginData* pluginData = plugins[i];
        PDPluginBase* base = (PDPluginBase*)pluginData->plugin;

        if (!strcmp(base->name, pluginName) && !strcmp(pluginData->filename, pluginFile))
            return pluginData;
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static PluginData* findPluginAll(const char* pluginFile, const char* pluginName)
{
	PluginData* plugin = 0;

	for (int i = 0; i < PRODBG_PLUGIN_COUNT; ++i)
	{
		if ((plugin = findPlugin(s_plugins[i], pluginFile, pluginName)))
			return plugin;
	}

	return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct PluginPrivateData
{
	const char* name;
	object_t lib;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void registerPlugin(const char* type, void* plugin, void* privateData)
{
	PluginPrivateData* privData = (PluginPrivateData*)privateData;

	const char* filename = privData->name; 

    for (int i = 0; i < PRODBG_PLUGIN_COUNT; ++i)
	{
		if (strstr(type, s_pluginTypes[i]))
		{
			if (findPlugin(s_plugins[i], filename, ((PDPluginBase*)plugin)->name))
				return;

			// TODO: Verify that we don't add a plugin with the same plugin name in the same plugin

    		PluginData* pluginData = (PluginData*)alloc_zero(sizeof(PluginData));
			pluginData->plugin = plugin;
			pluginData->type = type;
			pluginData->filename = filename;
			pluginData->lib = privData->lib; 

			return (void)array_push(s_plugins[i], pluginData);
		}
	}

	pd_error("Unknown pluginType %s - %s", type, ((PDPluginBase*)plugin)->name);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static char* buildLoadingPath(const char* basePath, const char* plugin)
{
    char* output = 0;

    size_t baseLen = strlen(basePath);
    size_t pluginLen = strlen(plugin);

#ifdef PRODBG_MAC
    output = (char*)malloc(baseLen + pluginLen + 12); // + 12 for separator /lib.dylib + terminator
    sprintf(output, "%s/lib%s.dylib", basePath, plugin);
#elif PRODBG_WIN
    output = (char*)malloc(baseLen + pluginLen + 6); // + 5 for separator /.dll + terminator
    sprintf(output, "%s\\%s.dll", basePath, plugin);
#else
    output = (char*)malloc(baseLen + pluginLen + 12); // + 4 for separator \.so + terminator
    sprintf(output, "%s/lib%s.so", basePath, plugin);
#endif

    return output;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool PluginHandler_addPlugin(const char* basePath, const char* plugin)
{
    void* (*initPlugin)(RegisterPlugin* registerPlugin, void* privateData);
	struct PluginPrivateData data;
    object_t lib = 0; 

    const char* filename = 0; 
    void* function;

    if (!basePath || !plugin)
        goto error;

    filename = buildLoadingPath(basePath, plugin);

	lib = library_load(filename);

    if (!library_valid(lib))
    {
        // TODO: Show error message
        pd_error("Unable to open %s\n", filename);
        goto error;
    }

	if (!(function = library_symbol(lib, "InitPlugin")))
    {
        // TODO: Show error message
        pd_error("Unable to find InitPlugin function in plugin %s\n", filename);
        goto error;
    }

    *(void**)(&initPlugin) = function;

	data.name = plugin;
	data.lib = lib; 

    initPlugin(registerPlugin, (void*)&data);

	free((void*)filename);

    return true;

error:

	free((void*)filename);

	if (library_valid(lib))
		library_unload(lib);

	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PluginHandler_unloadAllPlugins()
{
    // TODO: Actually unload everything

	for (int i = 0; i < PRODBG_PLUGIN_COUNT; ++i)
	{
    	int count = array_size(s_plugins[i]);

	    for (int t = 0; t < count; ++t)
	    {
	    	PluginData* plugin = s_plugins[i][t];
	    	library_unload(plugin->lib);
        	//free((void*)plugin->filename);
        	free(plugin);
 		}

    	array_clear(s_plugins[i]);
   	}

	//for (int i = 0; i < PRODBG_PLUGIN_COUNT; ++i)
	//	free(s_plugins[i]);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

PluginData* PluginHandler_findPlugin(const char** paths, const char* pluginFile, const char* pluginName, bool load)
{
    PluginData* pluginData;

    // TODO: Support paths
    (void)paths;

    // If not found and not !load (that is we will not try to load it)

    if ((pluginData = findPluginAll(pluginFile, pluginName)))
        return pluginData;

    if (!load)
        return 0;

    // TODO: Support base paths

    if (!PluginHandler_addPlugin(OBJECT_DIR, pluginFile))
        return 0;

    if ((pluginData = findPluginAll(pluginFile, pluginName)))
        return pluginData;

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

PluginData** PluginHandler_getBackendPlugins(int* count)
{
    *count = array_size(s_plugins[PRODBG_BACKEND_PLUGIN]);
    return s_plugins[PRODBG_BACKEND_PLUGIN];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

PluginData** PluginHandler_getViewPlugins(int* count)
{
    *count = array_size(s_plugins[PRODBG_VIEW_PLUGIN]);
    return s_plugins[PRODBG_VIEW_PLUGIN];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static PluginData* getPluginData(PluginData** plugins, void* plugin)
{
    int count = array_size(plugins);

    for (int i = 0; i < count; ++i)
    {
        if (plugins[i]->plugin == plugin)
            return plugins[i];
    }

	return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

PluginData* PluginHandler_getPluginData(void* plugin)
{
	PluginData* data = 0;

	for (int i = 0; i < PRODBG_PLUGIN_COUNT; ++i)
	{
		if ((data = getPluginData(s_plugins[i], plugin)))
			return data;
	}

    return 0;
}


