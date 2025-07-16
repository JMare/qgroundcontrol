/****************************************************************************
 *
 * (c) 2024 QGroundControl
 *
 * QGroundControl is licensed according to the terms in the file COPYING.md
 *
 ****************************************************************************/

#pragma once

#include <QObject>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(TerrainOverlayMapLog)

class TerrainOverlayMapRenderer : public QObject
{
    Q_OBJECT

public:
    explicit TerrainOverlayMapRenderer(QObject* parent = nullptr);

    static TerrainOverlayMapRenderer* instance();
    static void registerQmlTypes();

private slots:
    void _onGridDataAvailable();

private:
    void _connectToManager();
};
