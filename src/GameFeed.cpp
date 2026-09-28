#include "GameFeed.h"
#include "EspnUserAgent.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QTime>
#include <QTimeZone>
#include <QLocale>
#include <QRegularExpression>
#include <QSslSocket>
#include <QUrl>
#include <algorithm>

namespace {
// A live game refreshes fast; a season schedule barely changes, and a rank or
// a record changes once a week.
constexpr int kPollMs     = 15 * 1000;
constexpr int kScheduleMs = 10 * 60 * 1000;
constexpr int kTeamMs     = 30 * 60 * 1000;
// The league-wide list is expensive, so it refreshes slowly and only while it
// is on screen. The game you pick out of it is polled at kPollMs like any
// other, and that request is 23KB.
constexpr int kLiveMs     = 60 * 1000;

// A request that never answers has to fail eventually, or the poll behind it
// has nothing to retry. Qt sets no transfer timeout by default.
constexpr int kTimeoutMs  = 20 * 1000;

// More wall-clock than this between two polls means the tablet was asleep,
// not that a request was slow.
constexpr int kSleptSecs  = 90;

// ESPN stamps every date in UTC and the tablet's clock is UTC too
// (/etc/localtime -> Universal), so a Saturday night kickoff is already
// "Sunday" to the device. Everything shown to the reader is converted to
// Eastern, which is the zone a college football week is keyed to.
QDateTime easternNow()
{
    const QTimeZone eastern("America/New_York");
    const QDateTime utc = QDateTime::currentDateTimeUtc();
    return eastern.isValid() ? utc.toTimeZone(eastern)
                             : utc.addSecs(-5 * 60 * 60);
}

QDateTime toEastern(const QDateTime &utc)
{
    const QTimeZone eastern("America/New_York");
    return eastern.isValid() ? utc.toTimeZone(eastern) : utc.addSecs(-5 * 60 * 60);
}

// ESPN's dates are "2026-09-19T16:00Z" -- ISO 8601 with minute precision and a
// bare Z, which Qt::ISODate reads as UTC. Every date in every payload carries
// the Z, so there is nothing to fix up; a zone-less string would come back as
// local time, which is only a problem off-device, since the tablet's clock is
// UTC anyway.
QDateTime eventDate(const QJsonObject &ev)
{
    return QDateTime::fromString(ev.value(QStringLiteral("date")).toString(),
                                 Qt::ISODate);
}

QJsonObject competition(const QJsonObject &ev)
{
    const QJsonArray comps = ev.value(QStringLiteral("competitions")).toArray();
    return comps.isEmpty() ? QJsonObject() : comps.first().toObject();
}

// The status hangs off the competition in both the schedule and the
// single-event payload, but only off the event in some older shapes.
QJsonObject statusOf(const QJsonObject &ev, const QJsonObject &comp)
{
    const QJsonObject s = comp.value(QStringLiteral("status")).toObject();
    return s.isEmpty() ? ev.value(QStringLiteral("status")).toObject() : s;
}

// "pre" | "in" | "post" -> the three words the whole UI switches on.
QString abstractState(const QJsonObject &status)
{
    const QString st = status.value(QStringLiteral("type")).toObject()
                             .value(QStringLiteral("state")).toString();
    if (st == QStringLiteral("in"))   return QStringLiteral("Live");
    if (st == QStringLiteral("post")) return QStringLiteral("Final");
    return QStringLiteral("Preview");
}

// A competitor's score is a string in the single-event payload ("70") and an
// object in a team's schedule ({"value":70,"displayValue":"70"}), and absent
// altogether before kickoff. One reader for all three.
QVariant scoreOf(const QJsonObject &competitor)
{
    const QJsonValue v = competitor.value(QStringLiteral("score"));
    if (v.isObject())
        return QVariant(qRound(v.toObject().value(QStringLiteral("value")).toDouble()));
    if (v.isString()) {
        bool ok = false;
        const int n = v.toString().toInt(&ok);
        return ok ? QVariant(n) : QVariant(QString());
    }
    if (v.isDouble())
        return QVariant(qRound(v.toDouble()));
    return QVariant(QString());
}

// 99 is ESPN's "not ranked", not a 99th-best team.
int rankOf(const QJsonObject &competitor)
{
    const int r = competitor.value(QStringLiteral("curatedRank")).toObject()
                            .value(QStringLiteral("current")).toInt();
    return (r > 0 && r < 99) ? r : 0;
}

QString recordOf(const QJsonObject &competitor)
{
    for (const QJsonValue &rv : competitor.value(QStringLiteral("records")).toArray()) {
        const QJsonObject r = rv.toObject();
        if (r.value(QStringLiteral("type")).toString() == QStringLiteral("total"))
            return r.value(QStringLiteral("summary")).toString();
    }
    return QString();
}

QString ordinal(int n)
{
    switch (n) {
    case 1:  return QStringLiteral("1ST");
    case 2:  return QStringLiteral("2ND");
    case 3:  return QStringLiteral("3RD");
    default: return QStringLiteral("%1TH").arg(n);
    }
}

// Which quarter, or which overtime. Football has no half-innings, so the
// period and the clock are the whole of "where are we".
QString periodLabel(const QJsonObject &status)
{
    const QJsonObject type = status.value(QStringLiteral("type")).toObject();
    const QString name = type.value(QStringLiteral("name")).toString();
    const int period = status.value(QStringLiteral("period")).toInt();

    if (name.contains(QStringLiteral("HALFTIME")))
        return QStringLiteral("HALFTIME");
    if (name.contains(QStringLiteral("END_OF_PERIOD")))
        return QStringLiteral("END %1").arg(ordinal(period));
    if (period <= 0)
        return QString();
    if (period <= 4)
        return ordinal(period);
    return period == 5 ? QStringLiteral("OT") : QStringLiteral("%1OT").arg(period - 4);
}

// Overtime is worth calling out on a finished game; four quarters is not.
QString finalLabel(const QJsonObject &status)
{
    const int period = status.value(QStringLiteral("period")).toInt();
    if (period <= 4)
        return QStringLiteral("FINAL");
    return period == 5 ? QStringLiteral("FINAL/OT")
                       : QStringLiteral("FINAL/%1OT").arg(period - 4);
}

// Fixture mode maps a request URL onto a file name: everything after
// /college-football/ with each run of punctuation folded to a dash. So
//   teams/194/schedule   -> teams-194-schedule.json
//   scoreboard/401858454 -> scoreboard-401858454.json
// tools/fixtures.sh captures a set using exactly this rule.
QString fixtureName(const QString &url)
{
    QString tail = url;
    const int cut = tail.indexOf(QStringLiteral("/college-football/"));
    if (cut >= 0)
        tail = tail.mid(cut + 18);
    tail.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9]+")),
                 QStringLiteral("-"));
    return tail + QStringLiteral(".json");
}

QString broadcastOf(const QJsonObject &comp)
{
    // Three shapes in the wild: a plain string, {names:[...]}, and
    // {media:{shortName}}. Take whichever is there.
    const QString flat = comp.value(QStringLiteral("broadcast")).toString();
    if (!flat.isEmpty())
        return flat;
    for (const QJsonValue &bv : comp.value(QStringLiteral("broadcasts")).toArray()) {
        const QJsonObject b = bv.toObject();
        const QJsonArray names = b.value(QStringLiteral("names")).toArray();
        if (!names.isEmpty())
            return names.first().toString();
        const QString media = b.value(QStringLiteral("media")).toObject()
                               .value(QStringLiteral("shortName")).toString();
        if (!media.isEmpty())
            return media;
    }
    return QString();
}
} // namespace

