#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Qt5Locals.h"
#include "Qt5BaseView.h"
#include "Qt5DynamicView.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace prodbg
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class Qt5LocalsViewContextMenu : public Qt5DynamicViewContextMenu
{
    Q_OBJECT

public:
    Qt5LocalsViewContextMenu(Qt5MainWindow* mainWindow, Qt5BaseView* parent = nullptr);
    virtual ~Qt5LocalsViewContextMenu();
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class Qt5LocalsView : public Qt5BaseView
{
    Q_OBJECT

public:
    Qt5LocalsView(Qt5MainWindow* mainWindow, Qt5DockWidget* dock, Qt5DynamicView* parent = nullptr);
    virtual ~Qt5LocalsView();

    virtual Qt5ViewType getViewType() const
    {
        return Qt5ViewType_Locals;
    }

    virtual QString getViewTypeName() const
    {
        return "Locals";
    }

protected:
    virtual Qt5ContextMenu* createContextMenu()
    {
        return new Qt5LocalsViewContextMenu(m_mainWindow, this);
    }

private:
    Qt5Locals* m_locals;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

}
