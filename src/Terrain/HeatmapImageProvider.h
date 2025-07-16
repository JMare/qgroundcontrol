/****************************************************************************
 *
 * (c) 2024 QGroundControl
 *
 * QGroundControl is licensed according to the terms in the file COPYING.md
 *
 ****************************************************************************/

#pragma once

#include <QQuickImageProvider>
#include <QImage>
#include <QMutex>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(TerrainOverlayImageProviderLog)

class HeatmapImageProvider : public QQuickImageProvider
{
public:
    HeatmapImageProvider();

    static HeatmapImageProvider* instance();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
    void setImage(const QImage& image);

private:
    QImage _image;
    QMutex _mutex;
};