GameFeed::GameFeed(const League &league, int teamId, const QString &demoState,
                   QObject *parent)
    : QObject(parent), m_league(league), m_teamId(teamId)
{
    m_state[QStringLiteral("hasGame")]    = false;
    m_state[QStringLiteral("statusText")] = QStringLiteral("Loading");
    m_state[QStringLiteral("error")]      = QString();
    // Until the first reply lands we know nothing. Without this the UI cannot
    // tell "still asking" from "asked, and there is genuinely no game".
    m_state[QStringLiteral("loaded")]     = false;
    m_state[QStringLiteral("followId")]   = teamId;
    m_state[QStringLiteral("league")]     = league.key;
    publishFollowed();
    m_state[QStringLiteral("liveLoaded")] = false;

    if (!demoState.isEmpty()) {
        loadDemoGame(demoState);
        return;
    }

    // Measured on the device: after a fifteen minute suspend, a reused
    // keep-alive socket sat ESTABLISHED with 1761 bytes stuck in its send
    // queue, retransmitting into a connection whose far end was long gone.
    // Without a timeout that request hung forever, every later poll queued
    // behind it, and the board simply stopped updating -- which is exactly
    // what "it never refreshes after it sleeps" looks like from outside.
    m_net.setTransferTimeout(kTimeoutMs);

    qInfo("league: %s  api=%s  teams=%s",
          qPrintable(m_league.key), qPrintable(m_league.api()),
          qPrintable([this] {
              QStringList ids;
              for (int id : m_league.teams) ids << QString::number(id);
              return ids.join(QLatin1Char(','));
          }()));
    qInfo("tls: supportsSsl=%d build=%s runtime=%s",
          QSslSocket::supportsSsl(),
          qPrintable(QSslSocket::sslLibraryBuildVersionString()),
          qPrintable(QSslSocket::sslLibraryVersionString()));

    connect(&m_pollTimer, &WakeTimer::timeout, this, &GameFeed::refresh);
    m_pollTimer.start(kPollMs);

    connect(&m_scheduleTimer, &WakeTimer::timeout, this, [this]() {
        for (int id : trackedTeams())
            requestSchedule(id);
    });
    m_scheduleTimer.start(kScheduleMs);

    connect(&m_teamTimer, &WakeTimer::timeout, this, [this]() {
        for (int id : trackedTeams())
            requestTeam(id);
    });
    m_teamTimer.start(kTeamMs);

    // Started and stopped by watchLive(), never on its own.
    connect(&m_liveTimer, &WakeTimer::timeout, this, &GameFeed::requestLiveList);

    for (int id : trackedTeams()) {
        requestSchedule(id);
        requestTeam(id);
    }
}

void GameFeed::get(const QString &url, std::function<void(const QJsonObject &)> cb)
{
    // Fixture mode: read a saved payload off disk instead of asking ESPN.
    // Two reasons this earns its keep. The SDK's host Qt ships no TLS plugin,
    // so a probe built in the container cannot make an HTTPS request at all;
    // and college football is played on one day a week, so the situation block
    // -- down, distance, possession, last play -- simply does not exist in any
    // payload you can fetch on a Tuesday. A saved live payload is the only way
    // to exercise that path before kickoff.
    static const QByteArray fixtures = qgetenv("CFB_FIXTURES");
    if (!fixtures.isEmpty()) {
        const QString path = QString::fromLocal8Bit(fixtures)
                           + QLatin1Char('/') + fixtureName(url);
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            qWarning("fixture missing: %s", qPrintable(path));
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isObject()) {
            qWarning("fixture is not a JSON object: %s", qPrintable(path));
            return;
        }
        qInfo("fixture: %s", qPrintable(fixtureName(url)));
        m_state[QStringLiteral("error")] = QString();
        cb(doc.object());
        return;
    }

    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("User-Agent", Espn::kUserAgent);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_net.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, cb, url]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qWarning("request failed: %s -> %s",
                     qPrintable(url), qPrintable(reply->errorString()));
            m_state[QStringLiteral("error")] = reply->errorString();
            publish();
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject())
            return;
        m_state[QStringLiteral("error")] = QString();
        cb(doc.object());
    });
}

void GameFeed::refresh()
{
    // TCP connections do not survive the tablet sleeping, but Qt does not know
    // that and will reuse one from its keep-alive pool. If more time has
    // passed than a poll interval can explain, assume we were asleep and throw
    // the pool away rather than write into a dead socket.
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (m_lastRefresh > 0 && (now - m_lastRefresh) > kSleptSecs) {
        qInfo("woke after %lld s -- dropping stale connections",
              static_cast<long long>(now - m_lastRefresh));
        m_net.clearConnectionCache();
    }
    m_lastRefresh = now;


    // One request per game that matters: the one on the board, plus each
    // followed team's current game if it is live and not already on the board.
    // Everything else on the list is a date and a final score, and neither
    // moves.
    QSet<QString> wanted;
    if (!m_eventId.isEmpty()) {
        requestEvent(m_eventId, true);
        wanted.insert(m_eventId);
    }
    for (int id : trackedTeams()) {
        const QString ev = m_current.value(id);
        if (ev.isEmpty() || wanted.contains(ev))
            continue;
        for (const QVariant &rv : m_season.value(id)) {
            const QVariantMap row = rv.toMap();
            if (row.value(QStringLiteral("eventId")).toString() != ev)
                continue;
            if (row.value(QStringLiteral("isLive")).toBool()) {
                requestEvent(ev, false);
                wanted.insert(ev);
            }
            break;
        }
    }
    if (m_eventId.isEmpty()) {
        pickCurrentEvent();
        // Still nothing: the schedules never arrived, so there is no game to
        // follow and nothing above asked for anything. Ask again rather than
        // waiting out the ten minute schedule timer.
        if (m_eventId.isEmpty() && m_season.isEmpty()) {
            for (int id : m_league.teams) {
                requestSchedule(id);
                requestTeam(id);
            }
        }
    }
}

