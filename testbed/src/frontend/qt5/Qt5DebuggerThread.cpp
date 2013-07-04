#include "Qt5DebuggerThread.h"
#include "Qt5DebugSession.h"
#include <QThread>
#include <core/PluginHandler.h>
#include <core/BinarySerializer.h>
#include <core/Log.h>
#include <ProDBGAPI.h>
#include "../../../API/RemoteAPI/RemoteConnection.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace prodbg
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Qt5DebuggerThread::Qt5DebuggerThread(Qt5DebuggerThread::TargetType type) : 
	m_debugState(PDDebugState_noTarget),
	m_targetType(type)
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	
void Qt5DebuggerThread::start()
{
	int count;

	if (m_targetType == Local)
	{
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
	else
	{
		RemoteConnection* conn = RemoteConnection_create(RemoteConnectionType_Connect, m_port);

		if (!RemoteConnection_connect(conn, m_targetHost, m_port))
		{
			printf("Unable to connect to %s:%d\n", m_targetHost, m_port);
			RemoteConnection_destroy(conn);
			emit finished();
			return;
		}
		else
		{
			printf("Connected to %s:%d\n", m_targetHost, m_port);
		}

		m_connection = conn;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This gets called from the DebugSession (which is on the UI thread) and when the data gets here this thread has
// owner ship of the data and will release it when done with it

void Qt5DebuggerThread::getData(void* serializeData)
{
	PDSerializeRead reader;
	PDSerializeRead* readerPtr = &reader;

	log_debug("Qt5DebuggerThread::getData\n");

	BinarySerializer_initReader(readerPtr, serializeData);

	while (PDREAD_BYTES_LEFT(readerPtr) > 0)
	{
		BinarySerializer_saveReadOffset(readerPtr);

		int size = PDREAD_INT(readerPtr);
		PDEventType event = (PDEventType)PDREAD_INT(readerPtr);
		int id = PDREAD_INT(readerPtr);

		log_debug("event %d size %d id %d\n", (int)event, size, id);

		// We do the save offset/goto next offset as the the plugin may not handle the event and we want to go to the
		// next one and this way the plugin doesn't need to report back if it handled the event or not.
		// This forces us to handle it but worth it as it reduced the complexity for the plugins and it it's easy
		// to forget to return the correct error code and that would cause this loop to loop for ever which is bad.

		m_debuggerPlugin->setState(m_pluginData, event, id, readerPtr, 0);	// can't write back here yet

		BinarySerializer_gotoNextOffset(readerPtr, size);	// -8 as we read 3 ints for size, eventType, id
	}

	// TODO: Not really sure if this is the best way to handle this

	if (m_debuggerPlugin->update(m_pluginData) == PDDebugState_running)
	{
		// if timer isn't active at this point we should start it
		if (!m_timer.isActive())
			m_timer.start(10);
	}

	// After we finished reading the data we free it up
	
	BinarySerializer_destroyData(serializeData);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Qt5DebuggerThread::setRemoteTarget(const char* hostName, int port)
{
	m_targetHost = strdup(hostName);
	m_port = port;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
void Qt5DebuggerThread::tryStartDebugging()
{
	printf("Try starting debugging\n");

	// Start the debugging and if we manage start the update that will be called each 10 ms

	if (m_debuggerPlugin->action(m_pluginData, PDAction_run))
	{
		m_timer.start(10);
	}
}
*/

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Qt5DebuggerThread::sendState()
{
	PDSerializeWrite writer;

	BinarySerializer_initWriter(&writer);

	// get locals

	BinarySerialize_beginEvent(&writer, PDEventType_getLocals, 0);
	m_debuggerPlugin->getState(m_pluginData, PDEventType_getLocals, 0, &writer);
	BinarySerialize_endEvent(&writer);

	// get callstack

	BinarySerialize_beginEvent(&writer, PDEventType_getCallStack, 0);
	m_debuggerPlugin->getState(m_pluginData, PDEventType_getCallStack, 0, &writer);
	BinarySerialize_endEvent(&writer);

	// get exception location

	BinarySerialize_beginEvent(&writer, PDEventType_getExceptionLocation, 0);
	m_debuggerPlugin->getState(m_pluginData, PDEventType_getExceptionLocation, 0, &writer);
	BinarySerialize_endEvent(&writer);
	
	// get TTY 

	BinarySerialize_beginEvent(&writer, PDEventType_getTty, 0);
	m_debuggerPlugin->getState(m_pluginData, PDEventType_getTty, 0, &writer);
	BinarySerialize_endEvent(&writer);

	emit sendData(writer.writeData);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Qt5DebuggerThread::update()
{
	PDDebugState state = m_debuggerPlugin->update(m_pluginData);

	if (m_debugState != state)
	{
		if (PDDebugState_stopException == state || PDDebugState_stopBreakpoint == state) 
		{
			log_debug("Got exception! Sending over state to UI\n");
			sendState();
		}

		m_debugState = state; 
	}	

	// Send over the TTY if any
	
	PDSerializeWrite writer;
	BinarySerializer_initWriter(&writer);
	BinarySerialize_beginEvent(&writer, PDEventType_getTty, 0);
	m_debuggerPlugin->getState(m_pluginData, PDEventType_getTty, 0, &writer);
	BinarySerialize_endEvent(&writer);

	// Only send data if we have something to send
	
	if (BinarySerializer_writeSize(&writer) > 12)
		emit sendData(writer.writeData);
	else
		BinarySerializer_destroyData(writer.writeData);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Qt5DebuggerThread::doAction(int action)
{
	if (m_targetType == Local)
		m_debuggerPlugin->action(m_pluginData, (PDAction)action);
	else
	{
		if (RemoteConnection_isConnected(m_connection))
		{
			uint32_t command = htonl(0 << 16 | action);
			RemoteConnection_send(m_connection, &command, sizeof(uint32_t), 0);
		}
	}
}

}

