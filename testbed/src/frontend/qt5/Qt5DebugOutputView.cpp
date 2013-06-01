#include "Qt5DebugOutputView.h"
#include "Qt5MainWindow.h"

namespace prodbg
{

Qt5DebugOutputViewContextMenu::Qt5DebugOutputViewContextMenu(Qt5MainWindow* mainWindow, Qt5BaseView* parent)
: Qt5DynamicViewContextMenu(mainWindow, parent)
{
}

Qt5DebugOutputViewContextMenu::~Qt5DebugOutputViewContextMenu()
{
}

Qt5DebugOutputView::Qt5DebugOutputView(Qt5MainWindow* mainWindow, Qt5DockWidget* dock, Qt5DynamicView* parent)
: Qt5BaseView(mainWindow, dock, parent)
{
	m_type = Qt5ViewType_DebugOutput;

	focusInEvent(nullptr);
	
	m_debugOutput = new Qt5DebugOutput(parent);
    setCentralWidget(m_debugOutput);

    connect(parent, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(contextMenuProxy(const QPoint&)));
    connect(this, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(contextMenuProxy(const QPoint&)));
    connect(m_debugOutput, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(contextMenuProxy(const QPoint&)));

    m_debugOutput->setFocusProxy(this);
}

Qt5DebugOutputView::~Qt5DebugOutputView()
{
	disconnect();
	
	// Reset Focus Tracking (for safety)
	m_mainWindow->setCurrentWindow(nullptr, Qt5ViewType_Reset);

	centralWidget()->deleteLater();
    emit signalDelayedSetCentralWidget(nullptr);
}

void Qt5DebugOutputView::buildLayout()
{

}

void Qt5DebugOutputView::applyLayout(Qt5Layout* layout)
{
	(void)layout;
}

}