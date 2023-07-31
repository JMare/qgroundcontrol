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

void QGCPositionManager::_positionUpdated(const QGeoPositionInfo &update)
{

}

int QGCPositionManager::updateInterval() const
{
    return _updateInterval;
}

void QGCPositionManager::_error(QGeoPositionInfoSource::Error positioningError)
{
    qWarning() << "QGCPositionManager error" << positioningError;
}
