#include <ProDBGAPI.h>

#ifndef _WIN32

#include <stdlib.h> 
#include <stdio.h> 
#include <string.h> 
#include <SBTarget.h>
#include <SBThread.h>
#include <SBListener.h>
#include <SBProcess.h>
#include <SBDebugger.h>
#include <SBHostOS.h>
#include <SBEvent.h>
#include <SBBreakpoint.h>
#include <SBStream.h>
#include <SBValueList.h>
#include <SBCommandInterpreter.h> 
#include <SBCommandReturnObject.h> 

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct LLDBPlugin
{
	lldb::SBDebugger debugger;
	lldb::SBTarget target;
	lldb::SBListener listener;
	lldb::SBProcess process;
	const char* targetName;
	PDDebugState debugState;

} LLDBPlugin;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* createInstance(ServiceFunc* serviceFunc)
{
	lldb::SBDebugger::Initialize();

	printf("Create instance\n");
	LLDBPlugin* plugin = new LLDBPlugin; 

	plugin->debugger = lldb::SBDebugger::Create(false);
	plugin->debugState = PDDebugState_noTarget;
 	plugin->listener = plugin->debugger.GetListener(); 

	return plugin;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void destroyInstance(void* userData)
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void onBreak(LLDBPlugin* data)
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void onStep(LLDBPlugin* plugin)
{
	lldb::SBEvent evt;

	// TODO: Handle more than one thread here

	lldb::SBThread thread(plugin->process.GetThreadAtIndex((size_t)0));

	printf("thread stopReason %d\n", thread.GetStopReason());
	printf("threadValid %d\n", thread.IsValid());

	thread.StepInto();

	plugin->debugState = PDDebugState_running;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void onStepOver(LLDBPlugin* data)
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void onContinue(LLDBPlugin* data)
{
	data->process.Continue();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const bool m_verbose = true;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void updateLLDBEvent(LLDBPlugin* plugin)
{
	if (!plugin->process.IsValid())
		return;

	lldb::SBEvent evt;

	plugin->listener.WaitForEvent(1, evt);
	lldb::StateType state = lldb::SBProcess::GetStateFromEvent(evt);

	printf("event = %s\n", lldb::SBDebugger::StateAsCString(state));

	if (lldb::SBProcess::GetRestartedFromEvent(evt))
	{
		printf("lldb::SBProcess::GetRestartedFromEvent(evt)\n");
		return;
	}

	switch (state)
	{
		case lldb::eStateInvalid:
		case lldb::eStateDetached:
		case lldb::eStateCrashed:
		case lldb::eStateUnloaded:
			return;

		case lldb::eStateExited:
			return;

		case lldb::eStateConnected:
		case lldb::eStateAttaching:
		case lldb::eStateLaunching:
		case lldb::eStateRunning:
		case lldb::eStateStepping:
			return;

		case lldb::eStateStopped:
		case lldb::eStateSuspended:
		{
			//call_test_step = true;
			bool fatal = false;
			bool selected_thread = false;
			for (uint32_t thread_index = 0; thread_index < plugin->process.GetNumThreads(); thread_index++)
			{
				lldb::SBThread thread(plugin->process.GetThreadAtIndex((size_t)thread_index));
				lldb::SBFrame frame(thread.GetFrameAtIndex(0));
				bool select_thread = false;
				lldb::StopReason stop_reason = thread.GetStopReason();

				if (m_verbose) 
					printf("tid = 0x%llx pc = 0x%llx ",thread.GetThreadID(),frame.GetPC());

				switch (stop_reason)
				{
					case lldb::eStopReasonNone:
						if (m_verbose)
							printf("none\n");
						break;
						
					case lldb::eStopReasonTrace:
						select_thread = true;
						plugin->debugState = PDDebugState_stopBreakpoint;
						if (m_verbose)
							printf("trace\n");
						break;
						
					case lldb::eStopReasonPlanComplete:
						select_thread = true;
						plugin->debugState = PDDebugState_stopBreakpoint;
						if (m_verbose)
							printf("plan complete\n");
						break;
					case lldb::eStopReasonThreadExiting:
						if (m_verbose)
							printf("thread exiting\n");
						break;
					case lldb::eStopReasonExec:
						if (m_verbose)
							printf("exec\n");
						break;
					case lldb::eStopReasonInvalid:
						if (m_verbose)
							printf("invalid\n");
						break;
					case lldb::eStopReasonException:
						select_thread = true;
						plugin->debugState = PDDebugState_stopException;
						if (m_verbose)
							printf("exception\n");
						fatal = true;
						break;
					case lldb::eStopReasonBreakpoint:
						select_thread = true;
						plugin->debugState = PDDebugState_stopBreakpoint;
						if (m_verbose)
							printf("breakpoint id = %lld.%lld\n",thread.GetStopReasonDataAtIndex(0),thread.GetStopReasonDataAtIndex(1));
						break;
					case lldb::eStopReasonWatchpoint:
						select_thread = true;
						if (m_verbose)
							printf("watchpoint id = %lld\n",thread.GetStopReasonDataAtIndex(0));
						break;
					case lldb::eStopReasonSignal:
						select_thread = true;
						if (m_verbose)
							printf("signal %d\n",(int)thread.GetStopReasonDataAtIndex(0));
						break;
				}
				if (select_thread && !selected_thread)
				{
					//m_thread = thread;
					selected_thread = plugin->process.SetSelectedThread(thread);
				}
			}
		}
		break;
	}

	const int bufferSize = 2048;
	char buffer[bufferSize];
	size_t amountRead = 0;
	while ((amountRead = plugin->process.GetSTDOUT(buffer, bufferSize)) > 0)
	{
		printf("%s", buffer);
	}
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool actionCallback(void* userData, PDAction action)
{
	LLDBPlugin* plugin = (LLDBPlugin*)userData;

	switch (action)
	{
		case PDAction_break : onBreak(plugin); break;
		case PDAction_step : onStep(plugin); break;
		case PDAction_run : onContinue(plugin); break;
		case PDAction_stepOver : onStepOver(plugin); break;
		case PDAction_stepOut :
		case PDAction_custom :
		case PDAction_none : break;
	}

	// TODO: Handle if the were able to execute the action or not (for example stepping will not work
	//       if we are in state running.

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

PDDebugState updateCallback(void* userData)
{
	LLDBPlugin* plugin = (LLDBPlugin*)userData;

	switch (plugin->debugState)
	{
		case PDDebugState_running : updateLLDBEvent(plugin); break;
	}

	return plugin->debugState;
}


/*

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool startDebugging(void* userData, PDLaunchAction action, void* launchData, PDBreakpointFileLine* breakpoints, int bpCount)
{
	LLDBPlugin* plugin = (LLDBPlugin*)userData;

	// TODO: Check action here

	plugin->target = plugin->debugger.CreateTarget(plugin->target);

	if (!plugin->target.IsValid())
		return false;

	printf("Target is valid, launching\n");

	lldb::SBLaunchInfo launchInfo(0);
	lldb::SBError error;

	plugin->process = plugin->target.Launch(launchInfo, error);

	if (!error.Success())
	{
		printf("error false\n");
		return false;
	}

	if (!plugin->process.IsValid())
	{
		printf("process not valid\n");
		return false;
	}

	// Add breakpoints if we have any

	for (int i = 0; i < bpCount; ++i)
	{
		// TODO: Yeah modifing this one is a race with the main-thread so we should lock at this point

		PDBreakpointFileLine* fileLine = &breakpoints[i]; 

		printf("Trying to add breakpoint %s %d %d\n", fileLine->filename, fileLine->line, fileLine->id);

		if (fileLine->id >= 0)
			continue;

   		lldb::SBBreakpoint breakpoint = plugin->target.BreakpointCreateByLocation(fileLine->filename, (uint32_t)fileLine->line);

		if (breakpoint.IsValid())
		{
			printf("Added breakpoint %s %d\n", fileLine->filename, fileLine->line);
			fileLine->id = breakpoint.GetID();
		}
	}

	plugin->process.GetBroadcaster().AddListener(
			plugin->listener, 
			lldb::SBProcess::eBroadcastBitStateChanged |
			lldb::SBProcess::eBroadcastBitInterrupt);// |
			//lldb::SBProcess::eBroadcastBitSTDOUT);

	plugin->debugState = DebugState_updateEvent;

	// TODO: We should callback to main-thread here so we can update the the UI with the valid breakpoints after we started

	printf("Started ok!\n");

	// TODO

	lldb::SBCommandReturnObject result;
    plugin->debugger.GetCommandInterpreter().HandleCommand("log enable lldb all", result);

	return true;

	//process.Destroy();
	//lldb::SBDebugger::Destroy(plugin->debugger);
}
*/

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void getLocals(LLDBPlugin* plugin, PDSerializeWrite* writer) 
{
	lldb::SBThread thread(plugin->process.GetThreadAtIndex(0));
	lldb::SBFrame frame = thread.GetSelectedFrame();
	
    lldb::SBValueList variables = frame.GetVariables(true, true, true, false);
    
    uint32_t count = variables.GetSize();

    PDWRITE_INT(writer, (int)count);
    	
    for (uint32_t i = 0; i < count; ++i)
    {
    	char address[32];
    	lldb::SBValue value = variables.GetValueAtIndex(i);
    	
    	// TODO: Verify this line
    	sprintf(address, "%016llx", (uint64_t)value.GetAddress().GetFileAddress());
    		
    	PDWRITE_STRING(writer, address);
    	
    	if (value.GetValue())
    		PDWRITE_STRING(writer, value.GetValue());
   		else
    		PDWRITE_STRING(writer, "Unknown"); 
    		
    	if (value.GetTypeName())
    		PDWRITE_STRING(writer, value.GetTypeName());
    	else
    		PDWRITE_STRING(writer, "Unknown"); 

		if (value.GetName())
    		PDWRITE_STRING(writer, value.GetName());
    	else
    		PDWRITE_STRING(writer, "Unknown"); 
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void getCallStack(LLDBPlugin* plugin, PDSerializeWrite* writer)
{
	lldb::SBThread thread(plugin->process.GetThreadAtIndex(0));

	int frameCount = (int)thread.GetNumFrames();

	// TODO: Write type of callstack
	
	PDWRITE_INT(writer, frameCount);
		
	for (int i = 0; i < frameCount; ++i)
	{
		char fileLine[2048];
		char moduleName[2048];
		char addressName[32];

		lldb::SBFrame frame = thread.GetFrameAtIndex((uint32_t)i); 
		lldb::SBModule module = frame.GetModule();
		lldb::SBCompileUnit compileUnit = frame.GetCompileUnit();
		lldb::SBSymbolContext context(frame.GetSymbolContext(0x0000006e));
		lldb::SBLineEntry entry(context.GetLineEntry());

		uint64_t address = (uint64_t)frame.GetPC();
		sprintf(addressName, "%016llx", address);

		module.GetFileSpec().GetPath(moduleName, sizeof(moduleName));
		
		if (compileUnit.GetNumSupportFiles() > 0)
		{
			char filename[2048];
			lldb::SBFileSpec fileSpec = compileUnit.GetSupportFileAtIndex(0);
			fileSpec.GetPath(filename, sizeof(filename));
			sprintf(fileLine, "%s:%d", filename, entry.GetLine());
		}
		else
		{
			strcpy(fileLine, "<unknown>"); 
		}

		PDWRITE_STRING(writer, addressName);
		PDWRITE_STRING(writer, moduleName);
		PDWRITE_STRING(writer, fileLine);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void getExceptionLocation(LLDBPlugin* plugin, PDSerializeWrite* writer)
{
	char filename[2048];

	memset(filename, 0, sizeof(filename));

	// Get the filename & line of the exception/breakpoint
	// TODO: Right now we assume that we only got the break/exception at the first thread.

	lldb::SBThread thread(plugin->process.GetThreadAtIndex(0));
	lldb::SBFrame frame(thread.GetFrameAtIndex(0));
	lldb::SBCompileUnit compileUnit = frame.GetCompileUnit();
	lldb::SBFileSpec filespec(plugin->process.GetTarget().GetExecutable());

	//filespec.GetPath((char*)&plugin->filelineData.filename, 4096);

	if (compileUnit.GetNumSupportFiles() > 0)
	{
		lldb::SBFileSpec fileSpec = compileUnit.GetSupportFileAtIndex(0);
		fileSpec.GetPath(filename, sizeof(filename));
	}

	lldb::SBSymbolContext context(frame.GetSymbolContext(0x0000006e));
	lldb::SBLineEntry entry(context.GetLineEntry());
	int line = (int)entry.GetLine();

	// TODO: Write the type of exception presented here (might be just address for example if assembly)

	PDWRITE_STRING(writer, filename);
	PDWRITE_INT(writer, line);
}

/*

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int addBreakpoint(void* userData, PDBreakpointType type, void* breakpointData)
{
	LLDBPlugin* plugin = (LLDBPlugin*)userData;

	if (!plugin->target.IsValid())
		return -2;

	switch (type)
	{
		case PDBreakpointType_FileLine :
		{
			PDBreakpointFileLine* fileLine = (PDBreakpointFileLine*)breakpointData;
    		lldb::SBBreakpoint breakpoint = plugin->target.BreakpointCreateByLocation(fileLine->filename, (uint32_t)fileLine->line);
    		if (!breakpoint.IsValid())
    			return -1;

    		return (int)breakpoint.GetID();
		}

		case PDBreakpointType_watchPoint :
		case PDBreakpointType_address :
		case PDBreakpointType_custom :
			break;
	}

	return -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void removeBreakpoint(void* userData, int id)
{
	LLDBPlugin* plugin = (LLDBPlugin*)userData;

	if (!plugin->target.IsValid())
		return;

	plugin->target.BreakpointDelete((lldb::break_id_t)id);
}
*/


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void getState(void* userData, PDEventType eventType, int eventId, PDSerializeWrite* writer)
{
	LLDBPlugin* plugin = (LLDBPlugin*)userData;

	(void)eventId;

	switch (eventId)
	{
		case PDEventType_getLocals: 
		{
			getLocals(plugin, writer);
			break;
		}

		case PDEventType_getCallStack:
		{
			getCallStack(plugin, writer);
			break;
		}

		case PDEventType_getExceptionLocation:
		{
			getExceptionLocation(plugin, writer);
			break;
		}

		case PDEventType_getTty:
		{
			break;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void setState(void* userData, PDEventType inEvent, int eventId, PDSerializeRead* reader, PDSerializeWrite* writer)
{
	LLDBPlugin* plugin = (LLDBPlugin*)userData;
	int size = PDREAD_INT(reader);

	// to be used to write back replies (using the eventId when sending back)
	(void)writer;

	switch (inEvent)
	{
		case PDEventType_setBreakpointSourceLine:
		{
			const char* filename = PDREAD_STRING(reader);
			uint32_t line = (uint32_t)PDREAD_INT(reader);

    		lldb::SBBreakpoint breakpoint = plugin->target.BreakpointCreateByLocation(filename, line);
    		if (!breakpoint.IsValid())
			{
				printf("Unable to set breakpoint at %s:%d\n", filename, line);
    			return;
			}

			printf("Set breakpoint at %s:%d\n", filename, line);

			break;
		}

		default:
		{
			reader->skipBytes(reader->readData, size);
			break;
		}
	}
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static PDBackendPlugin plugin =
{
	0,	// version
	"LLDB Mac OS X",
	createInstance,
	destroyInstance,
	updateCallback,
	actionCallback,
	getState,
	setState,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C"
{

void InitPlugin(int version, ServiceFunc* serviceFunc, RegisterPlugin* registerPlugin)
{
	printf("Starting to register Plugin!\n");
	registerPlugin(0, &plugin);
}

}

#endif
