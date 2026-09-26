#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QVector>
#include <QWidget>

class QTimer;

/*
 * The desktop pet: a single-celled tenant that wanders the screen, naps,
 * complains, and can be dragged around by the operator.
 *
 * It is a top-level translucent tool window, so it never appears in the task
 * bar and never steals focus from whatever you are working in.
 */
class PetWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PetWidget(QWidget *parent = nullptr);

    void setPetScale(int percent);        // 60..200
    void setSpeedFactor(double factor);   // 0.2..3.0
    void setFollowCursor(bool on);
    void setChatter(bool on);
    void setAlwaysOnTop(bool on);
    void setColor(const QColor &c);

    void say(const QString &text, int ms = 3200);
    qreal bubbleLeft() const { return m_bubbleLeft; }
    void celebrate();
    void startEncounter();      // the "we're going to mutate again" scene

    // context reactions: the pet gabs about whatever the app is doing right now.
    enum class Mood { ProtectStart, ProtectDone, ProtectFail, Mining, Idle, Angry };
    void react(Mood mood);      // pick a themed line + a little body language

    // Orbit a fixed screen-space point at `radius` px until stopOrbit() is
    // called. Used by the crypt page so the pet circles the flask during
    // protection runs, heckling as it goes.
    void orbit(const QPoint &screenCenter, qreal radius);
    void stopOrbit();

signals:
    void openDashboardRequested();
    void followCursorToggled(bool on);
    void dismissed();

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;

private:
    enum class State { Wander, Pause, Sleep, Held, Excited, Orbit, Play, Stunned };
    enum class ParticleKind { Heart, Spark, Crumb };

    struct Particle {
        QPointF      pos;       // screen coordinates
        QPointF      velocity;
        QColor       color;
        qreal        life = 0.0;
        qreal        ttl  = 1.0;
        qreal        size = 5.0;
        ParticleKind kind = ParticleKind::Spark;
    };

    /* A short scripted encounter. Each step runs on its own clock and hands
     * over to the next, so the whole thing reads as one continuous move
     * rather than a queue of jumps. */
    enum class Scene { None, Descend, Greet, LineOne, LineTwo, LineThree, Divide, Farewell };

    void   tick();
    void   pickTarget();
    void   wake();
    void   updateWindowGeometry();
    void   clampToScreen();
    QRectF availableArea() const;
    QSizeF petBox() const;
    qreal  reach() const;               // half-extent of one bacterium, in px
    qreal  bodyLength() const;
    qreal  s() const { return m_scale; }

    void   updateScene(qreal dt);
    void   setSceneStep(Scene step);
    void   endEncounter(bool immediate);
    void   friendSay(const QString &text, int ms);
    qreal  friendRestX() const;
    qreal  sceneHalfWidth() const;
    QRectF speechRect(bool visitor) const;
    void   talk();
    void   pet();
    void   feed();
    void   startPlay();
    void   spawnBurst(ParticleKind kind, int count, const QColor &color);
    void   updateParticles(qreal dt);
    void   stunAtEdge();

    QTimer       *m_timer = nullptr;
    QElapsedTimer m_clock;

    QPointF m_center;                  // pet centre, in screen coordinates
    QPointF m_velocity;
    QPointF m_target;
    QPointF m_dragOffset;
    QPointF m_pressScreen;

    State m_state    = State::Wander;
    State m_stateBeforeHold = State::Wander;
    qreal m_scale    = 1.0;
    qreal m_speed    = 1.0;
    qreal m_phase    = 0.0;
    qreal m_heading  = 0.0;            // degrees
    qreal m_bob      = 0.0;
    qreal m_squash   = 0.0;
    qreal m_stateFor = 0.0;            // seconds in the current state
    qreal m_pauseFor = 2.0;
    qreal m_blinkIn  = 3.0;
    qreal m_blinkFor = 0.0;
    qreal m_sinceChatter = 0.0;
    qreal m_chatterAt    = 30.0;
    qreal m_affection    = 0.0;
    qreal m_playLeft     = 0.0;
    qreal m_playCatchCooldown = 0.0;
    qreal m_trailIn      = 0.0;

    bool   m_follow  = false;
    bool   m_chatter = true;
    bool   m_dragged = false;
    bool   m_menuOpen = false;
    int    m_talkIndex = -1;
    int    m_bonks = 0;
    QString m_talkReply;
    qreal  m_talkReplyIn = 0.0;
    QColor m_color;
    QVector<Particle> m_particles;

    QString m_bubble;
    qreal   m_bubbleLeft = 0.0;        // seconds
    QSizeF  m_bubbleSize;

    // --- the encounter ---------------------------------------------------
    Scene   m_scene         = Scene::None;
    qreal   m_sceneT        = 0.0;     // seconds in the current step
    qreal   m_sceneIn       = 120.0;   // seconds until the next one
    qreal   m_friendX       = 0.0;     // offset from the pet, in px
    qreal   m_friendAlpha   = 0.0;
    qreal   m_friendPhase   = 0.0;
    qreal   m_friendHeading = 180.0;
    qreal   m_dividePulse   = 0.0;     // 0 → 1 → 0 across the divide beat
    int     m_scenario      = 0;       // varied visitor story currently playing
    int     m_lastScenario  = -1;
    QString m_friendBubble;
    qreal   m_friendBubbleLeft = 0.0;
    QSizeF  m_friendBubbleSize;

    // --- orbit -----------------------------------------------------------
    QPointF m_orbitCenter;                 // screen coords
    qreal   m_orbitRadius = 0.0;
    qreal   m_orbitAngle  = 0.0;           // radians
    qreal   m_orbitBanterIn = 3.0;         // seconds until the next crypt-banter line
};
