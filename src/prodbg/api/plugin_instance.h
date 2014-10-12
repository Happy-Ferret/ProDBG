#pragma once

#include "api/include/pd_ui.h" 

struct PDReader;
struct PDWriter;
struct PDViewPlugin;
struct PDBackendPlugin;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct PDBackendInstance
{
    struct PDBackendPlugin* plugin;
    void* userData;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct ViewPluginInstance
{
    PDUI ui;
    PDViewPlugin* plugin;
    void* userData;
    bool markDeleted;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ViewPluginInstance* PluginInstance_createViewPlugin(PDViewPlugin* plugin);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

