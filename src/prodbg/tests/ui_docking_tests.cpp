#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "ui/ui_dock.h"
#include "ui/ui_dock_private.h"
#include "core/math.h"
#include "api/plugin_instance.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void create_docking(void**)
{
	Rect rect = { 0, 0, 0, 0 }; 
	UIDockingGrid* grid = UIDock_createGrid(&rect);
	assert_non_null(grid);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void validateRect(Rect r0, Rect r1)
{
	assert_int_equal(r0.x, r1.x);
	assert_int_equal(r0.y, r1.x);
	assert_int_equal(r0.width, r1.width);
	assert_int_equal(r0.height, r1.height);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void validateSize(Rect r, int x, int y, int w, int h)
{
	assert_int_equal(r.x, x);
	assert_int_equal(r.y, y);
	assert_int_equal(r.width, w);
	assert_int_equal(r.height, h);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void test_left_attach(void**)
{
	Rect rect = { 0, 0, 1000, 500 }; 
	UIDockingGrid* grid = UIDock_createGrid(&rect);

	ViewPluginInstance view0 = {};
	ViewPluginInstance view1 = {};
	ViewPluginInstance view2 = {};

	// Validate grid

	validateRect(grid->rect, rect);
	assert_int_equal(grid->sizers.size(), 0);
	assert_int_equal(grid->docks.size(), 0);

	UIDock* dock = UIDock_addView(grid, &view0);

	validateSize(dock->view->rect, 0, 0, 1000, 500);

	assert_true(dock->topSizer == &grid->topSizer);
	assert_true(dock->bottomSizer == &grid->bottomSizer);
	assert_true(dock->rightSizer == &grid->rightSizer);
	assert_true(dock->leftSizer == &grid->leftSizer);
	assert_true(dock->view == &view0);

	assert_int_equal((int)grid->sizers.size(), 0);
	assert_int_equal((int)grid->docks.size(), 1);

	UIDock_dockLeft(grid, dock, &view1);

	// at this point we should have two views/docks split in the middle (vertically)
	// with one sizer so lets verify that

	assert_int_equal((int)grid->sizers.size(), 1);
	assert_int_equal((int)grid->docks.size(), 2);

	UIDock* dock0 = grid->docks[0];
	UIDock* dock1 = grid->docks[1];
	UIDockSizer* s0 = grid->sizers[0]; 

	validateSize(dock0->view->rect, 500, 0, 500, 500);
	validateSize(dock1->view->rect, 0, 0, 500 - g_sizerSize, 500);

	assert_true(s0->dir == UIDockSizerDir_Vert); 

	assert_true(dock0->topSizer == &grid->topSizer);	
	assert_true(dock0->bottomSizer == &grid->bottomSizer);	
	assert_true(dock0->leftSizer == s0); 
	assert_true(dock0->rightSizer == &grid->rightSizer);	

	assert_true(dock1->topSizer == &grid->topSizer);	
	assert_true(dock1->bottomSizer == &grid->bottomSizer);	
	assert_true(dock1->leftSizer == &grid->leftSizer);	
	assert_true(dock1->rightSizer == s0);

	// The top and bottom sizers should now be connected to the two views

	assert_int_equal((int)grid->topSizer.side1.size(), 2);
	assert_int_equal((int)grid->bottomSizer.side0.size(), 2);

	assert_true(s0->side0[0]->view = &view0);
	assert_true(s0->side1[0]->view = &view1);

	UIDock_dockLeft(grid, dock0, &view2);

	// at this point we should have tree views looking like this:
	//  ______s0___s1__
	// |    |   |      |
	// | d1 |d2 |  d0  |
	// |    |   |      |
	// -----------------

	assert_int_equal((int)grid->sizers.size(), 2);
	assert_int_equal((int)grid->docks.size(), 3);

	assert_int_equal((int)grid->topSizer.side1.size(), 3);
	assert_int_equal((int)grid->bottomSizer.side0.size(), 3);

	UIDock* dock2 = grid->docks[2];
	UIDockSizer* s1 = grid->sizers[1]; 

	assert_int_equal((int)s1->side0.size(), 1);
	assert_int_equal((int)s1->side1.size(), 1);
	assert_true(s1->dir == UIDockSizerDir_Vert); 

	// Make sure sizers are assigned correct

	assert_true(dock0->topSizer == &grid->topSizer);	
	assert_true(dock0->bottomSizer == &grid->bottomSizer);	
	assert_true(dock0->leftSizer == s1);	
	assert_true(dock0->rightSizer == &grid->rightSizer);	

	assert_true(dock1->topSizer == &grid->topSizer);	
	assert_true(dock1->bottomSizer == &grid->bottomSizer);	
	assert_true(dock1->leftSizer == &grid->leftSizer);	
	assert_true(dock1->rightSizer == s0);	

	assert_true(dock2->topSizer == &grid->topSizer);	
	assert_true(dock2->bottomSizer == &grid->bottomSizer);	
	assert_true(dock2->leftSizer == s0);	
	assert_true(dock2->rightSizer == s1);	

	// Validate the size of views (they should not go above the inital size)
	// with the sizer size taken into account

	int width = 0;

	for (int i = 0; i < 3; ++i)
		width += (int)grid->docks[(size_t)i]->view->rect.width;

	width += g_sizerSize * 2;

	assert_int_equal(width, rect.width);

	// TODO: currently leaks here, will be fixed once added delete of views
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void test_misc(void**)
{
	Rect rect = { 0, 0, 1000, 500 }; 
	UIDockingGrid* grid = UIDock_createGrid(&rect);

	ViewPluginInstance view0 = {};
	ViewPluginInstance view1 = {};
	ViewPluginInstance view2 = {};

	UIDock* dock = UIDock_addView(grid, &view0);
	UIDock_dockRight(grid, dock, &view1);
	UIDock_dockBottom(grid, dock, &view2);

	// Expected layout:
	//
	//     _____s0_______
	//    |      |      |
	//    |  d0  |      |
	//    |      |      |
	// s1 |------|  d1  |
	//    |      |      |
	//    |  d2  |      |
	//    |      |      |
	//    ---------------
	//
	
	assert_int_equal((int)grid->sizers.size(), 2);
	assert_int_equal((int)grid->docks.size(), 3);

	UIDockSizer* s0 = grid->sizers[0];
	UIDockSizer* s1 = grid->sizers[1];

	UIDock* d0 = grid->docks[0];
	UIDock* d1 = grid->docks[1];
	UIDock* d2 = grid->docks[2];

	assert_true(d0->topSizer == &grid->topSizer);
	assert_true(d0->bottomSizer == s1);
	assert_true(d0->rightSizer == s0); 
	assert_true(d0->leftSizer == &grid->leftSizer); 

	assert_true(d1->topSizer == &grid->topSizer);
	assert_true(d1->bottomSizer == &grid->bottomSizer);
	assert_true(d1->rightSizer == &grid->rightSizer); 
	assert_true(d1->leftSizer == s0); 

	assert_true(d2->topSizer == s1); 
	assert_true(d2->bottomSizer == &grid->bottomSizer);
	assert_true(d2->rightSizer == s0); 
	assert_true(d2->leftSizer == &grid->leftSizer); 
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void test_sizer_hovering(void**)
{
	Vec2 pos;
	Rect rect = { 0, 0, 1000, 500 }; 
	UIDockingGrid* grid = UIDock_createGrid(&rect);

	UIDockSizer* s0 = new UIDockSizer;
	s0->rect.x = 0;
	s0->rect.y = 0;
	s0->rect.width = 10;
	s0->rect.height = g_sizerSize;
	s0->dir = UIDockSizerDir_Horz;

	UIDockSizer* s1 = new UIDockSizer;
	s1->rect.x = 20;
	s1->rect.y = 20;
	s1->rect.width = g_sizerSize;
	s1->rect.height = 20;
	s1->dir = UIDockSizerDir_Vert;

	grid->sizers.push_back(s0);
	grid->sizers.push_back(s1);

	pos.x = -10.0f;
	pos.y = -10.0f;

	assert_true(UIDock_isHoveringSizer(grid, &pos) == UIDockSizerDir_None);
	
	pos.x = 120.0f;
	pos.y = 20.0f;

	assert_true(UIDock_isHoveringSizer(grid, &pos) == UIDockSizerDir_None);

	pos.x = 0.0f;
	pos.y = 0.0f;

	assert_true(UIDock_isHoveringSizer(grid, &pos) == UIDockSizerDir_Horz);

	pos.x = 6.0f;
	pos.y = 6.0f;

	assert_true(UIDock_isHoveringSizer(grid, &pos) == UIDockSizerDir_Horz);

	pos.x = 20.0f;
	pos.y = 20.0f;

	assert_true(UIDock_isHoveringSizer(grid, &pos) == UIDockSizerDir_Vert);

	pos.x = 18.0f;
	pos.y = 30.0f;

	assert_true(UIDock_isHoveringSizer(grid, &pos) == UIDockSizerDir_Vert);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int main()
{
    const UnitTest tests[] =
    {
        unit_test(create_docking),
        unit_test(test_left_attach),
        unit_test(test_misc),
        unit_test(test_sizer_hovering),
    };

    return run_tests(tests);
}

