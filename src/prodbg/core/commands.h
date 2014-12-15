#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Commands_init();

int Commands_needsSave();

void Commands_undo();
void Commands_redo();

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct Command
{
	void* userData;
	void (*exec)(void* userData);
	void (*undo)(void* userData);
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Commands_execute(Command command);
void Commands_beginMulti();
void Commands_endMulti();

int Commands_undoCount();
