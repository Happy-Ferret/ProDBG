#include "menu.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

MenuDescriptor g_fileMenu[] =
{
    { "Open and run executable...", PRODBG_MENU_FILE_OPEN_AND_RUN_EXE, 'd', PRODBG_KEY_COMMAND, PRODBG_KEY_CTRL },
    { "Open source...", PRODBG_MENU_FILE_OPEN_SOURCE, 'o', PRODBG_KEY_COMMAND, PRODBG_KEY_CTRL },
    { 0, 0, 0, 0, 0 },
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

MenuDescriptor g_debugMenu[] =
{
    { "Toggle Breakpoint", PRODBG_MENU_DEBUG_TOGGLE_BREAKPOINT, PRODBG_KEY_F9, 0, 0 },
    { "Step Over", PRODBG_MENU_DEBUG_STEP_OVER, PRODBG_KEY_F10, 0, 0 },
    { "Step Out", PRODBG_MENU_DEBUG_STEP_OUT, PRODBG_KEY_F11, 0, 0 },
    { 0, 0, 0, 0, 0 },
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

