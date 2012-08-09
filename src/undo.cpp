/******************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  Framework for Undo features
 * Author:   Jesper Weissglas
 *
 ***************************************************************************
 *   Copyright (C) 2012 by David S. Register                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.         *
 ***************************************************************************
 *
 *
 */

#include "wx/wxprec.h"

#ifndef  WX_PRECOMP
#include "wx/wx.h"
#endif

#include <wx/file.h>
#include <wx/datetime.h>
#include <wx/clipbrd.h>

#include "ocpn_types.h"
#include "navutil.h"
#include "styles.h"
#include "routeman.h"
#include "routemanagerdialog.h"
#include "tinyxml.h"
#include "undo.h"

extern Routeman *g_pRouteMan;
extern MyConfig *pConfig;
extern Select *pSelect;
extern RouteManagerDialog *pRouteManagerDialog;
extern WayPointman *pWayPointMan;

Undo::Undo()
{
    depthSetting = 10;
    stackpointer = 0;
    isInsideUndoableAction = false;
}

Undo::~Undo()
{
    for( unsigned int i=0; i<undoStack.size(); i++ ) {
        delete undoStack[i];
    }
}

wxString UndoAction::Description()
{
    wxString descr;
    switch( type ){
        case Undo_CreateWaypoint:
            descr = _("Create Waypoint");
            break;
        case Undo_DeleteWaypoint:
            descr = _("Delete Waypoint");
            break;
        case Undo_MoveWaypoint:
            descr = _("Move Waypoint");
            break;
    }
    return descr;
}

void doUndoMoveWaypoint( UndoAction* action ) {
    double lat, lon;
    RoutePoint* currentPoint = (RoutePoint*) action->after[0];
    wxRealPoint* lastPoint = (wxRealPoint*) action->before[0];
    lat = currentPoint->m_lat;
    lon = currentPoint->m_lon;
    currentPoint->m_lat = lastPoint->y;
    currentPoint->m_lon = lastPoint->x;
    lastPoint->y = lat;
    lastPoint->x = lon;
    SelectItem* selectable = (SelectItem*) action->selectable[0];
    selectable->m_slat = currentPoint->m_lat;
    selectable->m_slon = currentPoint->m_lon;

    wxArrayPtrVoid* routeArray = g_pRouteMan->GetRouteArrayContaining( currentPoint );
    if( routeArray ) {
        for( unsigned int ir = 0; ir < routeArray->GetCount(); ir++ ) {
            Route *pr = (Route *) routeArray->Item( ir );
            pr->CalculateBBox();
            pr->UpdateSegmentDistances();
            pConfig->UpdateRoute( pr );
        }
    }
}

void doUndoDeleteWaypoint( UndoAction* action )
{
    RoutePoint* point = (RoutePoint*) action->before[0];
    pSelect->AddSelectableRoutePoint( point->m_lat, point->m_lon, point );
    pConfig->AddNewWayPoint( point, -1 );
    if( NULL != pWayPointMan ) pWayPointMan->m_pWayPointList->Append( point );
    if( pRouteManagerDialog && pRouteManagerDialog->IsShown() ) pRouteManagerDialog->UpdateWptListCtrl();
}

void doRedoDeleteWaypoint( UndoAction* action )
{
    RoutePoint* point = (RoutePoint*) action->before[0];
    pConfig->DeleteWayPoint( point );
    pSelect->DeleteSelectablePoint( point, SELTYPE_ROUTEPOINT );
    if( NULL != pWayPointMan ) pWayPointMan->m_pWayPointList->DeleteObject( point );
    if( pRouteManagerDialog && pRouteManagerDialog->IsShown() ) pRouteManagerDialog->UpdateWptListCtrl();
}

bool Undo::AnythingToUndo()
{
    return undoStack.size() > stackpointer;
}

bool Undo::AnythingToRedo()
{
    return stackpointer > 0;
}

UndoAction* Undo::GetNextUndoableAction()
{
    return undoStack[stackpointer];
}

UndoAction* Undo::GetNextRedoableAction()
{
    return undoStack[stackpointer-1];
}