namespace {
// One row of a team's season, from either payload shape. The row carries
// enough of the matchup that tapping it can paint the board immediately,
// before the single-event request for it comes back.
QVariantMap rowFor(int teamId, const QJsonObject &ev)
{
    const QJsonObject comp   = competition(ev);
    const QJsonObject status = statusOf(ev, comp);
    const QString abs        = abstractState(status);

    QJsonObject me, opp;
    for (const QJsonValue &cv : comp.value(QStringLiteral("competitors")).toArray()) {
        const QJsonObject c = cv.toObject();
        // Ids are strings throughout ESPN's JSON, including numeric ones.
        if (c.value(QStringLiteral("team")).toObject()
             .value(QStringLiteral("id")).toString().toInt() == teamId)
            me = c;
        else
            opp = c;
    }

    const QJsonObject away = (me.value(QStringLiteral("homeAway")).toString()
                              == QStringLiteral("away")) ? me : opp;
    const QJsonObject home = (away == me) ? opp : me;
    auto team = [](const QJsonObject &c) {
        return c.value(QStringLiteral("team")).toObject();
    };

    const QDateTime utc = eventDate(ev);
    const QDateTime et  = utc.isValid() ? toEastern(utc) : QDateTime();
    const bool timeKnown = ev.value(QStringLiteral("timeValid")).toBool(
        comp.value(QStringLiteral("timeValid")).toBool(true));

    QVariantMap row;
    row[QStringLiteral("eventId")] = ev.value(QStringLiteral("id")).toString();
    row[QStringLiteral("bye")]     = false;

    const QJsonObject week = ev.value(QStringLiteral("week")).toObject();
    const int weekNo = week.value(QStringLiteral("number")).toInt();
    row[QStringLiteral("weekNo")] = weekNo;
    row[QStringLiteral("week")]   = weekNo > 0 ? QStringLiteral("WK %1").arg(weekNo)
                                               : QString();

    row[QStringLiteral("date")] = et.isValid()
        ? QLocale::c().toString(et.date(), QStringLiteral("MMM d")).toUpper()
        : QString();
    row[QStringLiteral("kick")] = (et.isValid() && timeKnown)
        ? et.toString(QStringLiteral("h:mm AP")) : QStringLiteral("TBD");

    row[QStringLiteral("home")]    = (me.value(QStringLiteral("homeAway")).toString()
                                      == QStringLiteral("home"));
    row[QStringLiteral("oppId")]   = team(opp).value(QStringLiteral("id")).toString().toInt();
    row[QStringLiteral("oppAbbr")] = team(opp).value(QStringLiteral("abbreviation")).toString();
    row[QStringLiteral("oppName")] = team(opp).value(QStringLiteral("shortDisplayName")).toString();
    row[QStringLiteral("oppRank")] = rankOf(opp);
    row[QStringLiteral("myRank")]  = rankOf(me);

    const QVariant myScore  = scoreOf(me);
    const QVariant oppScore = scoreOf(opp);
    row[QStringLiteral("teamScore")] = myScore;
    row[QStringLiteral("oppScore")]  = oppScore;

    row[QStringLiteral("isLive")]  = (abs == QStringLiteral("Live"));
    row[QStringLiteral("isFinal")] = (abs == QStringLiteral("Final"));
    row[QStringLiteral("won")]     = me.value(QStringLiteral("winner")).toBool();

    // What the row says on its right: the result, the game clock, or kickoff.
    if (abs == QStringLiteral("Final")) {
        const int a = myScore.toInt(), b = oppScore.toInt();
        const QString mark = (a > b) ? QStringLiteral("W")
                           : (a < b) ? QStringLiteral("L") : QStringLiteral("T");
        row[QStringLiteral("note")] = QStringLiteral("%1 %2-%3").arg(mark).arg(a).arg(b);
    } else if (abs == QStringLiteral("Live")) {
        const QString p = periodLabel(status);
        const QString c = status.value(QStringLiteral("displayClock")).toString();
        row[QStringLiteral("note")] = (p == QStringLiteral("HALFTIME") || c.isEmpty())
            ? p : QStringLiteral("%1  %2").arg(p, c);
    } else {
        row[QStringLiteral("note")] = row.value(QStringLiteral("kick")).toString();
    }

    // Carried so a tapped row paints the board before its own request lands.
    row[QStringLiteral("awayId")]     = team(away).value(QStringLiteral("id")).toString().toInt();
    row[QStringLiteral("homeId")]     = team(home).value(QStringLiteral("id")).toString().toInt();
    row[QStringLiteral("awayAbbr")]   = team(away).value(QStringLiteral("abbreviation")).toString();
    row[QStringLiteral("homeAbbr")]   = team(home).value(QStringLiteral("abbreviation")).toString();
    row[QStringLiteral("awayName")]   = team(away).value(QStringLiteral("shortDisplayName")).toString();
    row[QStringLiteral("homeName")]   = team(home).value(QStringLiteral("shortDisplayName")).toString();
    row[QStringLiteral("awayScore")]  = scoreOf(away);
    row[QStringLiteral("homeScore")]  = scoreOf(home);
    row[QStringLiteral("awayRank")]   = rankOf(away);
    row[QStringLiteral("homeRank")]   = rankOf(home);
    row[QStringLiteral("venue")]      = comp.value(QStringLiteral("venue")).toObject()
                                           .value(QStringLiteral("fullName")).toString();
    row[QStringLiteral("statusText")] = status.value(QStringLiteral("type")).toObject()
                                              .value(QStringLiteral("detail")).toString();
    row[QStringLiteral("abstract")]   = abs;
    row[QStringLiteral("startsAt")]   = utc.isValid() ? utc.toMSecsSinceEpoch() : 0;
    return row;
}
} // namespace

namespace {
// One game as a matchup, for the league-wide list. rowFor() above is written
// from a followed team's point of view -- "at #4 TEX", "W 56-3" -- which makes
// no sense for a game neither team is in, so this one is plain away-at-home.
QVariantMap matchupRow(const QJsonObject &ev, const QList<int> &followed)
{
    const QJsonObject comp   = competition(ev);
    const QJsonObject status = statusOf(ev, comp);
    const QString abs        = abstractState(status);

    QJsonObject away, home;
    for (const QJsonValue &cv : comp.value(QStringLiteral("competitors")).toArray()) {
        const QJsonObject c = cv.toObject();
        if (c.value(QStringLiteral("homeAway")).toString() == QStringLiteral("away"))
            away = c;
        else
            home = c;
    }
    auto team = [](const QJsonObject &c) {
        return c.value(QStringLiteral("team")).toObject();
    };

    const QDateTime utc = eventDate(ev);
    const QDateTime et  = utc.isValid() ? toEastern(utc) : QDateTime();

    QVariantMap row;
    row[QStringLiteral("eventId")] = ev.value(QStringLiteral("id")).toString();
    row[QStringLiteral("isLive")]  = (abs == QStringLiteral("Live"));
    row[QStringLiteral("isFinal")] = (abs == QStringLiteral("Final"));
    row[QStringLiteral("abstract")] = abs;
    row[QStringLiteral("startsAt")] = utc.isValid() ? utc.toMSecsSinceEpoch() : 0;

    for (const auto &side : { qMakePair(away, QStringLiteral("away")),
                              qMakePair(home, QStringLiteral("home")) }) {
        const QJsonObject c = side.first;
        const QString p = side.second;
        row[p + QStringLiteral("Id")]    = team(c).value(QStringLiteral("id")).toString().toInt();
        row[p + QStringLiteral("Abbr")]  = team(c).value(QStringLiteral("abbreviation")).toString();
        row[p + QStringLiteral("Name")]  = team(c).value(QStringLiteral("shortDisplayName")).toString();
        row[p + QStringLiteral("Score")] = scoreOf(c);
        row[p + QStringLiteral("Rank")]  = rankOf(c);
    }

    // The status line the card carries: the clock, the result, or kickoff.
    if (abs == QStringLiteral("Live")) {
        const QString pl = periodLabel(status);
        const QString ck = status.value(QStringLiteral("displayClock")).toString();
        row[QStringLiteral("note")] = (pl == QStringLiteral("HALFTIME") || ck.isEmpty())
            ? pl : QStringLiteral("%1  %2").arg(pl, ck);
    } else if (abs == QStringLiteral("Final")) {
        row[QStringLiteral("note")] = finalLabel(status);
    } else {
        row[QStringLiteral("note")] = (et.isValid()
                                       && ev.value(QStringLiteral("timeValid")).toBool(true))
            ? et.toString(QStringLiteral("h:mm AP")) : QStringLiteral("TBD");
    }

    // Possession and down/distance, so a live card says who has the ball
    // without opening the game.
    const QJsonObject sit = comp.value(QStringLiteral("situation")).toObject();
    int possId = sit.value(QStringLiteral("possession")).toString().toInt();
    if (possId <= 0)
        possId = sit.value(QStringLiteral("possession")).toInt();
    row[QStringLiteral("possessionId")] = possId;
    QString dd = sit.value(QStringLiteral("shortDownDistanceText")).toString();
    if (dd.isEmpty()) {
        const int down = sit.value(QStringLiteral("down")).toInt();
        const int dist = sit.value(QStringLiteral("distance")).toInt();
        if (down > 0)
            dd = (dist == 0) ? QStringLiteral("%1 & GOAL").arg(ordinal(down))
                             : QStringLiteral("%1 & %2").arg(ordinal(down)).arg(dist);
    }
    row[QStringLiteral("downDistance")] = dd.toUpper();

    row[QStringLiteral("venue")]      = comp.value(QStringLiteral("venue")).toObject()
                                           .value(QStringLiteral("fullName")).toString();
    row[QStringLiteral("statusText")] = status.value(QStringLiteral("type")).toObject()
                                              .value(QStringLiteral("detail")).toString();

    // Mark the followed teams so their game stands out in a list of seventy.
    const int a = row.value(QStringLiteral("awayId")).toInt();
    const int h = row.value(QStringLiteral("homeId")).toInt();
    row[QStringLiteral("followed")] = followed.contains(a) || followed.contains(h);
    return row;
}
} // namespace

void GameFeed::publishFollowed()
{
    QVariantList ids;
    for (int id : m_league.teams)
        ids.append(id);
    m_state[QStringLiteral("followed")] = ids;
}

