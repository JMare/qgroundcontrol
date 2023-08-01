/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "PositionManager.h"
#include "QGCApplication.h"
#include "QGCCorePlugin.h"

#if !defined(NO_SERIAL_LINK) && !defined(__android__)
#include <QSerialPortInfo>
#endif

#include <QtPositioning/private/qgeopositioninfosource_p.h>

QGCPositionManager::QGCPositionManager(QGCApplication* app, QGCToolbox* toolbox)
    : QGCTool           (app, toolbox)
{
    _nmeaGPS = new PositionNMEAGPS(this);

    connect(_nmeaGPS,&PositionNMEAGPS::newPositionUpdate,this,&QGCPositionManager::_positionUpdated);
}

QGCPositionManager::~QGCPositionManager()
{
}


void QGCPositionManager::setToolbox(QGCToolbox *toolbox)
{
   QGCTool::setToolbox(toolbox);


}

void QGCPositionManager::setNmeaSourceDevice(QSerialPort* device)
{
   _nmeaGPS->setSourcePort(device);
}

void QGCPositionManager::_positionUpdated(QGeoPositionInfo update)
{
   _geoPositionInfo = update;

   QGeoCoordinate newGCSPosition = QGeoCoordinate();
   qreal newGCSHeading = update.attribute(QGeoPositionInfo::Direction);

   if (update.isValid() && update.hasAttribute(QGeoPositionInfo::HorizontalAccuracy)) {
       // Note that gcsPosition filters out possible crap values
       if (qAbs(update.coordinate().latitude()) > 0.001 &&
           qAbs(update.coordinate().longitude()) > 0.001 ) {
           _gcsPositionHorizontalAccuracy = update.attribute(QGeoPositionInfo::HorizontalAccuracy);
           if (_gcsPositionHorizontalAccuracy <= MinHorizonalAccuracyMeters) {
               newGCSPosition = update.coordinate();
           }
           emit gcsPositionHorizontalAccuracyChanged();
       }
   }
   if (newGCSPosition != _gcsPosition) {
       _gcsPosition = newGCSPosition;
       emit gcsPositionChanged(_gcsPosition);
   }
   _gcsHeading = newGCSHeading;
   emit gcsHeadingChanged(_gcsHeading);

   emit positionInfoUpdated(update);
}

int QGCPositionManager::updateInterval() const
{
    return _updateInterval;
}

void QGCPositionManager::_error(QGeoPositionInfoSource::Error positioningError)
{
    qWarning() << "QGCPositionManager error" << positioningError;
}
