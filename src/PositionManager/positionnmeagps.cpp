#include "positionnmeagps.h"
#include "QGC.h"
#include "QGCApplication.h"

#include "lwgps/lwgps.h"

/* GPS handle */

lwgps_t hgps;

PositionNMEAGPS::PositionNMEAGPS(QObject *parent)
{

}

void PositionNMEAGPS::setSourcePort(QSerialPort* device)
{
    _port = device;
    _port->open(QIODevice::ReadOnly);
    lwgps_init(&hgps);
    start();
}

void PositionNMEAGPS::run()
{
    while (!_exitThread) {
        QGC::SLEEP::msleep(50);
        if (_port && _port->isOpen()) {
            qint64 byteCount = _port->bytesAvailable();
            if (byteCount) {
                QByteArray buffer;
                buffer.resize(byteCount);
                _port->read(buffer.data(), buffer.size());
                QString myString(buffer);

                lwgps_process(&hgps,buffer.data(), buffer.size());
                qWarning() << myString;

            }
        } else {
            // Error occurred
            qWarning() << "Serial port not readable";
        }

       QGeoCoordinate coord(hgps.latitude,hgps.longitude,hgps.altitude);
       QDateTime now = QDateTime::currentDateTime();
       QGeoPositionInfo posinfo(coord, now);
       posinfo.setAttribute(QGeoPositionInfo::Direction,hgps.course);
       posinfo.setAttribute(QGeoPositionInfo::HorizontalAccuracy,hgps.dop_h);

       emit newPositionUpdate(posinfo);
    }
}