void GameFeed::setTeams(const QList<int> &teams)
{
    QList<int> wanted;
    for (int id : teams)
        if (id > 0 && !wanted.contains(id))
            wanted.append(id);
    if (wanted.isEmpty() || wanted == m_league.teams)
        return;

    qInfo("following: %s", qPrintable([&wanted] {
        QStringList out;
        for (int id : wanted) out << QString::number(id);
        return out.join(QLatin1Char(','));
    }()));

    m_league.teams = wanted;
    m_teamId = wanted.first();

    // Everything on screen belonged to the old pair.
    m_season.clear();
    m_current.clear();
    m_pinned = false;
    m_eventId.clear();
    m_state[QStringLiteral("hasGame")] = false;
    m_state[QStringLiteral("periods")] = QVariantList();
    m_state[QStringLiteral("leaders")] = QVariantList();
    publishFollowed();
    publishSlate();
    publish();

    for (int id : m_league.teams) {
        requestSchedule(id);
        requestTeam(id);
    }
}

void GameFeed::loadTeamIndex()
{
    if (!m_state.value(QStringLiteral("allTeams")).toList().isEmpty())
        return;

    const QString path = QDir::homePath() + QStringLiteral("/.cache/")
                       + m_league.key + QStringLiteral("-teams.json");

    // Straight off disk if it has ever been fetched. Teams do not move between
    // leagues, and this is the only request in the app measured in megabytes.
    QFile cached(path);
    if (cached.open(QIODevice::ReadOnly)) {
        const QJsonArray arr = QJsonDocument::fromJson(cached.readAll()).array();
        if (!arr.isEmpty()) {
            m_state[QStringLiteral("allTeams")] = arr.toVariantList();
            publish();
            return;
        }
    }

    get(m_league.api() + QStringLiteral("/teams?limit=1000"), [this, path](const QJsonObject &o) {
        const QJsonArray leagues = o.value(QStringLiteral("sports")).toArray().isEmpty()
            ? QJsonArray()
            : o.value(QStringLiteral("sports")).toArray().first().toObject()
               .value(QStringLiteral("leagues")).toArray();
        if (leagues.isEmpty())
            return;

        QJsonArray out;
        for (const QJsonValue &tv : leagues.first().toObject()
                                    .value(QStringLiteral("teams")).toArray()) {
            const QJsonObject t = tv.toObject().value(QStringLiteral("team")).toObject();
            const int id = t.value(QStringLiteral("id")).toString().toInt();
            if (id <= 0)
                continue;
            QString name = t.value(QStringLiteral("shortDisplayName")).toString();
            if (name.isEmpty())
                name = t.value(QStringLiteral("displayName")).toString();
            QJsonObject row;
            row[QStringLiteral("id")]   = id;
            row[QStringLiteral("abbr")] = t.value(QStringLiteral("abbreviation")).toString();
            row[QStringLiteral("name")] = name;
            out.append(row);
        }
        if (out.isEmpty())
            return;

        // 762 teams trim to about 33KB, which is worth keeping.
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(out).toJson(QJsonDocument::Compact));
        else
            qWarning("team index: cannot write %s", qPrintable(path));

        m_state[QStringLiteral("allTeams")] = out.toVariantList();
        publish();
    });
}

void GameFeed::watchLive(bool on)
{
    if (on) {
        requestLiveList();
        m_liveTimer.start(kLiveMs);
    } else {
        m_liveTimer.stop();
    }
}

void GameFeed::requestLiveList()
{
    // Keyed to the Eastern date, like everything else: a 10:30pm kickoff on the
    // west coast is still Saturday's game at 1am Sunday on a UTC tablet.
    const QDateTime et = easternNow();
    QDate day = et.date();
    if (et.time().hour() < 6)
        day = day.addDays(-1);

    const QString url = QStringLiteral("%1/scoreboard?dates=%2")
                            .arg(m_league.api(),
                                 day.toString(QStringLiteral("yyyyMMdd")));

    get(url, [this, day](const QJsonObject &o) {
        QVariantList live, other;
        for (const QJsonValue &ev : o.value(QStringLiteral("events")).toArray()) {
            const QVariantMap row = matchupRow(ev.toObject(), m_league.teams);
            if (row.value(QStringLiteral("isLive")).toBool())
                live.append(row);
            else
                other.append(row);
        }

        auto byStart = [](const QVariant &a, const QVariant &b) {
            return a.toMap().value(QStringLiteral("startsAt")).toLongLong()
                 < b.toMap().value(QStringLiteral("startsAt")).toLongLong();
        };
        std::sort(live.begin(), live.end(), byStart);
        std::sort(other.begin(), other.end(), byStart);

        // Games in progress are the point of the page. If there are none --
        // which on any given Tuesday is the answer -- show what else is on
        // today rather than an empty page, capped so a Saturday's seventy
        // games do not run off the bottom.
        const int kFill = 12;
        QVariantList out = live;
        for (const QVariant &row : other) {
            if (out.size() >= live.size() + kFill)
                break;
            out.append(row);
        }

        m_state[QStringLiteral("live")]      = out;
        m_state[QStringLiteral("liveCount")] = int(live.size());
        m_state[QStringLiteral("dayCount")]  = int(live.size() + other.size());
        m_state[QStringLiteral("liveDate")]  =
            QLocale::c().toString(day, QStringLiteral("dddd, MMMM d")).toUpper();
        m_state[QStringLiteral("liveLoaded")] = true;
        publish();
    });
}

void GameFeed::requestSchedule(int teamId)
{
    const QString url = QStringLiteral("%1/teams/%2/schedule")
                            .arg(m_league.api()).arg(teamId);

    get(url, [this, teamId](const QJsonObject &o) {
        QVariantList rows;
        for (const QJsonValue &ev : o.value(QStringLiteral("events")).toArray())
            rows.append(rowFor(teamId, ev.toObject()));

        // ESPN lists events in date order, but a postponed game can land out
        // of place and the list is the reader's mental model of the season.
        std::sort(rows.begin(), rows.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value(QStringLiteral("startsAt")).toLongLong()
                 < b.toMap().value(QStringLiteral("startsAt")).toLongLong();
        });

        // A bye is a real part of a college season and the schedule reads wrong
        // without it -- week 8 simply missing looks like a parsing bug. Fill
        // the gaps between the first and last regular-season week.
        QVariantList withByes;
        int expected = 0;
        for (const QVariant &rv : rows) {
            const QVariantMap row = rv.toMap();
            const int wk = row.value(QStringLiteral("weekNo")).toInt();
            if (expected > 0 && wk > expected) {
                for (int missing = expected; missing < wk; ++missing) {
                    QVariantMap bye;
                    bye[QStringLiteral("bye")]  = true;
                    bye[QStringLiteral("week")] = QStringLiteral("WK %1").arg(missing);
                    bye[QStringLiteral("note")] = QStringLiteral("BYE");
                    withByes.append(bye);
                }
            }
            withByes.append(row);
            if (wk > 0)
                expected = wk + 1;
        }

        m_season.insert(teamId, withByes);
        m_state[QStringLiteral("loaded")] = true;
        pickCurrentEvent();
        publishSlate();
        publish();
    });
}

void GameFeed::requestTeam(int teamId)
{
    const QString url = QStringLiteral("%1/teams/%2")
                            .arg(m_league.api()).arg(teamId);

    get(url, [this, teamId](const QJsonObject &o) {
        const QJsonObject t = o.value(QStringLiteral("team")).toObject();
        if (t.isEmpty())
            return;

        TeamInfo info;
        info.name  = t.value(QStringLiteral("shortDisplayName")).toString();
        info.abbr  = t.value(QStringLiteral("abbreviation")).toString();
        const int rank = t.value(QStringLiteral("rank")).toInt();
        info.rank  = (rank > 0 && rank < 99) ? rank : 0;
        info.standing = t.value(QStringLiteral("standingSummary")).toString();
        for (const QJsonValue &iv : t.value(QStringLiteral("record")).toObject()
                                     .value(QStringLiteral("items")).toArray()) {
            const QJsonObject item = iv.toObject();
            if (item.value(QStringLiteral("type")).toString() == QStringLiteral("total")) {
                info.record = item.value(QStringLiteral("summary")).toString();
                break;
            }
        }

        m_team.insert(teamId, info);
        applyTeamInfo();
        publishSlate();
        publish();
    });
}