void Undo::InvalidateRedo()
{
    if( stackpointer == 0 ) return;

    // Make sure we are not deleting any objects pointed to by
    // potential redo actions.

    for( unsigned int i=0; i<stackpointer; i++ ) {
        switch( undoStack[i]->type ) {
            case Undo_DeleteWaypoint:
                undoStack[i]->before[0] = NULL;
                break;
            case Undo_CreateWaypoint:
            case Undo_MoveWaypoint:
                break;
        }
        delete undoStack[i];
    }

    undoStack.erase( undoStack.begin(), undoStack.begin() + stackpointer );
    stackpointer = 0;
}

void Undo::InvalidateUndo()
{
    undoStack.clear();
    stackpointer = 0;
}

bool Undo::UndoLastAction()
{
    if( !AnythingToUndo() ) return false;
    UndoAction* action = GetNextUndoableAction();

    switch( action->type ){

        case Undo_CreateWaypoint:
            doRedoDeleteWaypoint( action ); // Same as delete but reversed.
            stackpointer++;
            break;

        case Undo_MoveWaypoint:
            doUndoMoveWaypoint( action );
            stackpointer++;
            break;

        case Undo_DeleteWaypoint:
            doUndoDeleteWaypoint( action );
            stackpointer++;
            break;
    }
    return true;
}

bool Undo::RedoNextAction()
{
    if( !AnythingToRedo() ) return false;
    UndoAction* action = GetNextRedoableAction();

    switch( action->type ){

        case Undo_CreateWaypoint:
            doUndoDeleteWaypoint( action ); // Same as delete but reversed.
            stackpointer--;
            break;

        case Undo_MoveWaypoint:
            doUndoMoveWaypoint( action ); // For Wpt move, redo is same as undo (swap lat/long);
            stackpointer--;
            break;

        case Undo_DeleteWaypoint:
            doRedoDeleteWaypoint( action );
            stackpointer--;
            break;
    }
    return true;
}

bool Undo::BeforeUndoableAction( UndoType type, UndoItemPointer before, UndoBeforePointerType beforeType,
        UndoItemPointer selectable )
{
    if( isInsideUndoableAction ) {
        // Cancel the previous Before.
        delete candidate;
        return false;
    }

    InvalidateRedo();
    candidate = new UndoAction;
    candidate->before.clear();
    candidate->beforeType.clear();
    candidate->selectable.clear();
    candidate->after.clear();

    candidate->type = type;
    UndoItemPointer subject = before;

    switch( beforeType ){
        case Undo_NeedsCopy: {
            switch( candidate->type ) {
                case Undo_MoveWaypoint: {
                    wxRealPoint* point = new wxRealPoint;
                    RoutePoint* rp = (RoutePoint*) before;
                    point->x = rp->m_lon;
                    point->y = rp->m_lat;
                    subject = point;
                    break;
                }
                case Undo_CreateWaypoint: break;
                case Undo_DeleteWaypoint: break;
            }
            break;
        }
        case Undo_IsOrphanded: break;
        case Undo_HasParent: break;
    }

    candidate->before.push_back( subject );
    candidate->beforeType.push_back( beforeType );
    candidate->selectable.push_back( selectable );

    isInsideUndoableAction = true;
    return true;
}

bool Undo::AfterUndoableAction( UndoItemPointer after )
{
    if( !isInsideUndoableAction ) return false;

    candidate->after.push_back( after );
    undoStack.push_front( candidate );

    if( undoStack.size() > depthSetting ) {
        undoStack.pop_back();
    }

    isInsideUndoableAction = false;
    return true;
}

//-----------------------------------------------------------------------------------

UndoAction::~UndoAction()
{
    assert( before.size() == beforeType.size() );

    for( unsigned int i = 0; i < before.size(); i++ ) {
        switch( beforeType[i] ){
            case Undo_NeedsCopy: {
                switch( type ){
                    case Undo_MoveWaypoint:
                        if( before[i] ) {
                            delete (wxRealPoint*) before[i];
                        }
                        break;
                    case Undo_DeleteWaypoint: break;
                    case Undo_CreateWaypoint: break;
                }
                break;
            }
            case Undo_IsOrphanded: {
                switch( type ){
                    case Undo_DeleteWaypoint:
                        if( before[i] ) {
                            delete (RoutePoint*) before[i];
                        }
                        break;
                    case Undo_CreateWaypoint: break;
                    case Undo_MoveWaypoint: break;
                }
                break;
            }
            case Undo_HasParent: break;
        }
    }
    before.clear();
}
