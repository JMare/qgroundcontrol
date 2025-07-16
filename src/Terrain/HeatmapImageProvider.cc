/****************************************************************************
 *
 * (c) 2024 QGroundControl
 *
 * QGroundControl is licensed according to the terms in the file COPYING.md
 *
 ****************************************************************************/

#include "HeatmapImageProvider.h"

#include <QtCore/qapplicationstatic.h>
#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(TerrainOverlayImageProviderLog, "qgc.terrainoverlay.imageprovider");

Q_APPLICATION_STATIC(HeatmapImageProvider, _instance);

HeatmapImageProvider* HeatmapImageProvider::instance()
{
    return _instance;
}

HeatmapImageProvider::HeatmapImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
    qCDebug(TerrainOverlayImageProviderLog) << "HeatmapImageProvider singleton initialized.";
}

QImage HeatmapImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    QMutexLocker locker(&_mutex);

    if (_image.isNull()) {
        qCWarning(TerrainOverlayImageProviderLog) << "[ImageProvider] requestImage FAILED: no image available for id:" << id;
    } else {
        qCDebug(TerrainOverlayImageProviderLog) << "[ImageProvider] requestImage SUCCESS for id:" << id << " size:" << _image.size();
    }

    if (size)
        *size = _image.size();

    if (!requestedSize.isEmpty() && !_image.isNull())
        return _image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    return _image;
}

void HeatmapImageProvider::setImage(const QImage& image)
{
    QMutexLocker locker(&_mutex);
    _image = image;

    qCDebug(TerrainOverlayImageProviderLog) << "[ImageProvider] New image set. Size:" << _image.size();
}