// The board opens on the followed team's game: whatever is being played, or
// the one just finished, or the next one up. Nothing else is a sensible answer
// to "show me my team" on a Tuesday.
void GameFeed::pickCurrentEvent()
{
    const qint64 now = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    constexpr qint64 kRecentMs = 3LL * 24 * 60 * 60 * 1000;   // three days

    for (int teamId : trackedTeams()) {
        QString live, recent, next;
        qint64 recentAt = 0, nextAt = 0;

        for (const QVariant &rv : m_season.value(teamId)) {
            const QVariantMap row = rv.toMap();
            if (row.value(QStringLiteral("bye")).toBool())
                continue;
            const QString id = row.value(QStringLiteral("eventId")).toString();
            const qint64 at  = row.value(QStringLiteral("startsAt")).toLongLong();

            if (row.value(QStringLiteral("isLive")).toBool()) {
                live = id;
            } else if (row.value(QStringLiteral("isFinal")).toBool()) {
                if (at >= recentAt) { recentAt = at; recent = id; }
            } else if (at > now && (nextAt == 0 || at < nextAt)) {
                nextAt = at; next = id;
            }
        }

        // A game finished more than three days ago is history; the next one is
        // the thing the reader wants. Within three days it is still "the game".
        QString pick = live;
        if (pick.isEmpty() && !recent.isEmpty() && (now - recentAt) < kRecentMs)
            pick = recent;
        if (pick.isEmpty())
            pick = next.isEmpty() ? recent : next;
        m_current.insert(teamId, pick);
    }

    // Only move the board if the reader has not pinned a game from the list.
    if (m_pinned)
        return;
    const QString pick = m_current.value(m_teamId);
    if (pick.isEmpty() || pick == m_eventId)
        return;
    m_eventId = pick;
    requestEvent(m_eventId, true);
}

