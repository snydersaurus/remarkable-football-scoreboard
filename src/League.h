#pragma once

#include <QList>
#include <QString>

/*
 * Which league this build of the app is following.
 *
 * ESPN's site API is the same API for every league -- same event shape, same
 * competition, same competitors, same status, same situation block -- with the
 * league's name in the path. So college football and the NFL differ by a path
 * segment, a logo directory and a list of team ids, and nothing else at all.
 * Verified field by field against both: ids are strings in both, a score is an
 * object in a team's schedule and a string in a single event in both, and the
 * down-and-distance block is identical because it is the same sport.
 *
 * The one real difference is that NFL teams have no ranking, so `curatedRank`
 * is absent and rankOf() returns 0 -- which the board already treats as
 * "unranked" and simply does not draw.
 *
 * Two installs, one binary. Which league a given install is comes from a
 * `league` file next to the backend executable, written by build.sh, because
 * AppLoad launches a backend with a bare environment and nothing else about
 * the process says which app it belongs to.
 */
struct League {
    QString key;        // "cfb" | "nfl", and the name of the file on disk
    QString apiPath;    // the league's segment in the ESPN URL
    QString logoDir;    // .../i/teamlogos/<logoDir>/500/<id>.png
    QString cacheDir;   // ~/.cache/<cacheDir>
    QString appId;      // the AppLoad application id, shared with the manifest
    QList<int> teams;   // followed, in the order they appear on the list page

    QString api() const
    {
        return QStringLiteral("https://site.api.espn.com/apis/site/v2/sports/football/%1")
            .arg(apiPath);
    }
    QString logoUrl(int teamId) const
    {
        return QStringLiteral("https://a.espncdn.com/i/teamlogos/%1/500/%2.png")
            .arg(logoDir).arg(teamId);
    }
};

namespace Leagues {

inline League collegeFootball()
{
    // 194 Ohio State, 2754 Youngstown State.
    return { QStringLiteral("cfb"), QStringLiteral("college-football"),
             QStringLiteral("ncaa"), QStringLiteral("cfb-logos"),
             QStringLiteral("cfb-scoreboard"), { 194, 2754 } };
}

inline League nfl()
{
    // 5 Cleveland Browns, 21 Philadelphia Eagles.
    return { QStringLiteral("nfl"), QStringLiteral("nfl"),
             QStringLiteral("nfl"), QStringLiteral("nfl-logos"),
             QStringLiteral("nfl-scoreboard"), { 5, 21 } };
}

inline League byKey(const QString &key)
{
    return (key == QStringLiteral("nfl")) ? nfl() : collegeFootball();
}

} // namespace Leagues
