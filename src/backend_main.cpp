/*
 * AppLoad backend for the college football scoreboard.
 *
 * The frontend is QML loaded into xochitl itself, so it cannot fetch anything:
 * it inherits xochitl's OpenSSL policy, which restricts TLS 1.2 to ECDHE-ECDSA
 * suites, and OPENSSL_CONF cannot be set for xochitl without changing
 * device-wide TLS. Doing the network here is what makes the split work -- this
 * is a separate process with its own environment.
 *
 * Everything is pushed as one JSON blob whenever it changes; the frontend is a
 * pure view. Logos are written to a cache directory and the QML loads them by
 * file path, so no image data crosses the socket.
 */
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

#include "AppLoadLink.h"
#include "GameFeed.h"
#include "League.h"
#include "LogoStore.h"

namespace {

// Backend -> frontend
constexpr quint32 MsgState = 101;
// Frontend -> backend
constexpr quint32 MsgHello       = 1;   // frontend ready, send everything
constexpr quint32 MsgShowGame    = 2;   // contents: ESPN event id
constexpr quint32 MsgShowTeam    = 3;   // back to the followed team's game
constexpr quint32 MsgGeometry    = 4;   // frontend reporting its window size
constexpr quint32 MsgWatchLive   = 5;   // contents: "1" while the live page is up
constexpr quint32 MsgSetTeams    = 6;   // contents: "194,2754" -- the pair to follow
constexpr quint32 MsgWantTeams   = 7;   // the picker opened; send the league's teams
constexpr quint32 MsgRefresh     = 8;   // the app is back on screen; refetch now

// The tablet autosleeps aggressively -- deep suspend roughly 40 seconds after
// the last touch -- and nothing runs while it is down: no timers, no network.
//
// A wakelock keeps the poll alive, but holding one all through a three and a
// half hour football game would empty the battery, so it is off unless
// CFB_STAY_AWAKE=1 says otherwise. After a suspend the poll resumes and the
// board is current within one cycle anyway.
void setWakeLock(bool wanted)
{
    static bool held = false;
    if (wanted == held)
        return;

    QFile f(wanted ? QStringLiteral("/sys/power/wake_lock")
                   : QStringLiteral("/sys/power/wake_unlock"));
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("wakelock: cannot open %s", qPrintable(f.fileName()));
        return;
    }
    f.write("football_scoreboard");
    f.close();
    held = wanted;
    qInfo("wakelock: %s", wanted ? "held (live game)" : "released");
}

// Is either followed team actually playing right now?
bool anythingLive(const QVariantMap &state)
{
    if (state.value(QStringLiteral("abstractState")).toString() == QStringLiteral("Live"))
        return true;
    for (const QVariant &tv : state.value(QStringLiteral("teams")).toList())
        for (const QVariant &gv : tv.toMap().value(QStringLiteral("games")).toList())
            if (gv.toMap().value(QStringLiteral("isLive")).toBool())
                return true;
    return false;
}

// Make sure every team on screen has its logo on disk for the frontend to load.
void ensureLogos(LogoStore *logos, const QVariantMap &state)
{
    const int ids[] = { state.value(QStringLiteral("awayId")).toInt(),
                        state.value(QStringLiteral("homeId")).toInt() };
    for (int id : ids)
        if (id > 0)
            logos->logoFor(id);

    for (const QVariant &tv : state.value(QStringLiteral("teams")).toList()) {
        const QVariantMap team = tv.toMap();
        const int own = team.value(QStringLiteral("teamId")).toInt();
        if (own > 0)
            logos->logoFor(own);
        for (const QVariant &gv : team.value(QStringLiteral("games")).toList()) {
            const int opp = gv.toMap().value(QStringLiteral("oppId")).toInt();
            if (opp > 0)
                logos->logoFor(opp);
        }
    }

    // The league-wide list can be any two of seven hundred teams.
    for (const QVariant &gv : state.value(QStringLiteral("live")).toList()) {
        const QVariantMap row = gv.toMap();
        for (const char *k : { "awayId", "homeId" }) {
            const int id = row.value(QLatin1String(k)).toInt();
            if (id > 0)
                logos->logoFor(id);
        }
    }
}