void GameFeed::requestEvent(const QString &eventId, bool isBoard)
{
    if (eventId.isEmpty())
        return;

    // The single-event form of /scoreboard. The full scoreboard for college
    // football is 1.3MB because there are 700-odd teams in it; this is 23KB
    // and carries the same fields, which is what makes a 15 second poll sane.
    const QString url = QStringLiteral("%1/scoreboard/%2")
                            .arg(m_league.api(), eventId);

    get(url, [this, eventId, isBoard](const QJsonObject &ev) {
        // Keep every row for this game current, whichever team's season it is
        // in, so a live game on the list ticks along even while the board is
        // showing the other one.
        for (int teamId : trackedTeams()) {
            QVariantList season = m_season.value(teamId);
            bool touched = false;
            for (int i = 0; i < season.size(); ++i) {
                QVariantMap row = season.at(i).toMap();
                if (row.value(QStringLiteral("eventId")).toString() != eventId)
                    continue;
                season[i] = rowFor(teamId, ev);
                touched = true;
                break;
            }
            if (touched)
                m_season.insert(teamId, season);
        }
        publishSlate();

        if (!isBoard || eventId != m_eventId) {
            publish();
            return;
        }

        const QJsonObject comp   = competition(ev);
        const QJsonObject status = statusOf(ev, comp);
        const QJsonObject type   = status.value(QStringLiteral("type")).toObject();
        const QString abs        = abstractState(status);

        QJsonObject away, home;
        for (const QJsonValue &cv : comp.value(QStringLiteral("competitors")).toArray()) {
            const QJsonObject c = cv.toObject();
            if (c.value(QStringLiteral("homeAway")).toString() == QStringLiteral("away"))
                away = c;
            else
                home = c;
        }
        auto team = [](const QJsonObject &c) {
            return c.value(QStringLiteral("team")).toObject();
        };

        m_state[QStringLiteral("hasGame")]       = true;
        m_state[QStringLiteral("eventId")]       = eventId;
        m_state[QStringLiteral("abstractState")] = abs;
        m_state[QStringLiteral("statusText")]    = type.value(QStringLiteral("detail")).toString();
        m_state[QStringLiteral("finalLabel")]    = finalLabel(status);
        m_state[QStringLiteral("periodLabel")]   = periodLabel(status);
        m_state[QStringLiteral("clock")]         = status.value(QStringLiteral("displayClock")).toString();
        m_state[QStringLiteral("venue")]         = comp.value(QStringLiteral("venue")).toObject()
                                                      .value(QStringLiteral("fullName")).toString();
        m_state[QStringLiteral("broadcast")]     = broadcastOf(comp);

        const QJsonArray notes = comp.value(QStringLiteral("notes")).toArray();
        m_state[QStringLiteral("note")] = notes.isEmpty()
            ? QString()
            : notes.first().toObject().value(QStringLiteral("headline")).toString();

        auto side = [&](const QJsonObject &c, const char *prefix) {
            const QString p = QString::fromLatin1(prefix);
            m_state[p + QStringLiteral("Id")]     = team(c).value(QStringLiteral("id")).toString().toInt();
            m_state[p + QStringLiteral("Abbr")]   = team(c).value(QStringLiteral("abbreviation")).toString();
            m_state[p + QStringLiteral("Name")]   = team(c).value(QStringLiteral("shortDisplayName")).toString();
            m_state[p + QStringLiteral("Score")]  = scoreOf(c);
            m_state[p + QStringLiteral("Rank")]   = rankOf(c);
            m_state[p + QStringLiteral("Record")] = recordOf(c);
        };
        side(away, "away");
        side(home, "home");
        applyTeamInfo();

        // ---- situation: who has the ball, and where ------------------------
        // Present only while a game is being played; ESPN drops the whole
        // object between games, so everything under it has to be cleared too
        // or the board keeps showing a down and distance after the final.
        const QJsonObject sit = comp.value(QStringLiteral("situation")).toObject();
        if (sit.isEmpty()) {
            m_state[QStringLiteral("possessionId")] = 0;
            m_state[QStringLiteral("downDistance")] = QString();
            m_state[QStringLiteral("fieldPos")]     = QString();
            m_state[QStringLiteral("ballOn")]       = -1;
            m_state[QStringLiteral("toGo")]         = 0;
            m_state[QStringLiteral("isRedZone")]    = false;
            m_state[QStringLiteral("awayTimeouts")] = -1;
            m_state[QStringLiteral("homeTimeouts")] = -1;
            m_state[QStringLiteral("lastPlay")]     = QString();
            m_state[QStringLiteral("lastPlayTag")]  = QString();
        } else {
            const QJsonObject lastPlay = sit.value(QStringLiteral("lastPlay")).toObject();

            int possId = sit.value(QStringLiteral("possession")).toString().toInt();
            if (possId <= 0)
                possId = sit.value(QStringLiteral("possession")).toInt();
            if (possId <= 0)
                possId = lastPlay.value(QStringLiteral("team")).toObject()
                                 .value(QStringLiteral("id")).toString().toInt();
            m_state[QStringLiteral("possessionId")] = possId;

            // What college football actually sends is less than the NFL does.
            // Checked against a live game: the college payload carries down,
            // distance, yardLine, isRedZone, timeouts and lastPlay -- and no
            // downDistanceText, no shortDownDistanceText, no possessionText
            // and no possession at all. So the text is composed here, and
            // possession comes from whoever ran the last play.
            //
            // Between plays -- after a touchdown, during the extra point, on a
            // kickoff -- ESPN sets down and distance to **-1** and drops
            // yardLine, which emptied the middle of the board every time
            // anyone scored and made it look broken. Hold the last known
            // values instead, the same way the baseball board holds the last
            // pitch between batters. The situation block going away entirely
            // is the only thing that clears them, and that means the game is
            // over or has not started.
            const int down = sit.value(QStringLiteral("down")).toInt();
            const int dist = sit.value(QStringLiteral("distance")).toInt();

            QString dd = sit.value(QStringLiteral("shortDownDistanceText")).toString();
            if (dd.isEmpty() && down > 0) {
                dd = (dist <= 0) ? QStringLiteral("%1 & GOAL").arg(ordinal(down))
                                 : QStringLiteral("%1 & %2").arg(ordinal(down)).arg(dist);
            }
            if (!dd.isEmpty()) {
                m_state[QStringLiteral("downDistance")] = dd.toUpper();
                m_state[QStringLiteral("toGo")] = qMax(0, dist);
            }

            // Which team's half of the field the ball is in. yardLine counts
            // from the team in possession's own goal line, so past 50 it is
            // named for the other team.
            QString where = sit.value(QStringLiteral("possessionText")).toString();
            const bool haveYard = sit.contains(QStringLiteral("yardLine"))
                                  && sit.value(QStringLiteral("yardLine")).toInt() >= 0;
            const int yard = sit.value(QStringLiteral("yardLine")).toInt();
            if (where.isEmpty() && haveYard && possId > 0) {
                const int awayId = m_state.value(QStringLiteral("awayId")).toInt();
                const QString own = (possId == awayId)
                    ? m_state.value(QStringLiteral("awayAbbr")).toString()
                    : m_state.value(QStringLiteral("homeAbbr")).toString();
                const QString opp = (possId == awayId)
                    ? m_state.value(QStringLiteral("homeAbbr")).toString()
                    : m_state.value(QStringLiteral("awayAbbr")).toString();
                where = (yard == 50) ? QStringLiteral("50")
                      : (yard > 50)  ? QStringLiteral("%1 %2").arg(opp).arg(100 - yard)
                                     : QStringLiteral("%1 %2").arg(own).arg(yard);
            }
            if (!where.isEmpty())
                m_state[QStringLiteral("fieldPos")] = where.toUpper();
            if (haveYard)
                m_state[QStringLiteral("ballOn")] = yard;

            m_state[QStringLiteral("isRedZone")] = sit.value(QStringLiteral("isRedZone")).toBool();
            m_state[QStringLiteral("awayTimeouts")] =
                sit.contains(QStringLiteral("awayTimeouts"))
                    ? sit.value(QStringLiteral("awayTimeouts")).toInt() : -1;
            m_state[QStringLiteral("homeTimeouts")] =
                sit.contains(QStringLiteral("homeTimeouts"))
                    ? sit.value(QStringLiteral("homeTimeouts")).toInt() : -1;

            m_state[QStringLiteral("lastPlay")] = lastPlay.value(QStringLiteral("text")).toString();

            // The tag above it: "RUSH  ·  7 YDS  ·  TOUCHDOWN".
            QStringList bits;
            const QString playType = lastPlay.value(QStringLiteral("type")).toObject()
                                             .value(QStringLiteral("text")).toString();
            if (!playType.isEmpty())
                bits << playType.toUpper();
            if (lastPlay.contains(QStringLiteral("statYardage"))) {
                const int yds = lastPlay.value(QStringLiteral("statYardage")).toInt();
                bits << QStringLiteral("%1 YDS").arg(yds);
            }
            if (lastPlay.value(QStringLiteral("scoringPlay")).toBool())
                bits << QStringLiteral("SCORE");
            m_state[QStringLiteral("lastPlayTag")] = bits.join(QStringLiteral("  ·  "));
        }

        // ---- quarter by quarter -------------------------------------------
        const QJsonArray awayLines = away.value(QStringLiteral("linescores")).toArray();
        const QJsonArray homeLines = home.value(QStringLiteral("linescores")).toArray();
        auto cell = [](const QJsonArray &lines, int i) -> QVariant {
            if (i >= lines.size())
                return QVariant(QStringLiteral("-"));
            const QJsonObject l = lines.at(i).toObject();
            const QString dv = l.value(QStringLiteral("displayValue")).toString();
            return dv.isEmpty() ? QVariant(qRound(l.value(QStringLiteral("value")).toDouble()))
                                : QVariant(dv);
        };
        QVariantList periods;
        const int played = qMax(awayLines.size(), homeLines.size());
        // Always show four quarters, even at 0-0 in the first, so the grid does
        // not grow a column at a time while the game is being played.
        for (int i = 0; i < qMax(4, played); ++i) {
            QVariantMap p;
            p[QStringLiteral("num")]  = (i < 4) ? QString::number(i + 1)
                                      : (i == 4 ? QStringLiteral("OT")
                                                : QStringLiteral("%1OT").arg(i - 3));
            p[QStringLiteral("away")] = cell(awayLines, i);
            p[QStringLiteral("home")] = cell(homeLines, i);
            periods.append(p);
        }
        m_state[QStringLiteral("periods")] = periods;

        // ---- leaders ------------------------------------------------------
        // Football's answer to the pitchers of record: passing, rushing and
        // receiving, which ESPN already ranks for us.
        QVariantList leaders;
        for (const QJsonValue &cv : comp.value(QStringLiteral("leaders")).toArray()) {
            const QJsonObject cat = cv.toObject();
            const QJsonArray list = cat.value(QStringLiteral("leaders")).toArray();
            if (list.isEmpty())
                continue;
            const QJsonObject top = list.first().toObject();
            const QJsonObject athlete = top.value(QStringLiteral("athlete")).toObject();
            QVariantMap m;
            m[QStringLiteral("label")] = cat.value(QStringLiteral("shortDisplayName")).toString().toUpper();
            m[QStringLiteral("name")]  = athlete.value(QStringLiteral("shortName")).toString();
            m[QStringLiteral("line")]  = top.value(QStringLiteral("displayValue")).toString();
            m[QStringLiteral("teamId")] = top.value(QStringLiteral("team")).toObject()
                                             .value(QStringLiteral("id")).toString().toInt();
            leaders.append(m);
        }
        m_state[QStringLiteral("leaders")] = leaders;

        // ---- the line for a game that has not started ----------------------
        QString oddsLine;
        const QJsonArray odds = comp.value(QStringLiteral("odds")).toArray();
        if (!odds.isEmpty()) {
            const QJsonObject o = odds.first().toObject();
            const QString details = o.value(QStringLiteral("details")).toString();
            QStringList bits;
            if (!details.isEmpty())
                bits << details;
            const double ou = o.value(QStringLiteral("overUnder")).toDouble();
            if (ou > 0)
                bits << QStringLiteral("O/U %1").arg(ou, 0, 'f', 1);
            oddsLine = bits.join(QStringLiteral("   ·   "));
        }
        m_state[QStringLiteral("oddsLine")] = oddsLine;

        // Once it is over the last play is "End of game", which says nothing.
        // ESPN's own recap sentence is the useful thing in that slot.
        const QJsonArray heads = comp.value(QStringLiteral("headlines")).toArray();
        m_state[QStringLiteral("recap")] = heads.isEmpty()
            ? QString()
            : heads.first().toObject().value(QStringLiteral("description")).toString()
                   .trimmed().remove(QRegularExpression(QStringLiteral("^[\\x{2014}\\-\\s]+")));

        publish();
    });
}

// Rank, overall record and conference standing for the two followed teams.
// An opponent gets whatever the event payload carried and no more -- there is
// no cheap way to have the standing of all 762 teams, and no reason to.
void GameFeed::applyTeamInfo()
{
    for (const char *prefix : { "away", "home" }) {
        const QString p = QString::fromLatin1(prefix);
        const int id = m_state.value(p + QStringLiteral("Id")).toInt();
        const TeamInfo info = m_team.value(id);
        m_state[p + QStringLiteral("Standing")] = info.standing;
        if (!info.record.isEmpty())
            m_state[p + QStringLiteral("Record")] = info.record;
        if (info.rank > 0 && m_state.value(p + QStringLiteral("Rank")).toInt() == 0)
            m_state[p + QStringLiteral("Rank")] = info.rank;
    }
}

