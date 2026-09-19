#pragma once

#include <QObject>
#include <QHash>
#include <QImage>
#include <QNetworkAccessManager>

#include "League.h"

/*
 * Team logos, fetched once and cached on disk as greyscale PNG.
 *
 * ESPN serves logos as 500x500 RGBA PNGs
 * (a.espncdn.com/i/teamlogos/<league>/500/<id>.png) -- raster, not the small
 * clean SVGs MLB publishes, and with 762 college teams no palette assumptions
 * hold.
 * Each pixel is mapped to its luma and then darkened the same way the rest of
 * this app treats colour: near-white stays white because it is knock-out
 * space, everything else is capped so it reads on a white page. Alpha is kept,
 * so the logo still sits on paper rather than in a grey box.
 */
class LogoStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    explicit LogoStore(const League &league, QObject *parent = nullptr);

    int revision() const { return m_revision; }

    // Returns a file:// url once cached, or an empty string while fetching.
    // Requesting an uncached id starts the download.
    Q_INVOKABLE QString logoFor(int teamId);

    // Write the followed team's mark over the AppLoad launcher icon. Same
    // greyscale treatment as the in-app logos, at icon size. AppLoad reads
    // icon.png once, when xochitl starts, so this only shows after a restart.
    void writeLauncherIcon(int teamId, const QString &iconPath);

    // Where the frontend can expect to find them. The AppLoad frontend runs
    // inside xochitl and loads these straight off disk, so the path is part of
    // the contract between the two halves.
    QString cacheDir() const { return m_cacheDir; }

signals:
    void revisionChanged();

private:
    void fetch(int teamId);
    static QImage greyscale(QImage img, int side);

    League m_league;

    QNetworkAccessManager m_net;
    QHash<int, QString> m_ready;
    QHash<int, bool> m_inFlight;
    QString m_cacheDir;
    int m_revision = 0;
};
