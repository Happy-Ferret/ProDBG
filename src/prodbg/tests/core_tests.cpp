#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "core/plugin_handler.h"
#include "core/log.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void plugin_handler_null_base_path(void** state)
{
	(void)state;
	assert_false(PluginHandler_addPlugin(0, "dummy"));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void plugin_handler_null_plugin(void** state)
{
	(void)state;
	assert_false(PluginHandler_addPlugin("dummyPath", 0));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void plugin_handler_dummy_paths(void** state)
{
	(void)state;
	assert_false(PluginHandler_addPlugin("dummyPath", "dummy"));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void plugin_handler_add_plugin(void** state)
{
	(void)state;
	assert_false(PluginHandler_addPlugin("dummyPath", "dummy"));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void plugin_handler_add_plugin_true(void** state)
{
	(void)state;
	assert_true(PluginHandler_addPlugin(OBJECT_DIR, "sourcecode_plugin"));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int main()
{
	log_set_level(LOG_NONE);

    const UnitTest tests[] =
	{
		unit_test(plugin_handler_null_base_path),
		unit_test(plugin_handler_null_plugin),
		unit_test(plugin_handler_dummy_paths),
		unit_test(plugin_handler_add_plugin),
		unit_test(plugin_handler_add_plugin_true),
	};

    return run_tests(tests);
}


