#pragma once

#include <QAbstractListModel>
#include "TerrainGridCell.h"

class TerrainOverlayGridModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        LatitudeRole = Qt::UserRole + 1,
        LongitudeRole,
        AltitudeRole,
        ValueRole
    };

    explicit TerrainOverlayGridModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setCells(const QList<TerrainGridCell>& cells);
    void updateCellValue(int index, double value);

private:
    QList<TerrainGridCell> _cells;
};
