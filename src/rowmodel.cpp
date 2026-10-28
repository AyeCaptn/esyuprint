#include "rowmodel.h"

RowModel::RowModel(const QString &key, QObject *parent) : QAbstractListModel(parent), m_key(key) {}

int RowModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : count(); }

QVariant RowModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= count() || role != Qt::UserRole)
        return {};
    return m_rows.at(index.row());
}

QHash<int, QByteArray> RowModel::roleNames() const { return {{Qt::UserRole, "modelData"}}; }

void RowModel::update(const QVariantList &rows) {
    const int oldCount = count();
    for (int i = 0; i < rows.size(); ++i) {
        const auto key = rows.at(i).toMap().value(m_key);
        int existing = i;
        while (existing < count() && m_rows.at(existing).toMap().value(m_key) != key)
            ++existing;
        if (existing == count()) {
            beginInsertRows({}, i, i);
            m_rows.insert(i, rows.at(i));
            endInsertRows();
        } else {
            if (existing != i) {
                beginMoveRows({}, existing, existing, {}, i);
                m_rows.move(existing, i);
                endMoveRows();
            }
            if (m_rows.at(i) != rows.at(i)) {
                m_rows[i] = rows.at(i);
                emit dataChanged(index(i), index(i), {Qt::UserRole});
            }
        }
    }
    if (count() > rows.size()) {
        beginRemoveRows({}, rows.size(), count() - 1);
        m_rows.erase(m_rows.begin() + rows.size(), m_rows.end());
        endRemoveRows();
    }
    if (oldCount != count())
        emit countChanged();
}
