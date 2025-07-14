#pragma once

#include <QAbstractListModel>
#include "TileOverlayEntry.h"

class TileOverlayModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        LatRole = Qt::UserRole + 1,
        LonRole,
        AltitudeRole,
        ColorRole
    };

    explicit TileOverlayModel(QObject* parent = nullptr)
        : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        Q_UNUSED(parent)
        return _entries.count();
    }

    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() >= _entries.count()) return QVariant();
        auto* entry = _entries[index.row()];
        switch (role) {
        case LatRole: return entry->lat();
        case LonRole: return entry->lon();
        case AltitudeRole: return entry->altitude();
        case ColorRole: return entry->color();
        default: return QVariant();
        }
    }

    QHash<int, QByteArray> roleNames() const override {
        return {
            {LatRole, "lat"},
            {LonRole, "lon"},
            {AltitudeRole, "altitude"},
            {ColorRole, "color"}
        };
    }

    void clear() {
        beginResetModel();
        qDeleteAll(_entries);
        _entries.clear();
        endResetModel();
    }

    void addEntry(TileOverlayEntry* entry) {
        beginInsertRows(QModelIndex(), _entries.count(), _entries.count());
        _entries.append(entry);
        endInsertRows();
    }

private:
    QList<TileOverlayEntry*> _entries;
};
