#include "Qt5DebuggerThread.h"
#include "Qt5DebugSession.h"
#include <QThread>
#include <core/PluginHandler.h>
#include <core/BinarySerializer.h>
#include <ProDBGAPI.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace prodbg
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Qt5DebuggerThread::Qt5DebuggerThread() : m_debugState(PDDebugState_noTarget)
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	
void Qt5DebuggerThread::start()
{
	int count;

	printf("start Qt5DebuggerThread\n");

	Plugin* plugin = PluginHandler_getPlugins(&count);

	if (count != 1)
	{
		emit finished();
		return;
	}

	// try to start debugging session of a plugin

	m_debuggerPlugin = (PDBackendPlugin*)plugin->data;
	m_pluginData = m_debuggerPlugin->createInstance(0);

	connect(&m_timer, SIGNAL(timeout()), this, SLOT(update()));

	printf("end start Qt5DebuggerThread\n");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This gets called from the DebugSession (which is on the UI thread) and when the data gets here this thread has
// owner ship of the data and will release it when done with it

void Qt5DebuggerThread::getData(void* serializeData)
{
	PDSerializeRead reader;
	PDSerializeRead* readerPtr = &reader;

	BinarySerializer_initReader(readerPtr, serializeData);

	while (reader.bytesLeft(reader.readData) > 0)
	{
		int size = PDREAD_INT(readerPtr);
		int id = PDREAD_INT(readerPtr);
		PDEventType event = (PDEventType)PDREAD_INT(readerPtr);

		// We do the save offset/goto next offset as the the plugin may not handle the event and we want to go to the
		// next one and this way the plugin doesn't need to report back if it handled the event or not.
		// This forces us to handle it but worth it as it reduced the complexity for the plugins and it it's easy
		// to forget to return the correct error code and that would cause this loop to loop for ever which is bad.

		BinarySerializer_saveReadOffset(readerPtr);

		m_debuggerPlugin->setState(m_pluginData, event, id, readerPtr, 0);	// can't write back here yet

		BinarySerializer_gotoNextOffset(readerPtr, size - 8);	// -8 as we read 2 ints for the eventId and eventType
	}

	// After we finished reading the data we free it up
	
	BinarySerializer_destroyData(serializeData);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Qt5DebuggerThread::tryStartDebugging()
{
	printf("Try starting debugging\n");

	// Start the debugging and if we manage start the update that will be called each 10 ms

	if (m_debuggerPlugin->action(m_pluginData, PDAction_run))
	{
		m_timer.start(10);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Qt5DebuggerThread::sendState()
{
	PDSerializeWrite writer;

	BinarySerializer_initWriter(&writer);

	// get locals

	BinarySerialize_beginEvent(&writer);
	m_debuggerPlugin->getState(m_pluginData, PDEventType_getLocals, 0, &writer);
	BinarySerialize_endEvent(&writer);

	// get callstack

	BinarySerialize_beginEvent(&writer);
	m_debuggerPlugin->getState(m_pluginData, PDEventType_getCallStack, 0, &writer);
	BinarySerialize_endEvent(&writer);

	// get exception location

	BinarySerialize_beginEvent(&writer);
	m_debuggerPlugin->getState(m_pluginData, PDEventType_getExceptionLocation, 0, &writer);
	BinarySerialize_endEvent(&writer);

	//emit sendState(writer.writeData);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Qt5DebuggerThread::update()
{
	PDDebugState state = m_debuggerPlugin->update(m_pluginData);

	// TODO: We likely need to handle this better here as a step can be very short and fast (1 instruction)
	// and thus we won't detect a new state change here and not send the current state back to the UI 

	if (m_debugState != state)
	{
		if (PDDebugState_stopException == state || PDDebugState_stopBreakpoint == state) 
			sendState();

		m_debugState = state; 
	}	
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Qt5DebuggerThread::tryStep()
{
	m_debuggerPlugin->action(m_pluginData, PDAction_step);
}

}

