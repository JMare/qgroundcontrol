#include "positionnmeagps.h"
#include "QGC.h"
#include "QGCApplication.h"

PositionNMEAGPS::PositionNMEAGPS(QObject *parent)
{

}

void PositionNMEAGPS::setSourcePort(QSerialPort* device)
{
    _port = device;
    start();
}

void PositionNMEAGPS::run()
{
    while (!_exitThread) {
        QGC::SLEEP::msleep(100);
        QByteArray requestData = _port->readAll();
        while (_port->waitForReadyRead(10))
            requestData += _port->readAll();

        qWarning() << "QGCPositionManager lol" << requestData;

    }
}
