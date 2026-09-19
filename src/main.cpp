#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QStringList>
#include <QTimer>
#include <QImage>
#include <QUrl>
#include <QtGlobal>

#include "GameFeed.h"
#include "League.h"
#include "Orientation.h"
#include "LogoStore.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("cfb_scoreboard"));

    const QStringList args = app.arguments();
    auto opt = [&args](const QString &flag) -> QString {
        const int i = args.indexOf(flag);
        return (i >= 0 && i + 1 < args.size()) ? args.at(i + 1) : QString();
    };

    // --league cfb|nfl. Same API, same parsing, same board; a different path
    // segment and a different pair of teams.
    const League league = Leagues::byKey(opt(QStringLiteral("--league")));

    // Which of the followed teams the board opens on. Both are on the list
    // page either way.
    const QString teamOpt = opt(QStringLiteral("--team"));
    const int teamId = teamOpt.isEmpty() ? league.teams.value(0) : teamOpt.toInt();

    // --demo [live|pregame|final|none]  (defaults to live)
    const bool demo = args.contains(QStringLiteral("--demo"));
    QString demoState = opt(QStringLiteral("--demo"));
    if (demoState.startsWith(QStringLiteral("--")) || demoState.isEmpty())
        demoState = QStringLiteral("live");
    const QString shotPath = opt(QStringLiteral("--shot"));

    // --panel 954x1696 overrides the detected panel geometry. The device
    // reports its own size in fullscreen, but --shot runs offscreen and has
    // nothing to detect, so previews need to be told.
    int panelW = 0, panelH = 0;
    const QString panelOpt = opt(QStringLiteral("--panel"));
    if (panelOpt.contains(QLatin1Char('x'))) {
        const QStringList wh = panelOpt.split(QLatin1Char('x'));
        if (wh.size() == 2) {
            panelW = wh.at(0).toInt();
            panelH = wh.at(1).toInt();
        }
    }

    // --orient landscape | portrait | auto  (default portrait)
    // --flip turns the chosen orientation upside down, for a tented device.
    // --rotate-offset N corrects the sensor mapping without a rebuild.
    QString orient = opt(QStringLiteral("--orient"));
    if (orient != QStringLiteral("landscape") && orient != QStringLiteral("auto"))
        orient = QStringLiteral("portrait");

    // --muted / --faint override the two secondary greys, e.g. --muted 2e2e2e.
    const QString mutedHex = opt(QStringLiteral("--muted"));
    const QString faintHex = opt(QStringLiteral("--faint"));

    // --page schedule starts on the list instead of the game. Mostly for
    // --shot previews, since tapping is how you switch on device.
    const QString pageOpt = opt(QStringLiteral("--page"));
    const int startPage = (pageOpt == QStringLiteral("schedule")
                           || pageOpt == QStringLiteral("slate")) ? 1
                        : (pageOpt == QStringLiteral("live")) ? 2 : 0;

    const bool flip = args.contains(QStringLiteral("--flip"));
    const int rotateOffset = opt(QStringLiteral("--rotate-offset")).toInt();

    // Fixed rotation when not following the sensor.
    int fixedRotation = (orient == QStringLiteral("landscape")) ? 90 : 0;
    if (flip)
        fixedRotation = (fixedRotation + 180) % 360;

    GameFeed feed(league, teamId, demo ? demoState : QString());
    LogoStore logos(league);

    Orientation orientationSensor(rotateOffset);
    const bool autoRotate = (orient == QStringLiteral("auto")) && orientationSensor.available();
    if (orient == QStringLiteral("auto") && !orientationSensor.available())
        qWarning("--orient auto: no accelerometer found, holding portrait");
    qInfo("orientation: mode=%s sensor=%s autoRotate=%d fixed=%d",
          qPrintable(orient), orientationSensor.available() ? "yes" : "no",
          int(autoRotate), fixedRotation);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("feed"), &feed);
    engine.rootContext()->setContextProperty(QStringLiteral("shotMode"),
                                             !shotPath.isEmpty());
    engine.rootContext()->setContextProperty(QStringLiteral("logos"), &logos);
    engine.rootContext()->setContextProperty(QStringLiteral("startPage"), startPage);
    engine.rootContext()->setContextProperty(QStringLiteral("mutedOverride"), mutedHex);
    engine.rootContext()->setContextProperty(QStringLiteral("faintOverride"), faintHex);
    engine.rootContext()->setContextProperty(QStringLiteral("sensor"), &orientationSensor);
    engine.rootContext()->setContextProperty(QStringLiteral("autoRotate"), autoRotate);
    engine.rootContext()->setContextProperty(QStringLiteral("fixedRotation"), fixedRotation);
    engine.rootContext()->setContextProperty(QStringLiteral("panelWOverride"), panelW);
    engine.rootContext()->setContextProperty(QStringLiteral("panelHOverride"), panelH);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    engine.loadFromModule("Scoreboard", "Preview");
#else
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Scoreboard/Preview.qml")));
#endif
    if (engine.rootObjects().isEmpty())
        return -1;

    // --shot renders one full-size frame to a PNG and exits. Handy for
    // iterating on the layout without a device in front of you.
    if (!shotPath.isEmpty()) {
        auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        if (!win)
            return -1;
        QTimer::singleShot(demo ? 600 : 5000, &app, [win, shotPath]() {
            const QImage img = win->grabWindow();
            if (img.isNull() || !img.save(shotPath)) {
                qWarning("could not write %s", qPrintable(shotPath));
                QGuiApplication::exit(1);
                return;
            }
            qInfo("wrote %s (%dx%d)", qPrintable(shotPath), img.width(), img.height());
            QGuiApplication::quit();
        });
    }

    return app.exec();
}
