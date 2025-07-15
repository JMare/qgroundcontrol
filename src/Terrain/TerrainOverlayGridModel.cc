#include "TerrainOverlayGridModel.h"

TerrainOverlayGridModel::TerrainOverlayGridModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int TerrainOverlayGridModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return _cells.size();
}

QVariant TerrainOverlayGridModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= _cells.size())
        return QVariant();

    const TerrainGridCell& cell = _cells[index.row()];
    switch (role) {
    case LatitudeRole: return cell.latitude;
    case LongitudeRole: return cell.longitude;
    case AltitudeRole: return cell.altitude;
    case ValueRole: return cell.value;
    default: return QVariant();
    }
}

QHash<int, QByteArray> TerrainOverlayGridModel::roleNames() const
{
    return {
        { LatitudeRole, "latitude" },
        { LongitudeRole, "longitude" },
        { AltitudeRole, "altitude" },
        { ValueRole, "value" }
    };
}

void TerrainOverlayGridModel::setCells(const QList<TerrainGridCell>& cells)
{
    beginResetModel();
    _cells = cells;
    endResetModel();
}

void TerrainOverlayGridModel::updateCellValue(int index, double value)
{
    if (index < 0 || index >= _cells.size())
        return;

    if (!qFuzzyCompare(_cells[index].value + 1, value + 1)) {
        _cells[index].value = value;
        emit dataChanged(this->index(index), this->index(index), { ValueRole });
    }
}
