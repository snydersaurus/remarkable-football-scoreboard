#include "LogoStore.h"
#include "EspnUserAgent.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>

namespace {
// Displayed at about a fifth of the source size, and the tablet decodes these
// on every repaint, so cache them small.
constexpr int kSide = 256;

// AppLoad's launcher icons are 600x600.
constexpr int kIconSide = 600;
} // namespace

LogoStore::LogoStore(const League &league, QObject *parent)
    : QObject(parent), m_league(league)
{
    // /home/root survives firmware updates; the XDG cache dir may not exist.
    // Each league gets its own directory: team ids collide across leagues,
    // and 5 is the Browns here and Marshall over there.
    m_cacheDir = QDir::homePath() + QStringLiteral("/.cache/") + league.cacheDir;
    QDir().mkpath(m_cacheDir);

    // Adopt anything cached from a previous run.
    const QStringList existing =
        QDir(m_cacheDir).entryList(QStringList() << QStringLiteral("*.png"), QDir::Files);
    for (const QString &f : existing) {
        const int id = QFileInfo(f).baseName().toInt();
        if (id > 0)
            m_ready.insert(id, QUrl::fromLocalFile(m_cacheDir + "/" + f).toString());
    }
}

QString LogoStore::logoFor(int teamId)
{
    if (teamId <= 0)
        return QString();
    if (m_ready.contains(teamId))
        return m_ready.value(teamId);
    if (!m_inFlight.value(teamId, false))
        fetch(teamId);
    return QString();
}

// Rec.601 luma, then two cases: near-white is knock-out space and stays white,
// everything else is ink and is darkened enough to read on paper. A scarlet O
// at luma 90 and a navy field at luma 40 stay distinguishable; both flattened
// to black would not. Alpha is kept, so the mark sits on paper rather than in
// a grey box.
QImage LogoStore::greyscale(QImage img, int side)
{
    if (img.width() > side || img.height() > side)
        img = img.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    img = img.convertToFormat(QImage::Format_ARGB32);

    for (int y = 0; y < img.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb p = line[x];
            const int a = qAlpha(p);
            if (a == 0)
                continue;
            int v = qGray(p);
            v = (v >= 245) ? 255 : qMin(v, 150);
            line[x] = qRgba(v, v, v, a);
        }
    }
    return img;
}

void LogoStore::writeLauncherIcon(int teamId, const QString &iconPath)
{
    if (teamId <= 0 || iconPath.isEmpty())
        return;

    QNetworkRequest req{QUrl(m_league.logoUrl(teamId))};
    req.setRawHeader("User-Agent", Espn::kUserAgent);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_net.get(req);

    connect(reply, &QNetworkReply::finished, this, [reply, iconPath, teamId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qWarning("icon: fetch failed for %d: %s", teamId,
                     qPrintable(reply->errorString()));
            return;
        }
        QImage src;
        if (!src.loadFromData(reply->readAll(), "PNG")) {
            qWarning("icon: team %d did not return a PNG", teamId);
            return;
        }

        // Centred on a square transparent canvas: the marks are not all the
        // same shape, and a letterboxed icon looks wrong next to the others.
        const QImage mark = greyscale(src, kIconSide - 80);
        QImage icon(kIconSide, kIconSide, QImage::Format_ARGB32);
        icon.fill(Qt::transparent);
        QPainter painter(&icon);
        painter.drawImage((kIconSide - mark.width()) / 2,
                          (kIconSide - mark.height()) / 2, mark);
        painter.end();

        if (!icon.save(iconPath, "PNG")) {
            qWarning("icon: cannot write %s", qPrintable(iconPath));
            return;
        }
        qInfo("icon: set to team %d (visible after xochitl restarts)", teamId);
    });
}

void LogoStore::fetch(int teamId)
{
    m_inFlight[teamId] = true;

    QNetworkRequest req{QUrl(m_league.logoUrl(teamId))};
    req.setRawHeader("User-Agent", Espn::kUserAgent);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_net.get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply, teamId]() {
        reply->deleteLater();
        m_inFlight[teamId] = false;
        if (reply->error() != QNetworkReply::NoError) {
            qWarning("logo %d failed: %s", teamId, qPrintable(reply->errorString()));
            return;
        }

        QImage img;
        if (!img.loadFromData(reply->readAll(), "PNG")) {
            qWarning("logo %d: not a PNG", teamId);
            return;
        }
        img = greyscale(img, kSide);

        const QString path = m_cacheDir + QStringLiteral("/%1.png").arg(teamId);
        if (!img.save(path, "PNG")) {
            qWarning("cannot write %s", qPrintable(path));
            return;
        }

        m_ready.insert(teamId, QUrl::fromLocalFile(path).toString());
        ++m_revision;
        emit revisionChanged();
    });
}
