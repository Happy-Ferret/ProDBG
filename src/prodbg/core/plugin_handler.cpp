#include "plugin_handler.h"
#include "io/shared_object.h"
#include "log.h"
#include "core.h"
#include <pd_common.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

namespace prodbg
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static Plugin s_plugins[128];
static unsigned int s_pluginCount = 0;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void registerPlugin(const char* type, void* data)
{
    Plugin plugin;

    plugin.data = data;
    plugin.type = type;

    log_debug("Register plugin (type %s data %p)\n", type, data);

    assert(s_pluginCount < sizeof_array(s_plugins));

    s_plugins[s_pluginCount++] = plugin;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool PluginHandler_addPlugin(const char* basePath, const char* plugin)
{
    Handle handle;
    void* (*initPlugin)(int version, ServiceFunc* serviceFunc, RegisterPlugin* registerPlugin);

    if (!(handle = SharedObject_open(basePath, plugin)))
        return false;

    void* function = SharedObject_getSym(handle, "InitPlugin");

    if (!function)
    {
        log_error("Unable to find InitPlugin function in plugin %s\n", plugin);
        SharedObject_close(handle);
        return false;
    }

    *(void**)(&initPlugin) = function;

    initPlugin(1, 0, registerPlugin);

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Plugin* PluginHandler_getPlugins(int* count)
{
    *count = (int)s_pluginCount;
    return &s_plugins[0];
}

}