void GameFeed::publishSlate()
{
    QVariantList teams;
    for (int teamId : trackedTeams()) {
        const TeamInfo info = m_team.value(teamId);
        QVariantMap t;
        t[QStringLiteral("teamId")]   = teamId;
        t[QStringLiteral("name")]     = info.name;
        t[QStringLiteral("abbr")]     = info.abbr;
        t[QStringLiteral("record")]   = info.record;
        t[QStringLiteral("standing")] = info.standing;
        t[QStringLiteral("rank")]     = info.rank;
        t[QStringLiteral("current")]  = m_current.value(teamId);
        t[QStringLiteral("games")]    = m_season.value(teamId);
        teams.append(t);
    }
    m_state[QStringLiteral("teams")] = teams;
    m_state[QStringLiteral("seasonLabel")] =
        QStringLiteral("%1 SEASON").arg(easternNow().date().year()
                                        - (easternNow().date().month() < 3 ? 1 : 0));
}

void GameFeed::showGame(const QString &eventId, const QVariantMap &info)
{
    if (eventId.isEmpty())
        return;

    m_pinned = true;
    m_eventId = eventId;

    // Paint what the list already knows straight away, so the board is never
    // blank while the request for this game is in flight.
    auto carry = [&](const char *from, const char *to) {
        if (info.contains(QLatin1String(from)))
            m_state[QString::fromLatin1(to)] = info.value(QLatin1String(from));
    };
    for (const char *k : { "awayId", "homeId", "awayAbbr", "homeAbbr",
                           "awayName", "homeName", "awayScore", "homeScore",
                           "awayRank", "homeRank", "venue", "statusText" })
        carry(k, k);
    carry("abstract", "abstractState");

    m_state[QStringLiteral("eventId")]     = eventId;
    m_state[QStringLiteral("hasGame")]     = true;
    m_state[QStringLiteral("periods")]     = QVariantList();
    m_state[QStringLiteral("leaders")]     = QVariantList();
    m_state[QStringLiteral("lastPlay")]    = QString();
    m_state[QStringLiteral("lastPlayTag")] = QString();
    m_state[QStringLiteral("recap")]       = QString();
    m_state[QStringLiteral("downDistance")] = QString();
    m_state[QStringLiteral("ballOn")]      = -1;
    applyTeamInfo();
    publish();

    requestEvent(eventId, true);
}

void GameFeed::showTeamGame()
{
    if (!m_pinned)
        return;
    m_pinned = false;
    m_eventId.clear();
    pickCurrentEvent();
}

void GameFeed::publish()
{
    m_state[QStringLiteral("updatedAt")] =
        easternNow().time().toString(QStringLiteral("h:mm:ss AP"));
    emit stateChanged();
}

