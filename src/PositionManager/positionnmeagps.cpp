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
        if(_port->bytesAvailable())
        {
            QByteArray data = _port->readLine();
            //    requestData += _port->readAll();
            QString myString(data);

            uint8_t simpledata[data.length()];
            for(int i = 0; i < data.length(); i++)
            {
                simpledata[i] = data[i];
            }
            if(myString.length() > 0)
            {
                lwgps_process(&hgps,&simpledata, data.length());
            }
        }

       QGeoCoordinate coord(hgps.latitude,hgps.longitude,hgps.altitude);
       QDateTime now = QDateTime::currentDateTime();
       QGeoPositionInfo posinfo(coord, now);
       posinfo.setAttribute(QGeoPositionInfo::Direction,hgps.course);
       posinfo.setAttribute(QGeoPositionInfo::HorizontalAccuracy,hgps.dop_h);

       emit newPositionUpdate(posinfo);
    }
}