// Find the list row for an event, so a tapped game can paint the board before
// its own request comes back.
QVariantMap rowForEvent(const QVariantMap &state, const QString &eventId)
{
    for (const QVariant &tv : state.value(QStringLiteral("teams")).toList()) {
        for (const QVariant &gv : tv.toMap().value(QStringLiteral("games")).toList()) {
            const QVariantMap row = gv.toMap();
            if (row.value(QStringLiteral("eventId")).toString() == eventId)
                return row;
        }
    }
    // A game picked off the league-wide list belongs to neither season.
    for (const QVariant &gv : state.value(QStringLiteral("live")).toList()) {
        const QVariantMap row = gv.toMap();
        if (row.value(QStringLiteral("eventId")).toString() == eventId)
            return row;
    }
    return QVariantMap();
}

// Whose schedules this install shows. Kept next to the baseball app's own
// settings, one file per league, so following a different pair survives both a
// relaunch and a reinstall.
QString settingsPath(const League &league)
{
    return QStringLiteral("/home/root/.config/scoreboard/%1.json").arg(league.key);
}

QList<int> loadFollowedTeams(const League &league)
{
    QFile f(settingsPath(league));
    if (!f.open(QIODevice::ReadOnly))
        return league.teams;      // the built-in pair, first time round
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    QList<int> out;
    for (const QJsonValue &v : o.value(QStringLiteral("teams")).toArray())
        if (v.toInt() > 0)
            out.append(v.toInt());
    return out.isEmpty() ? league.teams : out;
}

void saveFollowedTeams(const League &league, const QList<int> &teams)
{
    const QString path = settingsPath(league);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("settings: cannot write %s", qPrintable(path));
        return;
    }
    QJsonArray arr;
    for (int id : teams)
        arr.append(id);
    QJsonObject o;
    o[QStringLiteral("teams")] = arr;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

// AppLoad reads icon.png once, when xochitl starts, so a new icon only shows
// up after a restart. It has to live inside the app's own directory, which is
// named for the application id.
QString launcherIconPath(const League &league)
{
    return QStringLiteral("/home/root/xovi/exthome/appload/%1/icon.png").arg(league.appId);
}

// Which league this install follows. One binary serves both, so it has to be
// told -- and it cannot be told through the environment, because AppLoad
// launches a backend with a bare one. build.sh writes a `league` file next to
// the executable; this reads it. Missing or unreadable means college football,
// which is what the first install was.
League leagueForThisInstall(const char *argv0)
{
    const QString path = QFileInfo(QString::fromLocal8Bit(argv0)).absolutePath()
                       + QStringLiteral("/league");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("league: no %s, defaulting to cfb", qPrintable(path));
        return Leagues::collegeFootball();
    }
    const QString key = QString::fromLatin1(f.readAll()).trimmed();
    qInfo("league: %s (from %s)", qPrintable(key), qPrintable(path));
    return Leagues::byKey(key);
}

} // namespace

