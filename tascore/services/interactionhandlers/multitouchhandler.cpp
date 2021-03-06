/*************************************************************************** 
** 
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies). 
** All rights reserved. 
** Contact: Nokia Corporation (testabilitydriver@nokia.com) 
** 
** This file is part of Testability Driver Qt Agent
** 
** If you have questions regarding the use of this file, please contact 
** Nokia at testabilitydriver@nokia.com . 
** 
** This library is free software; you can redistribute it and/or 
** modify it under the terms of the GNU Lesser General Public 
** License version 2.1 as published by the Free Software Foundation 
** and appearing in the file LICENSE.LGPL included in the packaging 
** of this file. 
** 
****************************************************************************/ 
 


#include <testabilityutils.h>

#include "taslogger.h"
#include "tasmultigesturerunner.h"
#include "multitouchhandler.h"

/*!
  \class MultitouchHandler
  \brief MultitouchHandler generates mouse press and release events.

*/    


MultitouchHandler::MultitouchHandler()
{
    mPressCommands << "MouseClick" << "MousePress" << "Tap" ;
    mReleaseCommands << "MouseClick" << "MouseRelease" << "Tap" ;
    mFactory = new TasGestureFactory();    
}

MultitouchHandler::~MultitouchHandler()
{    
    delete mFactory;
}

/*!
  Starts a multitouch gesture motion based on the given arguments from TasCommand. 
  The gesture is done using a mouse press, move and release operations.
  The path is determined from the given arguments and a QLineF or a list
  of points build from them.

  QTimeLine is used to make the gesture operations. The value from the valuechanged signal
  of timeline is used to determine the position.
 */
bool MultitouchHandler::executeInteraction(TargetData /*data*/)
{
    return false;
}

