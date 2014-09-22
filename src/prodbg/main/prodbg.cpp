#include <stdlib.h>
#include <core/Core.h>
#include <core/PluginHandler.h>
//#include "ui/Application.h"

#ifdef PRODBG_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <bgfx.h>
#include <entry.h>
#include "ui/imgui/imgui.h"
#include "ui/IMGUISetup.h"
#include <stdio.h>

namespace prodbg
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const char* s_plugins[] =
{
	"SourceCodePlugin", 
	"CallStackPlugin", 
	"Registers", 
	"Locals", 
	"Disassembly", 
#ifdef PRODBG_MAC
	"LLDBPlugin",
#endif
};


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int realMain(int argc, const char** argv)
{
	(void)argc;
	(void)argv;

	for (uint32_t i = 0; i < sizeof_array(s_plugins); ++i)
	{
		if (!PluginHandler_addPlugin(OBJECT_DIR, s_plugins[i]))
			return 0;
	}

	uint32_t width = 1280;
	uint32_t height = 720;
	uint32_t debug = BGFX_DEBUG_TEXT;
	uint32_t reset = BGFX_RESET_VSYNC;

	bgfx::init();
	bgfx::reset(width, height, reset);

	// Enable debug text.
	bgfx::setDebug(debug);

	// Set view 0 clear state.
	bgfx::setViewClear(0
		, BGFX_CLEAR_COLOR_BIT|BGFX_CLEAR_DEPTH_BIT
		, 0x303030ff
		, 1.0f
		, 0
		);

	bgfx::setViewSeq(0, true);

	entry::MouseState mouseState;

	IMGUI_setup((int)width, (int)height);

	while (!entry::processEvents(width, height, debug, reset, &mouseState))
	{
		// Set view 0 default viewport.
		bgfx::setViewRect(0, 0, 0, (uint16_t)width, (uint16_t)height);

		// This dummy draw call is here to make sure that view 0 is cleared
		// if no other draw calls are submitted to view 0.
		bgfx::submit(0);

		IMGUI_preUpdate(&mouseState);

		bool show = true;

    	ImGui::Begin("ImGui Test", &show, ImVec2(550,480), true, ImGuiWindowFlags_ShowBorders);

		if (ImGui::Button("Test0r testing!"))
		{
			printf("test\n");
		}

    	ImGui::End();

		// Use debug font to print information about this example.
		//bgfx::dbgTextClear();
		//bgfx::dbgTextPrintf(0, 1, 0x4f, "bgfx/examples/00-helloworld");
		//bgfx::dbgTextPrintf(0, 2, 0x6f, "Description: Initialization and debug text.");

		IMGUI_postUpdate();

		// Advance to next frame. Rendering thread will be kicked to 
		// process submitted rendering primitives.
		bgfx::frame();
	}

	// Shutdown bgfx.
	bgfx::shutdown();
	

	return 0; //Application_init(argc, argv);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

}

extern "C"
{

int _main_(int argc, char** argv)
{
	return prodbg::realMain(argc, (const char**)argv);
}

}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef WIN32

extern "C" int main(int _argc, char** _argv);

int CALLBACK WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	char* temp[] = { "foo" };
	main(1, temp);
}

#endif