int main(int argc, char *argv[])
{
    // The device restricts TLS 1.2 to ECDHE-ECDSA suites (SOG-IS, for EU-RED
    // certification), which is what blocks statsapi.mlb.com outright. ESPN
    // negotiates TLS 1.3 and should be unaffected, but AppLoad launches this
    // backend with a bare environment and the cost of being wrong is a board
    // that reads OFFLINE forever, so restore the default cipher list for this
    // process the same way. Must happen before anything touches OpenSSL.
    // A missing file is ignored by OpenSSL, so this is safe either way.
    qputenv("OPENSSL_CONF", "/home/root/openssl-scoreboard.cnf");

    QCoreApplication app(argc, argv);

    if (argc < 2) {
        qWarning("usage: entry <appload-socket>   (AppLoad passes this)");
        return 2;
    }

    AppLoadLink link;
    if (!link.connectTo(QString::fromLocal8Bit(argv[1])))
        return 1;

    League league = leagueForThisInstall(argv[0]);
    league.teams = loadFollowedTeams(league);

    // Has anyone actually chosen? The built-in pair is a starting point, not a
    // choice, and an app that opens straight onto somebody else's teams never
    // tells you that you can change them. The settings file is the record that
    // a pick happened; until it exists the frontend opens on the picker.
    bool teamsPicked = QFile::exists(settingsPath(league));
    qInfo("first run: %s", teamsPicked ? "no, teams already chosen"
                                       : "yes, opening on the team picker");
    qInfo("following %d teams, first is %d", int(league.teams.size()),
          league.teams.value(0));

    LogoStore logos(league);
    // The board opens on the first followed team; the other is on the list
    // either way.
    GameFeed feed(league, league.teams.value(0));

    auto push = [&]() {
        QVariantMap state = feed.state();
        ensureLogos(&logos, state);
        // Where the frontend should look, and a counter it can watch. The
        // frontend runs inside xochitl and reads these files straight off
        // disk, so the directory is part of the contract between the halves --
        // and it differs per league.
        state[QStringLiteral("logoDir")] = logos.cacheDir();
        state[QStringLiteral("logoRev")] = logos.revision();
        state[QStringLiteral("teamsPicked")] = teamsPicked;
        static const bool stayAwake =
            qEnvironmentVariableIntValue("SCOREBOARD_STAY_AWAKE") == 1;
        setWakeLock(stayAwake && anythingLive(state));
        const QByteArray json =
            QJsonDocument(QJsonObject::fromVariantMap(state)).toJson(QJsonDocument::Compact);
        link.send(MsgState, json);
    };

    QObject::connect(&feed, &GameFeed::stateChanged, &app, push);

    // A logo landing changes nothing in the state map, but the frontend needs
    // to re-check the files, so nudge it.
    QObject::connect(&logos, &LogoStore::revisionChanged, &app, push);

    QObject::connect(&link, &AppLoadLink::messageReceived, &app,
                     [&](quint32 type, const QByteArray &payload) {
        switch (type) {
        case AppLoadLink::MsgNewCoordinator:
        case MsgHello:
            // A frontend attached (or re-attached) -- it has no state yet.
            // Push what we have so the screen is never blank, then refetch:
            // after a suspend the cached state can be minutes stale.
            push();
            feed.refresh();
            break;
        case MsgShowGame: {
            const QString eventId = QString::fromLatin1(payload.trimmed());
            if (!eventId.isEmpty())
                feed.showGame(eventId, rowForEvent(feed.state(), eventId));
            break;
        }
        case MsgShowTeam:
            feed.showTeamGame();
            break;
        case MsgWatchLive:
            feed.watchLive(payload.trimmed() == "1");
            break;
        case MsgWantTeams:
            feed.loadTeamIndex();
            break;
        case MsgRefresh:
            feed.refresh();
            break;
        case MsgSetTeams: {
            QList<int> ids;
            for (const QByteArray &part : payload.trimmed().split(',')) {
                const int id = part.trimmed().toInt();
                if (id > 0 && !ids.contains(id))
                    ids.append(id);
            }
            if (ids.isEmpty())
                break;
            // Act on the pick even when it matches what is already followed:
            // guarding this on "did it change" means choosing the team you
            // already have silently does nothing, no saved file and no icon,
            // which just looks broken.
            saveFollowedTeams(league, ids);
            teamsPicked = true;
            logos.writeLauncherIcon(ids.first(), launcherIconPath(league));
            feed.setTeams(ids);   // itself a no-op if unchanged
            break;
        }
        case MsgGeometry:
            qInfo("frontend window: %s", payload.constData());
            break;
        case AppLoadLink::MsgTerminate:
            qInfo("backend: AppLoad asked us to terminate");
            app.quit();
            break;
        default:
            break;
        }
    });

    QObject::connect(&link, &AppLoadLink::disconnected, &app, [&]() {
        qInfo("backend: link closed, exiting");
        feed.watchLive(false);
        setWakeLock(false);   // never leave the tablet pinned awake
        app.quit();
    });

    push();
    return app.exec();
}
