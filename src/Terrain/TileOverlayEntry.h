#pragma once

#include <QObject>
#include <QColor>

class TileOverlayEntry : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double lat READ lat CONSTANT)
    Q_PROPERTY(double lon READ lon CONSTANT)
    Q_PROPERTY(double altitude READ altitude CONSTANT)
    Q_PROPERTY(QColor color READ color CONSTANT)

public:
    TileOverlayEntry(double lat, double lon, double altitude, const QColor& color, QObject* parent = nullptr)
        : QObject(parent), _lat(lat), _lon(lon), _altitude(altitude), _color(color) {}

    double lat() const { return _lat; }
    double lon() const { return _lon; }
    double altitude() const { return _altitude; }
    QColor color() const { return _color; }

private:
    double _lat;
    double _lon;
    double _altitude;
    QColor _color;
};