// ---------------------------------------------------------------------------
// Demo mode. College football happens on one day a week, so for six days out
// of seven there is nothing live to look at and no way to judge the layout.
// These fixtures are shaped exactly like the parsed payload, including the
// situation block, which is the part that only exists while a game is on.
// ---------------------------------------------------------------------------
void GameFeed::loadDemoGame(const QString &demoState)
{
    auto S = [](const char *s) { return QString::fromUtf8(s); };

    m_state[QStringLiteral("loaded")]        = true;
    m_state[QStringLiteral("hasGame")]       = true;
    m_state[QStringLiteral("eventId")]       = S("401858454");
    m_state[QStringLiteral("abstractState")] = S("Live");
    m_state[QStringLiteral("statusText")]    = S("In Progress");
    m_state[QStringLiteral("finalLabel")]    = S("FINAL");
    m_state[QStringLiteral("venue")]         = S("Ohio Stadium");
    m_state[QStringLiteral("broadcast")]     = S("BTN");
    m_state[QStringLiteral("note")]          = QString();

    m_state[QStringLiteral("awayName")]   = S("Kent State");
    m_state[QStringLiteral("homeName")]   = S("Ohio State");
    m_state[QStringLiteral("awayAbbr")]   = S("KENT");
    m_state[QStringLiteral("homeAbbr")]   = S("OSU");
    m_state[QStringLiteral("awayId")]     = 2309;
    m_state[QStringLiteral("homeId")]     = 194;
    m_state[QStringLiteral("awayRank")]   = 0;
    m_state[QStringLiteral("homeRank")]   = 6;
    m_state[QStringLiteral("awayRecord")] = S("0-3");
    m_state[QStringLiteral("homeRecord")] = S("1-1");
    m_state[QStringLiteral("awayStanding")] = S("5th in MAC");
    m_state[QStringLiteral("homeStanding")] = S("1st in Big Ten");
    m_state[QStringLiteral("awayScore")]  = 7;
    m_state[QStringLiteral("homeScore")]  = 24;

    m_state[QStringLiteral("periodLabel")] = S("2ND");
    m_state[QStringLiteral("clock")]       = S("7:12");

    m_state[QStringLiteral("possessionId")] = 194;
    m_state[QStringLiteral("downDistance")] = S("2ND & 7");
    m_state[QStringLiteral("fieldPos")]     = S("KENT 43");
    m_state[QStringLiteral("ballOn")]       = 57;
    m_state[QStringLiteral("toGo")]         = 7;
    m_state[QStringLiteral("isRedZone")]    = false;
    m_state[QStringLiteral("awayTimeouts")] = 2;
    m_state[QStringLiteral("homeTimeouts")] = 3;

    m_state[QStringLiteral("lastPlayTag")] = S("RUSH  ·  6 YDS");
    m_state[QStringLiteral("lastPlay")] =
        S("Bo Jackson run for 6 yards to the Kent 43 (tackle by Marcus Bell).");
    m_state[QStringLiteral("recap")] = QString();
    m_state[QStringLiteral("oddsLine")] = S("OSU -38.5   ·   O/U 55.5");

    const int away[4] = { 0, 7, -1, -1 };
    const int home[4] = { 14, 10, -1, -1 };
    QVariantList periods;
    for (int i = 0; i < 4; ++i) {
        QVariantMap p;
        p[QStringLiteral("num")]  = QString::number(i + 1);
        p[QStringLiteral("away")] = away[i] < 0 ? QVariant(QStringLiteral("-")) : QVariant(away[i]);
        p[QStringLiteral("home")] = home[i] < 0 ? QVariant(QStringLiteral("-")) : QVariant(home[i]);
        periods.append(p);
    }
    m_state[QStringLiteral("periods")] = periods;

    QVariantList leaders;
    const char *rows[3][3] = {
        { "PASS", "J. Sayin",   "14/18, 214 YDS, 2 TD" },
        { "RUSH", "B. Jackson", "12 CAR, 96 YDS, 1 TD" },
        { "REC",  "C. Tate",    "5 REC, 88 YDS, 1 TD"  }
    };
    for (const auto &r : rows) {
        QVariantMap m;
        m[QStringLiteral("label")]  = S(r[0]);
        m[QStringLiteral("name")]   = S(r[1]);
        m[QStringLiteral("line")]   = S(r[2]);
        m[QStringLiteral("teamId")] = 194;
        leaders.append(m);
    }
    m_state[QStringLiteral("leaders")] = leaders;

    // A canned season for both teams, so the list page has something to draw.
    struct DemoGame { int wk; const char *date; const char *opp; int oppId;
                      bool home; const char *note; int me; int them; };
    const DemoGame osu[] = {
        { 1, "SEP 5",  "BALL", 2050, true,  "W 56-3",  56,  3 },
        { 2, "SEP 12", "TEX",   251, false, "L 23-24", 23, 24 },
        { 3, "SEP 19", "KENT", 2309, true,  "2ND  7:12", 24, 7 },
        { 4, "SEP 26", "ILL",   356, true,  "12:00 PM", -1, -1 },
        { 5, "OCT 3",  "IOWA", 2294, false, "TBD",      -1, -1 },
        { 6, "OCT 10", "MD",    120, true,  "TBD",      -1, -1 }
    };
    const DemoGame ysu[] = {
        { 1, "SEP 5",  "PITT",  221, false, "L 10-45", 10, 45 },
        { 2, "SEP 12", "DUQ",  2184, true,  "W 70-13", 70, 13 },
        { 3, "SEP 19", "SDAK", 2571, false, "2:00 PM", -1, -1 },
        { 4, "SEP 26", "NDSU", 2449, true,  "3:30 PM", -1, -1 }
    };

    auto season = [&](const DemoGame *games, int n, int teamId) {
        QVariantList out;
        for (int i = 0; i < n; ++i) {
            const DemoGame &g = games[i];
            QVariantMap row;
            row[QStringLiteral("bye")]       = false;
            row[QStringLiteral("eventId")]   = QStringLiteral("demo-%1-%2").arg(teamId).arg(g.wk);
            row[QStringLiteral("weekNo")]    = g.wk;
            row[QStringLiteral("week")]      = QStringLiteral("WK %1").arg(g.wk);
            row[QStringLiteral("date")]      = S(g.date);
            row[QStringLiteral("home")]      = g.home;
            row[QStringLiteral("oppAbbr")]   = S(g.opp);
            row[QStringLiteral("oppId")]     = g.oppId;
            row[QStringLiteral("oppRank")]   = 0;
            row[QStringLiteral("note")]      = S(g.note);
            row[QStringLiteral("teamScore")] = g.me   < 0 ? QVariant(QString()) : QVariant(g.me);
            row[QStringLiteral("oppScore")]  = g.them < 0 ? QVariant(QString()) : QVariant(g.them);
            const bool live = (g.wk == 3 && teamId == 194);
            row[QStringLiteral("isLive")]    = live;
            row[QStringLiteral("isFinal")]   = (g.me >= 0 && !live);
            row[QStringLiteral("won")]       = (g.me > g.them);
            out.append(row);
        }
        return out;
    };

    m_season.insert(194,  season(osu, 6, 194));
    m_season.insert(2754, season(ysu, 4, 2754));

    TeamInfo osuInfo;
    osuInfo.name = S("Ohio State"); osuInfo.abbr = S("OSU");
    osuInfo.record = S("1-1"); osuInfo.standing = S("1st in Big Ten"); osuInfo.rank = 6;
    TeamInfo ysuInfo;
    ysuInfo.name = S("Youngstown St"); ysuInfo.abbr = S("YSU");
    ysuInfo.record = S("2-1"); ysuInfo.standing = S("1st in MVFC"); ysuInfo.rank = 0;
    m_team.insert(194, osuInfo);
    m_team.insert(2754, ysuInfo);
    m_current.insert(194, QStringLiteral("demo-194-3"));
    m_current.insert(2754, QStringLiteral("demo-2754-3"));
    publishSlate();

    // ... and the league-wide page, which otherwise has nothing to draw.
    struct DemoLive { const char *away; int awayId; int awayRank; const char *ascore;
                      const char *home; int homeId; int homeRank; const char *hscore;
                      const char *note; int poss; const char *dd; bool followed; };
    const DemoLive liveGames[] = {
        { "KENT", 2309, 0, "7",  "OSU",  194, 6,  "24", "2ND  7:12",  194,  "2ND & 7",  true  },
        { "MICH",  130, 19, "13", "NEB",  158, 0,  "10", "3RD  2:48",  158,  "1ST & 10", false },
        { "LSU",   99,  8,  "21", "MISS", 145, 12, "17", "4TH  11:03", 99,   "3RD & 4",  false },
        { "UGA",   61,  2,  "35", "ARK",   8,  0,  "14", "HALFTIME",   0,    "",         false }
    };
    QVariantList liveRows;
    for (const auto &r : liveGames) {
        QVariantMap m;
        m[QStringLiteral("eventId")]  = QStringLiteral("demo-live-%1").arg(r.awayId);
        m[QStringLiteral("isLive")]   = true;
        m[QStringLiteral("isFinal")]  = false;
        m[QStringLiteral("abstract")] = S("Live");
        m[QStringLiteral("awayAbbr")] = S(r.away);
        m[QStringLiteral("homeAbbr")] = S(r.home);
        m[QStringLiteral("awayId")]   = r.awayId;
        m[QStringLiteral("homeId")]   = r.homeId;
        m[QStringLiteral("awayRank")] = r.awayRank;
        m[QStringLiteral("homeRank")] = r.homeRank;
        m[QStringLiteral("awayScore")] = S(r.ascore);
        m[QStringLiteral("homeScore")] = S(r.hscore);
        m[QStringLiteral("note")]     = S(r.note);
        m[QStringLiteral("possessionId")] = r.poss;
        m[QStringLiteral("downDistance")] = S(r.dd);
        m[QStringLiteral("followed")] = r.followed;
        liveRows.append(m);
    }
    m_state[QStringLiteral("live")]       = liveRows;
    m_state[QStringLiteral("liveCount")]  = int(liveRows.size());
    m_state[QStringLiteral("dayCount")]   = int(liveRows.size());
    m_state[QStringLiteral("liveLoaded")] = true;
    m_state[QStringLiteral("liveDate")]   = S("SATURDAY, SEPTEMBER 19");

    if (demoState == QStringLiteral("none")) {
        m_state[QStringLiteral("hasGame")]       = false;
        m_state[QStringLiteral("abstractState")] = S("Preview");
        m_state[QStringLiteral("statusText")]    = S("No game scheduled");
    } else if (demoState == QStringLiteral("pregame")) {
        m_state[QStringLiteral("abstractState")] = S("Preview");
        m_state[QStringLiteral("statusText")]    = S("Sat, September 19th at 12:00 PM EDT");
        m_state[QStringLiteral("awayScore")]     = 0;
        m_state[QStringLiteral("homeScore")]     = 0;
        m_state[QStringLiteral("periodLabel")]   = QString();
        m_state[QStringLiteral("clock")]         = QString();
        m_state[QStringLiteral("possessionId")]  = 0;
        m_state[QStringLiteral("downDistance")]  = QString();
        m_state[QStringLiteral("ballOn")]        = -1;
        m_state[QStringLiteral("lastPlay")]      = QString();
        m_state[QStringLiteral("lastPlayTag")]   = QString();
        m_state[QStringLiteral("leaders")]       = QVariantList();
        m_state[QStringLiteral("periods")]       = QVariantList();
    } else if (demoState == QStringLiteral("final")) {
        m_state[QStringLiteral("abstractState")] = S("Final");
        m_state[QStringLiteral("statusText")]    = S("Final");
        m_state[QStringLiteral("awayScore")]     = 17;
        m_state[QStringLiteral("homeScore")]     = 45;
        m_state[QStringLiteral("periodLabel")]   = QString();
        m_state[QStringLiteral("clock")]         = S("0:00");
        m_state[QStringLiteral("possessionId")]  = 0;
        m_state[QStringLiteral("downDistance")]  = QString();
        m_state[QStringLiteral("ballOn")]        = -1;
        m_state[QStringLiteral("lastPlay")]      = QString();
        m_state[QStringLiteral("lastPlayTag")]   = QString();
        m_state[QStringLiteral("recap")] =
            S("Julian Sayin threw for three touchdowns and Ohio State pulled away "
              "from Kent State in the second half for a 45-17 win on Saturday.");
        QVariantList p4;
        const int a4[4] = { 0, 7, 3, 7 };
        const int h4[4] = { 14, 10, 14, 7 };
        for (int i = 0; i < 4; ++i) {
            QVariantMap p;
            p[QStringLiteral("num")]  = QString::number(i + 1);
            p[QStringLiteral("away")] = a4[i];
            p[QStringLiteral("home")] = h4[i];
            p4.append(p);
        }
        m_state[QStringLiteral("periods")] = p4;
    }

    publish();
}
