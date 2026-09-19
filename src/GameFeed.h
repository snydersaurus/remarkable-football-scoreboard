#pragma once

#include <QObject>
#include <QVariantMap>
#include <QVariantList>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QHash>
#include <QSet>
#include <QJsonObject>
#include <functional>

#include "League.h"

/*
 * Pulls football state from ESPN's public site API. No key, no auth.
 *
 *   /teams/{id}/schedule
 *       -> a team's whole season: every event id, date, opponent, result.
 *          ~210KB, so it is asked for on a ten minute timer, not the poll.
 *   /scoreboard/{eventId}
 *       -> one game in scoreboard shape: score, quarter, clock, possession,
 *          down and distance, last play, quarter-by-quarter, leaders. 23KB.
 *          This is the single-event form of /scoreboard, which for all of
 *          college football is 1.3MB -- far too much to poll on a tablet.
 *   /teams/{id}
 *       -> rank, overall record and conference standing. 20KB, twice an hour.
 *
 * Which league and which teams come from the League this was built with --
 * the API is identical for college football and the NFL, so nothing below
 * knows the difference. The followed teams' seasons are the list page; one
 * game is the detail page.
 *
 * Everything lands in a single `state` QVariantMap that QML reads directly,
 * so there is no model plumbing to maintain.
 */
class GameFeed : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged)

public:
    // The teams on the list page, in the order they appear.
    const QList<int> &trackedTeams() const { return m_league.teams; }
    const League &league() const { return m_league; }

    // A non-empty demoState ("live", "pregame", "final", "none") fills in a
    // canned game and makes no network calls, so the layout can be worked on
    // when nothing is being played -- which for football is six days a week.
    explicit GameFeed(const League &league, int teamId,
                      const QString &demoState = QString(),
                      QObject *parent = nullptr);

    QVariantMap state() const { return m_state; }

public slots:
    void refresh();

    // Show any game from the list, not just the followed team's. Pins the feed
    // to that event until showTeamGame() releases it, so the schedule poll
    // cannot quietly drag the board back to the followed team's game.
    Q_INVOKABLE void showGame(const QString &eventId, const QVariantMap &info);
    Q_INVOKABLE void showTeamGame();

    // Watch every game in college football, not just these two teams. Only
    // while the reader is looking at that page: the league-wide scoreboard is
    // ~18KB per game, which on a Saturday afternoon is well over a megabyte,
    // and there is no field-trimming parameter to make it smaller.
    Q_INVOKABLE void watchLive(bool on);

    // Follow a different pair. Everything the old pair produced is dropped and
    // refetched; the board opens on the first of the new two.
    Q_INVOKABLE void setTeams(const QList<int> &teams);

    // The whole league, for the picker: id, abbreviation and short name only.
    // Fetched once ever and kept on disk -- the full team list is 1.9MB for
    // college football, and it is the same list every week.
    Q_INVOKABLE void loadTeamIndex();

signals:
    void stateChanged();

private:
    void get(const QString &url, std::function<void(const QJsonObject &)> cb);
    void requestSchedule(int teamId);
    void requestTeam(int teamId);
    void requestEvent(const QString &eventId, bool isBoard);
    void requestLiveList();
    void publishFollowed();
    void pickCurrentEvent();
    void publishSlate();
    void applyTeamInfo();
    void publish();
    void loadDemoGame(const QString &demoState);

    struct TeamInfo {
        QString name;       // "Ohio State"
        QString abbr;       // "OSU"
        QString record;     // "1-1"
        QString standing;   // "1st in Big Ten"
        int rank = 0;       // 0 = unranked
    };

    League m_league;
    int m_teamId;
    QString m_eventId;
    bool m_pinned = false;

    QNetworkAccessManager m_net;
    QTimer m_pollTimer;
    QTimer m_scheduleTimer;
    QTimer m_teamTimer;
    QTimer m_liveTimer;

    // teamId -> that team's season, newest parse wins.
    QHash<int, QVariantList> m_season;
    // teamId -> rank / record / conference standing.
    QHash<int, TeamInfo> m_team;
    // The game each followed team is currently on, so a live game on the list
    // keeps its score even when the board is showing the other one.
    QHash<int, QString> m_current;

    QVariantMap m_state;
};
