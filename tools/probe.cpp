/*
 * Console harness for the feed: no QML, no window, no device.
 *
 * It builds a GameFeed exactly as the backend does, lets it run for a few
 * seconds against the live ESPN API, then prints the state map the QML would
 * have read. That is the one check worth doing before anything is deployed --
 * the layout can be argued about on a screenshot, but whether the parsing
 * survives a real payload cannot.
 *
 *   ./probe.sh              # the first followed team
 *   ./probe.sh 2754 20      # a specific team, twenty seconds
 *   LEAGUE=nfl ./probe.sh   # the NFL build instead
 */
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QVariantMap>
#include <QStringList>
#include <QTextStream>

#include "GameFeed.h"
#include "League.h"
#include "LogoStore.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const League league = Leagues::byKey(QString::fromLocal8Bit(qgetenv("LEAGUE")));
    const int teamId  = (argc > 1 && QString::fromLatin1(argv[1]).toInt() > 0)
                        ? QString::fromLatin1(argv[1]).toInt() : league.teams.value(0);
    const int seconds = (argc > 2) ? QString::fromLatin1(argv[2]).toInt() : 12;

    GameFeed feed(league, teamId);
    LogoStore logos(league);

    // Exercise the league-wide page too. On the device this only runs while
    // that page is on screen; here it is always on, because the whole point of
    // the probe is to make every request the app can make.
    feed.watchLive(true);

    // The picker's list: 1.9MB for college football, fetched once and then
    // kept on disk. Exercised here because it is the only request in the app
    // measured in megabytes.
    feed.loadTeamIndex();

    // PROBE_EVENT=<id> exercises the path a reader takes when they tap a game
    // out of the live list: pin the board to a game neither followed team is
    // in, and print what the board would then be showing.
    const QByteArray pinned = qgetenv("PROBE_EVENT");
    if (!pinned.isEmpty()) {
        QTimer::singleShot(4000, &app, [&feed, pinned]() {
            feed.showGame(QString::fromLatin1(pinned), QVariantMap());
        });
    }

    // Ask for every logo the board and the list would want, so the fetch and
    // the greyscale conversion are exercised as well.
    QObject::connect(&feed, &GameFeed::stateChanged, &app, [&]() {
        const QVariantMap s = feed.state();
        for (const char *k : { "awayId", "homeId" })
            logos.logoFor(s.value(QLatin1String(k)).toInt());
        for (const QVariant &tv : s.value(QStringLiteral("teams")).toList()) {
            const QVariantMap t = tv.toMap();
            logos.logoFor(t.value(QStringLiteral("teamId")).toInt());
            for (const QVariant &gv : t.value(QStringLiteral("games")).toList())
                logos.logoFor(gv.toMap().value(QStringLiteral("oppId")).toInt());
        }
    });

    QTimer::singleShot(seconds * 1000, &app, [&]() {
        QTextStream out(stdout);
        const QVariantMap s = feed.state();

        // The board, as one block, with the season lists split out below so the
        // interesting part is not buried under two dozen rows.
        QVariantMap board = s;
        board.remove(QStringLiteral("teams"));
        out << QJsonDocument(QJsonObject::fromVariantMap(board)).toJson(QJsonDocument::Indented);

        for (const QVariant &tv : s.value(QStringLiteral("teams")).toList()) {
            const QVariantMap t = tv.toMap();
            out << "\n" << t.value(QStringLiteral("name")).toString() << "  "
                << t.value(QStringLiteral("record")).toString() << "  "
                << t.value(QStringLiteral("standing")).toString()
                << "  rank=" << t.value(QStringLiteral("rank")).toInt()
                << "  current=" << t.value(QStringLiteral("current")).toString() << "\n";
            for (const QVariant &gv : t.value(QStringLiteral("games")).toList()) {
                const QVariantMap g = gv.toMap();
                out << "  " << g.value(QStringLiteral("week")).toString().leftJustified(7)
                    << g.value(QStringLiteral("date")).toString().leftJustified(8)
                    << (g.value(QStringLiteral("bye")).toBool()
                            ? QStringLiteral("—")
                            : (g.value(QStringLiteral("home")).toBool()
                                   ? QStringLiteral("vs ") : QStringLiteral("at "))
                              + (g.value(QStringLiteral("oppRank")).toInt() > 0
                                     ? QStringLiteral("#%1 ").arg(g.value(QStringLiteral("oppRank")).toInt())
                                     : QString())
                              + g.value(QStringLiteral("oppAbbr")).toString()).leftJustified(14)
                    << g.value(QStringLiteral("note")).toString().leftJustified(12)
                    << g.value(QStringLiteral("eventId")).toString() << "\n";
            }
        }
        const QVariantList live = s.value(QStringLiteral("live")).toList();
        out << "\nLIVE NOW  " << s.value(QStringLiteral("liveCount")).toInt()
            << " in progress of " << s.value(QStringLiteral("dayCount")).toInt()
            << " on " << s.value(QStringLiteral("liveDate")).toString() << "\n";
        for (const QVariant &gv : live) {
            const QVariantMap g = gv.toMap();
            out << "  " << g.value(QStringLiteral("note")).toString().leftJustified(13)
                << (g.value(QStringLiteral("awayAbbr")).toString() + " "
                    + g.value(QStringLiteral("awayScore")).toString()).leftJustified(10)
                << ("at " + g.value(QStringLiteral("homeAbbr")).toString() + " "
                    + g.value(QStringLiteral("homeScore")).toString()).leftJustified(13)
                << g.value(QStringLiteral("downDistance")).toString().leftJustified(11)
                << (g.value(QStringLiteral("followed")).toBool() ? "*" : " ")
                << " " << g.value(QStringLiteral("eventId")).toString() << "\n";
        }

        const QVariantList all = s.value(QStringLiteral("allTeams")).toList();
        out << "\nTEAM INDEX  " << all.size() << " teams";
        if (!all.isEmpty()) {
            const QVariantMap f = all.first().toMap();
            out << "  (first: " << f.value(QStringLiteral("abbr")).toString()
                << " " << f.value(QStringLiteral("name")).toString() << ")";
        }
        out << "\nfollowed: " << s.value(QStringLiteral("followed")).toStringList().join(", ")
            << "\n";

        out << "\nlogos: " << logos.revision() << " converted this run, into "
            << logos.cacheDir() << "\n";
        out.flush();
        app.quit();
    });

    return app.exec();
}