bool MultitouchHandler::executeMultitouchInteraction(QList<TargetData> dataList)
{
    TasLogger::logger()->debug("MultitouchHandler::executeMultitouchInteraction");
    //check eventtype, must be the same with all so use first one
    bool sendPrimary = false;
    if(!dataList.isEmpty()){
        if(!dataList.first().command->parameter(POINTER_TYPE).isEmpty()){
            if(MouseHandler::TypeBoth == static_cast<MouseHandler::PointerType>(dataList.first().command->parameter(POINTER_TYPE).toInt())){
                sendPrimary = true;       
                TasLogger::logger()->debug("MultitouchHandler::executeMultitouchInteraction primary on " + dataList.first().command->parameter(POINTER_TYPE));
            }
        }
    }

    bool consumed = false;
    if(!dataList.isEmpty()){
        consumed = true;
        //look for tap downs and taps (press down motion to start the multitouch)
        QList<QTouchEvent::TouchPoint> touchPoints;
        QList<QTouchEvent::TouchPoint> touchReleasePoints;
        //we need to group points to enable the touch ids for points
        //the string is actually a identifier for a graphicsitem and possible extra details 
        //if specific points pressed
        QHash<QString, QList<TasTouchPoints>* > itemPressPoints;
        QHash<QString, QList<TasTouchPoints>* > itemReleasePoints;

        QList<TasGesture*> gestures;
        TargetData targetData;
        foreach(targetData, dataList){
            if(mPressCommands.contains(targetData.command->name())){            
                QString identifier = idAndCoordinates(targetData);
                if(!itemPressPoints.contains(identifier)){
                    itemPressPoints.insert(identifier, new QList<TasTouchPoints>());
                }
                itemPressPoints.value(identifier)->append(mTouchGen.toTouchPoint(targetData.targetPoint, false));
            }
            if(mReleaseCommands.contains(targetData.command->name())){
                QString identifier = idAndCoordinates(targetData);
                if(!itemReleasePoints.contains(identifier)){
                    itemReleasePoints.insert(identifier, new QList<TasTouchPoints>());
                }
                itemReleasePoints.value(identifier)->append(mTouchGen.toTouchPoint(targetData.targetPoint, false));
            }            
            TasGesture* gesture = mFactory->makeGesture(targetData);
            if(gesture){
                gestures.append(gesture);
            }             
        }

        //currently only one target widget supported
        QWidget *target = dataList.first().target;

        //make touch points from the collected points per graphicsitem
        QMutableHashIterator<QString, QList<TasTouchPoints>* > presses(itemPressPoints);
        bool primaryOn = sendPrimary;
        QPoint mousePressPoint;
        while (presses.hasNext()) {
            presses.next();
            touchPoints.append(mTouchGen.convertToTouchPoints(target, Qt::TouchPointPressed, *presses.value(), sendPrimary, presses.key()));
            if(primaryOn){
                mousePressPoint = presses.value()->first().screenPoint;
            }
            primaryOn = false;//only one can be primary
        }
        //check the that 
        
        primaryOn = sendPrimary;
        QPoint mouseReleasePoint;
        QMutableHashIterator<QString, QList<TasTouchPoints>* > releases(itemReleasePoints);
        while (releases.hasNext()) {
            releases.next();
            bool primaryRelease = false;
            if(primaryOn){
                //look for primary point
                QList<TasTouchPoints>* pointList = releases.value();
                for(int i = 0 ; i < pointList->size(); i++){
                    TasTouchPoints tasPoint = pointList->at(i);
                    if(tasPoint.screenPoint == mousePressPoint){
                        TasLogger::logger()->debug("MultitouchHandler::executeMultitouchInteraction primary to release point. ");
                        mouseReleasePoint = tasPoint.screenPoint;
                        primaryRelease = true; 
                        pointList->move(i, 0);
                        break;
                    }                    
                }
            }
            touchReleasePoints.append(mTouchGen.convertToTouchPoints(target, Qt::TouchPointReleased, *releases.value(), primaryRelease, releases.key()));
            if(primaryRelease){
                primaryOn = false;//only one can be primary
            }
        }

        //send begin event
        if(!touchPoints.isEmpty()){
            QTouchEvent* touchPress = new QTouchEvent(QEvent::TouchBegin, QTouchEvent::TouchScreen, Qt::NoModifier, 
                                                      Qt::TouchPointPressed, touchPoints);
            touchPress->setWidget(target);
            mTouchGen.sendTouchEvent(target, touchPress);
            if(sendPrimary){
                TasLogger::logger()->debug("MultitouchHandler::executeMultitouchInteraction send mouse event press.");
                mMouseGen.doMousePress(target, Qt::LeftButton, mousePressPoint);
            } 
        }

        if(!touchReleasePoints.isEmpty()){
            //send end event
            QTouchEvent *touchRelease = new QTouchEvent(QEvent::TouchEnd, QTouchEvent::TouchScreen, Qt::NoModifier, 
                                                        Qt::TouchPointReleased, touchReleasePoints);
            touchRelease->setWidget(target);
            mTouchGen.sendTouchEvent(target, touchRelease);            
            if(sendPrimary && mouseReleasePoint == mousePressPoint){
                TasLogger::logger()->debug("MultitouchHandler::executeMultitouchInteraction send mouse event release.");
                mMouseGen.doMouseRelease(target, Qt::LeftButton, mouseReleasePoint);
            }
        }
        //add support for gestures...
        if(!gestures.isEmpty()){
            //removes itself after done
            new TasMultiGestureRunner(gestures);
        }

        //cleanup
        qDeleteAll(itemPressPoints);
        itemPressPoints.clear();
        qDeleteAll(itemReleasePoints);
        itemReleasePoints.clear();

    }
    return consumed;
}

QString MultitouchHandler::idAndCoordinates(TargetData& data)
{    
    QString id;
    if(data.targetItem){
        id = TasCoreUtils::pointerId(data.targetItem);
    }
    else{
        id = TasCoreUtils::pointerId(data.target);
    }
    if(data.command->parameter("useCoordinates") == "true"){ 
        data.targetPoint.setX(data.command->parameter("x").toInt());
        data.targetPoint.setY(data.command->parameter("y").toInt());        
        id = QString::number(data.targetPoint.x()) +"_"+ QString::number(data.targetPoint.y());
    }
    return id;
}
