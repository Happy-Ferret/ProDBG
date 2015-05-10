#pragma once

#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct PluginData
{
    void* plugin;
    const char* type;
    const char* filename;
    uint64_t lib;
    int count;
    int menuStart;
    int menuEnd;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PluginHandler_create(bool useShadowDir);
void PluginHandler_destroy(); 

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Adds a path for that is used for plugin loading. This path is first base first load priority meaning that the
// path that gets added first has the highest priority

void PluginHandler_addSearchPath(const char* path);

bool PluginHandler_addPlugin(const char* basePath, const char* plugin);
void PluginHandler_unloadAllPlugins();

bool PluginHandler_unloadPlugin(PluginData* plugin);

PluginData* PluginHandler_findPlugin(const char** paths, const char* pluginFile, const char* pluginName, bool load);

PluginData* PluginHandler_getViewPluginData(void* plugin);
PluginData** PluginHandler_getViewPlugins(int* count);
PluginData** PluginHandler_getBackendPlugins(int* count);
PluginData* PluginHandler_getPluginData(void* plugin);


