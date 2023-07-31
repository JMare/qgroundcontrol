#ifndef POSITIONNMEAGPS_H
#define POSITIONNMEAGPS_H

#include <QObject>
#include <QThread>
#include <QSerialPort>

class PositionNMEAGPS : public QThread
{
    Q_OBJECT
public:
    explicit PositionNMEAGPS(QObject *parent = nullptr);
    void setSourcePort(QSerialPort* device);

signals:

private:
    // Override from QThread
    virtual void run();

protected:
    std::atomic<bool> _exitThread{false};    ///< true: signal thread to exit

    QSerialPort* _port = nullptr;
};

#endif // POSITIONNMEAGPS_H
