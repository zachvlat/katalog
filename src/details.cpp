#include "details.h"

Details::Details(QObject *parent)
    : QObject(parent)
{
}

QVariantList Details::screenshots() const
{
    QVariantList list;
    for (const ScreenshotData &shot : m_data.screenshots) {
        QVariantMap map;
        map.insert(QStringLiteral("url"), shot.url);
        map.insert(QStringLiteral("caption"), shot.caption);
        list.append(map);
    }
    return list;
}

QVariantList Details::releases() const
{
    QVariantList list;
    for (const ReleaseData &release : m_data.releases) {
        QVariantMap map;
        map.insert(QStringLiteral("version"), release.version);
        map.insert(QStringLiteral("date"), release.date);
        map.insert(QStringLiteral("description"), release.description);
        list.append(map);
    }
    return list;
}

void Details::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    Q_EMIT changed();
}

void Details::reset(const QString &appId)
{
    m_data = AppDetailData();
    m_data.appId = appId;
    m_loading = true;
    Q_EMIT changed();
}

void Details::apply(const AppDetailData &data)
{
    m_data = data;
    m_loading = false;
    Q_EMIT changed();
}

void Details::setVerified(bool verified)
{
    if (m_data.verified == verified)
        return;
    m_data.verified = verified;
    Q_EMIT changed();
}
