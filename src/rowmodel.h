#pragma once
#include <QAbstractListModel>
#include <QVariantMap>

// Keep delegates alive when a poll changes data, rather than replacing a QML array.
class RowModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
  public:
    explicit RowModel(const QString &key, QObject *parent = nullptr);
    int count() const { return m_rows.size(); }
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void update(const QVariantList &rows);
  signals:
    void countChanged();

  private:
    QString m_key;
    QVariantList m_rows;
};
