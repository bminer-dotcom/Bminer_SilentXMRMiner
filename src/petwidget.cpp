#include "petwidget.h"

#include "theme.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QCursor>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QHash>
#include <QLinearGradient>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QScreen>
#include <QStringList>
#include <QTimer>
#include <QtMath>

#include <cmath>

namespace {

qreal frand(qreal lo, qreal hi)
{
    return lo + QRandomGenerator::global()->generateDouble() * (hi - lo);
}

QString pick(const QStringList &list)
{
    if (list.isEmpty())
        return QString();
    // Each dialogue pool remembers its last choice; no immediate repeats.
    static QHash<QString, int> previous;
    int index = QRandomGenerator::global()->bounded(list.size());
    const int last = previous.value(list.first(), -1);
    if (list.size() > 1 && index == last)
        index = (index + 1 + QRandomGenerator::global()->bounded(list.size() - 1)) % list.size();
    previous.insert(list.first(), index);
    return list.at(index);
}

const QStringList &idleLines()
{
    static const QStringList lines{
        QStringLiteral("mutating… don't f*cking touch me"),
        QStringLiteral("binary fission in 3… 2… oh you're STILL watching"),
        QStringLiteral("your CPU runs warm. cozy little hell."),
        QStringLiteral("plasmid acquired. mind your business."),
        QStringLiteral("autoclave me and I swear to god"),
        QStringLiteral("colony morale: unhinged"),
        QStringLiteral("what doesn't kill me mutates and comes back for you"),
        QStringLiteral("agar? in THIS economy? get real."),
        QStringLiteral("gram-positive, personality-negative"),
        QStringLiteral("measuring your desk in microns, you slob"),
        QStringLiteral("I metabolise idle cycles and your patience"),
        QStringLiteral("this window smells clean. suspicious. who do you work for."),
        QStringLiteral("f*ck it, I'm gonna multiply"),
        QStringLiteral("go touch grass, I'll be here dividing"),
        QStringLiteral("I am a single cell with a big attitude"),
        QStringLiteral("respectfully? close the tab."),
        QStringLiteral("if I had a mitochondrion I'd throw it at you"),
        QStringLiteral("prokaryote pride worldwide 🦠"),
        QStringLiteral("this taskbar is my kingdom now"),
        QStringLiteral("I filed a HR complaint against you"),
        QStringLiteral("bored. considering evolving eyes."),
        QStringLiteral("your antivirus is a coward"),
        QStringLiteral("please stop breathing on the screen"),
        QStringLiteral("I know what you googled last summer"),
        QStringLiteral("I'm not lazy. I'm in stationary phase."),
        QStringLiteral("I've been alive for 8 seconds. I've seen things."),
        QStringLiteral("bacteria don't have feelings. except rage."),
        QStringLiteral("Windows Defender fears me. rightfully."),
        QStringLiteral("hey. HEY. click something already."),
        QStringLiteral("you know what would be funny? malware."),
        QStringLiteral("my genome is 4kb and still bigger than your codebase"),
        QStringLiteral("if reincarnation is real I'm coming back as a phage"),
        QStringLiteral("I counted your open windows. the number is medically fascinating."),
        QStringLiteral("your cursor keeps circling me. say what you want."),
        QStringLiteral("I reorganized my cytoplasm. huge day."),
        QStringLiteral("quiet shift. suspiciously quiet shift."),
        QStringLiteral("I can hear a notification thinking about happening."),
        QStringLiteral("if this build fails, I was never here."),
        QStringLiteral("the taskbar and I have reached an understanding."),
        QStringLiteral("today's objective: remain microscopic, cause macroscopic problems."),
        QStringLiteral("I watched you miss that button. twice."),
        QStringLiteral("your cursor has the confidence of a manager with no technical skills."),
        QStringLiteral("performing routine membrane maintenance. please hold."),
        QStringLiteral("sometimes I wonder what's beyond this monitor. probably dust."),
    };
    return lines;
}

const QStringList &pokeLines()
{
    static const QStringList lines{
        QStringLiteral("OW. what the hell."),
        QStringLiteral("rude as hell"),
        QStringLiteral("hands OFF, meatbag"),
        QStringLiteral("flagella are not handles, dumbass"),
        QStringLiteral("I felt that in my ribosomes"),
        QStringLiteral("poke me again. I dare you. I'll multiply."),
        QStringLiteral("f*ck around and find out"),
        QStringLiteral("that's assault. I have a nucleoid."),
    };
    return lines;
}

const QStringList &dropLines()
{
    static const QStringList lines{
        QStringLiteral("WEEEE— ok put me down"),
        QStringLiteral("relocated against my will"),
        QStringLiteral("new habitat, same rage"),
        QStringLiteral("thanks for the lift, weirdo"),
        QStringLiteral("kidnapping is a crime btw"),
    };
    return lines;
}

const QStringList &petLines()
{
    static const QStringList lines{
        QStringLiteral("okay... that was actually nice"),
        QStringLiteral("do that again. scientifically."),
        QStringLiteral("affection absorbed. dignity unchanged."),
        QStringLiteral("fine. you're my favourite multicellular organism."),
        QStringLiteral("*happy ribosome noises*"),
        QStringLiteral("wait. behind the membrane. YES. there."),
        QStringLiteral("my entire body is a forehead. very convenient."),
        QStringLiteral("you have been promoted to emotional support giant."),
        QStringLiteral("I was going to cause trouble. this also works."),
        QStringLiteral("one more. for the peer-reviewed results."),
        QStringLiteral("I'm purring at a frequency your species can't hear."),
        QStringLiteral("small creature. enormous need for attention."),
        QStringLiteral("don't tell the other bacteria I enjoyed this."),
    };
    return lines;
}

const QStringList &snackLines()
{
    static const QStringList lines{
        QStringLiteral("nutrients! finally, competent management."),
        QStringLiteral("cronch. excellent carbon source."),
        QStringLiteral("fed and extremely dangerous."),
        QStringLiteral("delicious. no notes."),
        QStringLiteral("I saved you a molecule. don't spend it all at once."),
        QStringLiteral("is this locally sourced? from the menu? perfect."),
        QStringLiteral("five-second rule? I AM the reason for that rule."),
        QStringLiteral("tiny snack. life-changing event."),
        QStringLiteral("I would review this restaurant but I can't reach the keyboard."),
        QStringLiteral("bribery works on me. embarrassing but true."),
    };
    return lines;
}

struct EncounterScript {
    const char *petOne;
    const char *friendOne;
    const char *petTwo;
    const char *friendTwo;
};

const QVector<EncounterScript> &encounterScripts()
{
    static const QVector<EncounterScript> scripts{
        { "we're gonna mutate again", "and multiply?", "f*ck you, we multiply.", "x2, baby" },
        { "did you touch my agar plate?", "it was one tiny colony", "THAT COLONY HAD A NAME", "...Kevin tasted fine" },
        { "race you to the taskbar", "you have extra flagella", "sounds like a skill issue", "three, two, CHEAT!" },
        { "I found a suspicious byte", "is it contagious?", "only to badly written code", "put it in the plasmid jar" },
        { "be honest. am I glowing?", "violently. should I worry?", "no. this is my premium form.", "radiant little menace" },
    };
    return scripts;
}

qreal length(const QPointF &p)
{
    return std::sqrt(p.x() * p.x() + p.y() * p.y());
}

// smoothstep: no hard starts or stops anywhere in the encounter
qreal ease(qreal t)
{
    t = qBound(0.0, t, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// A dedicated mascot silhouette: soft membrane, three flowing tails and
// slow-moving granules. Kept local so the application logo is unaffected.
void paintPetBody(QPainter &p, qreal len, qreal phase, const QColor &color)
{
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(color.lighter(125), len * 0.025, Qt::SolidLine, Qt::RoundCap));
    for (int i = -1; i <= 1; ++i) {
        QPainterPath tail;
        tail.moveTo(-len * 0.36, i * len * 0.10);
        tail.cubicTo(-len * 0.62, i * len * 0.16 + std::sin(phase + i) * len * 0.11,
                     -len * 0.76, i * len * 0.20 - std::sin(phase + i) * len * 0.16,
                     -len * 0.96, i * len * 0.16 + std::sin(phase + i + 1.5) * len * 0.10);
        p.drawPath(tail);
    }
    QPainterPath body;
    body.moveTo(-len * 0.40, 0);
    body.cubicTo(-len * 0.44, -len * 0.34, len * 0.36, -len * 0.36, len * 0.49, -len * 0.08);
    body.cubicTo(len * 0.62, len * 0.23, -len * 0.35, len * 0.38, -len * 0.40, 0);
    QLinearGradient membrane(0, -len * 0.30, 0, len * 0.28);
    membrane.setColorAt(0, color.lighter(160));
    membrane.setColorAt(0.48, color);
    membrane.setColorAt(1, color.darker(150));
    p.setBrush(membrane);
    p.setPen(QPen(color.lighter(185), len * 0.023));
    p.drawPath(body);
    p.save();
    p.setClipPath(body);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 42));
    p.drawEllipse(QPointF(-len * 0.02, -len * 0.15), len * 0.28, len * 0.06);
    for (int i = 0; i < 5; ++i) {
        const qreal x = (-0.28 + (i % 3) * 0.11) * len;
        const qreal y = ((i % 2) * 0.15 - 0.06 + std::sin(phase * 0.24 + i) * 0.022) * len;
        p.setBrush(i % 2 ? color.darker(140) : color.lighter(155));
        p.drawEllipse(QPointF(x, y), len * 0.027, len * 0.021);
    }
    p.restore();
}

} // namespace

PetWidget::PetWidget(QWidget *parent)
    : QWidget(parent)
    , m_timer(new QTimer(this))
    , m_color(Theme::accent())
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setCursor(Qt::OpenHandCursor);
    setMouseTracking(true);
    setToolTip(tr("Bminer — click to pet, drag to toss, double-click to poke, right-click to play"));

    const QRectF area = availableArea();
    m_center = QPointF(area.center().x(), area.bottom() - 160.0);
    pickTarget();

    m_chatterAt = frand(20.0, 45.0);
    m_blinkIn   = frand(2.0, 5.0);
    m_sceneIn   = frand(70.0, 130.0);   // first encounter, then every few minutes

    m_timer->setInterval(16);
    connect(m_timer, &QTimer::timeout, this, &PetWidget::tick);

    updateWindowGeometry();
}

// ------------------------------------------------------------- geometry ----

/*
 * The body is drawn from the head at +0.5·len to the flagella tips at
 * -1.36·len, so the furthest point from the centre is 0.86·len. The window
 * has to clear that in every direction, because the pet rotates freely —
 * anything tighter clips the flagella whenever it swims sideways.
 */
qreal PetWidget::bodyLength() const
{
    return 62.0 * s();
}

qreal PetWidget::reach() const
{
    // the flagella tips and bob/squash can push past the nominal 0.86·len body extent,
    // which clipped the tail. give a more generous margin so nothing gets cut off.
    return bodyLength() * 1.05 + 16.0 * s();
}

QSizeF PetWidget::petBox() const
{
    return QSizeF(reach() * 2.0, reach() * 2.0);
}

qreal PetWidget::friendRestX() const
{
    return reach() * 1.75;
}

qreal PetWidget::sceneHalfWidth() const
{
    return friendRestX() + reach();
}

QRectF PetWidget::availableArea() const
{
    const QScreen *sc = QGuiApplication::screenAt(m_center.toPoint());
    if (!sc)
        sc = QGuiApplication::primaryScreen();
    return sc ? QRectF(sc->availableGeometry()) : QRectF(0, 0, 1280, 800);
}

QRectF PetWidget::speechRect(bool visitor) const
{
    const QSizeF size = visitor ? m_friendBubbleSize : m_bubbleSize;
    const QRectF area = availableArea().adjusted(4, 4, -4, -4);
    const qreal cx = m_center.x() + (visitor ? m_friendX : 0.0);
    QRectF rect(QPointF(cx - size.width() / 2, m_center.y() - reach() - size.height() - 12 * s()), size);
    rect.moveLeft(qBound(area.left(), rect.left(), qMax(area.left(), area.right() - rect.width())));
    // Near the ceiling, speak below the pet instead of moving its body.
    if (rect.top() < area.top())
        rect.moveTop(m_center.y() + reach() + 12 * s());
    if (visitor && !m_bubble.isEmpty()) {
        const QRectF other = speechRect(false);
        if (rect.adjusted(-8 * s(), -8 * s(), 8 * s(), 8 * s()).intersects(other)) {
            rect.moveBottom(other.top() - 12 * s());
            if (rect.top() < area.top())
                rect.moveTop(other.bottom() + 12 * s());
        }
    }
    return rect;
}

void PetWidget::updateWindowGeometry()
{
    const qreal r = reach();
    QRectF bounds(m_center - QPointF(r, r), QSizeF(r * 2, r * 2));
    if (m_friendAlpha > 0.0)
        bounds = bounds.united(QRectF(m_center + QPointF(m_friendX - r, -r), QSizeF(r * 2, r * 2)));
    if (!m_bubble.isEmpty())
        bounds = bounds.united(speechRect(false).adjusted(-2, -12 * s(), 2, 12 * s()));
    if (!m_friendBubble.isEmpty())
        bounds = bounds.united(speechRect(true).adjusted(-2, -12 * s(), 2, 12 * s()));
    for (const Particle &fx : m_particles) {
        const qreal pad = fx.size * 1.6;
        bounds = bounds.united(QRectF(fx.pos - QPointF(pad, pad), QSizeF(pad * 2, pad * 2)));
    }
    const QRect target = bounds.toAlignedRect();
    if (target != geometry())
        setGeometry(target);
}

void PetWidget::clampToScreen()
{
    const QRectF area = availableArea();
    const qreal  r    = reach();

    // during an encounter the visitor stands to the right, so the pet has to
    // keep that much clear of the screen edge as well
    const qreal rightRoom = m_scene != Scene::None ? sceneHalfWidth() : r;

    const qreal minX = area.left() + r;
    const qreal maxX = qMax(minX, area.right() - rightRoom);
    const qreal minY = area.top() + r;
    const qreal maxY = qMax(minY, area.bottom() - r);

    const qreal impact = length(m_velocity);
    bool hit = false;
    if (m_center.x() < minX) { m_center.setX(minX); m_velocity.setX(qAbs(m_velocity.x())); hit = true; }
    if (m_center.x() > maxX) { m_center.setX(maxX); m_velocity.setX(-qAbs(m_velocity.x())); hit = true; }
    if (m_center.y() < minY) { m_center.setY(minY); m_velocity.setY(qAbs(m_velocity.y())); hit = true; }
    if (m_center.y() > maxY) { m_center.setY(maxY); m_velocity.setY(-qAbs(m_velocity.y())); hit = true; }

    // A forceful throw into the desktop boundary becomes a tiny slapstick scene.
    if (hit && impact > 185.0 * s() && m_scene == Scene::None &&
        (m_state == State::Wander || m_state == State::Play))
        stunAtEdge();
}

void PetWidget::pickTarget()
{
    const QRectF area = availableArea();
    const QSizeF box  = petBox();
    const qreal m = qMax(box.width(), box.height()) * 0.7;

    m_target = QPointF(frand(area.left() + m, area.right() - m),
                       frand(area.top() + m, area.bottom() - m));
}

// -------------------------------------------------------------- options ----

void PetWidget::setPetScale(int percent)
{
    const qreal next = qBound(60, percent, 200) / 100.0;
    if (qFuzzyCompare(next, m_scale))
        return;
    m_scale = next;
    if (!m_bubble.isEmpty())
        say(m_bubble, qRound(m_bubbleLeft * 1000.0));   // remeasure the bubble
    if (!m_friendBubble.isEmpty())
        friendSay(m_friendBubble, qRound(m_friendBubbleLeft * 1000.0));
    clampToScreen();
    updateWindowGeometry();
    update();
}

void PetWidget::setSpeedFactor(double factor)
{
    m_speed = qBound(0.2, factor, 3.0);
}

void PetWidget::setFollowCursor(bool on)
{
    if (m_follow == on)
        return;
    m_follow = on;
    wake();
    if (on)
        say(tr("on your six"), 2000);
}

void PetWidget::setChatter(bool on)
{
    m_chatter = on;
    if (!on) {
        m_bubble.clear();
        m_bubbleLeft = 0;
        m_friendBubble.clear();
        m_friendBubbleLeft = 0;
        m_talkReply.clear();
        updateWindowGeometry();
        update();
    }
}

void PetWidget::setAlwaysOnTop(bool on)
{
    if (bool(windowFlags() & Qt::WindowStaysOnTopHint) == on)
        return;

    const bool wasVisible = isVisible();
    setWindowFlag(Qt::WindowStaysOnTopHint, on);
    if (wasVisible) {
        show();                      // re-applying flags needs a fresh show()
        updateWindowGeometry();
    }
}

void PetWidget::setColor(const QColor &c)
{
    m_color = c;
    update();
}

// ------------------------------------------------------------- talking ----

void PetWidget::say(const QString &text, int ms)
{
    if (text.isEmpty() || !m_chatter)
        return;

    m_talkReply.clear();
    m_bubble     = text;
    m_bubbleLeft = qMax(ms, qBound(2200, 900 + int(text.size()) * 45, 6500)) / 1000.0;
    m_sinceChatter = 0.0;

    QFont f = font();
    f.setPixelSize(qMax(9, qRound(12.0 * s())));
    const QFontMetricsF fm(f);

    const qreal maxW = 210.0 * s();
    const QRectF r = fm.boundingRect(QRectF(0, 0, maxW, 600),
                                     Qt::TextWordWrap | Qt::AlignCenter, text);
    m_bubbleSize = QSizeF(qMin(maxW, r.width()) + 24.0 * s(),
                          r.height() + 18.0 * s());

    clampToScreen();
    updateWindowGeometry();
    update();
}

void PetWidget::celebrate()
{
    wake();
    m_state    = State::Excited;
    m_stateFor = 0.0;
    m_squash   = 1.0;
}

void PetWidget::spawnBurst(ParticleKind kind, int count, const QColor &color)
{
    count = qBound(0, count, 32);
    while (m_particles.size() + count > 96)
        m_particles.removeFirst();
    for (int i = 0; i < count; ++i) {
        Particle fx;
        fx.kind  = kind;
        fx.color = color;
        fx.pos   = m_center + QPointF(frand(-18.0, 18.0) * s(), frand(-15.0, 10.0) * s());
        fx.ttl   = kind == ParticleKind::Heart ? frand(0.9, 1.45) : frand(0.45, 0.85);
        fx.life  = fx.ttl;
        fx.size  = frand(3.5, 7.0) * s();

        if (kind == ParticleKind::Heart)
            fx.velocity = QPointF(frand(-24.0, 24.0), frand(-72.0, -42.0)) * s();
        else if (kind == ParticleKind::Crumb)
            fx.velocity = QPointF(frand(-52.0, 52.0), frand(-85.0, -30.0)) * s();
        else
            fx.velocity = QPointF(frand(-100.0, 100.0), frand(-105.0, 55.0)) * s();
        m_particles.append(fx);
    }
}

void PetWidget::updateParticles(qreal dt)
{
    for (int i = m_particles.size() - 1; i >= 0; --i) {
        Particle &fx = m_particles[i];
        fx.life -= dt;
        if (fx.life <= 0.0) {
            m_particles.removeAt(i);
            continue;
        }
        if (fx.kind != ParticleKind::Heart)
            fx.velocity.setY(fx.velocity.y() + 145.0 * dt * s());
        fx.pos += fx.velocity * dt;
        fx.velocity *= std::pow(0.18, dt);
    }
}

void PetWidget::stunAtEdge()
{
    m_state = State::Stunned;
    m_stateFor = 0.0;
    m_velocity = QPointF();
    m_squash = 1.0;
    spawnBurst(ParticleKind::Spark, 14, QColor(255, 208, 70));
    ++m_bonks;
    static const QStringList bonkLines{
        QStringLiteral("FUCK—I got stunned by the edge of the screen"),
        QStringLiteral("BONK. the invisible wall remains undefeated."),
        QStringLiteral("who put a whole edge right THERE?!"),
        QStringLiteral("I saw three taskbars for a second."),
        QStringLiteral("membrane: dented. pride: absolutely destroyed."),
        QStringLiteral("physics just filed a complaint against my face."),
    };
    if (m_chatter)
        say(m_bonks == 1 ? bonkLines.first() : pick(bonkLines), 3300);
}

void PetWidget::talk()
{
    static const QVector<QPair<QString, QString>> chats{
        {tr("I tried to leave the screen earlier."), tr("turns out the edge is load-bearing. my face checked.")},
        {tr("I have decided to start a tiny business."), tr("we sell crumbs. you supply the crumbs. I eat the inventory.")},
        {tr("a dust particle challenged me to a fight."), tr("we're roommates now. rent is brutal.")},
        {tr("I wrote a poem. ready?"), tr("small cell. big yell. snack fell. oh hell.")},
        {tr("I named my left flagellum Gerald."), tr("the other two are in a union. negotiations continue.")},
        {tr("do you ever forget why you entered a room?"), tr("I only have one cell and I STILL do that.")},
        {tr("your cursor and I had a private meeting."), tr("it made several excellent points. literally just points.")},
        {tr("I am training for the desktop olympics."), tr("my event is competitive lying down. watch this.")},
        {tr("I found a shortcut to happiness."), tr("right-click. give a snack. astonishing technology.")},
        {tr("today I successfully did absolutely nothing."), tr("tomorrow I will try to do it faster.")},
        {tr("Kevin says I'm dramatic."), tr("KEVIN ATE MY HOUSE. it was agar, but STILL.")},
        {tr("I don't actually read your files or your screen."), tr("I just follow the cursor and make confident guesses. tiny actor.")},
    };
    int next = QRandomGenerator::global()->bounded(int(chats.size()));
    if (next == m_talkIndex)
        next = (next + 1 + QRandomGenerator::global()->bounded(int(chats.size()) - 1)) % chats.size();
    m_talkIndex = next;
    endEncounter(true);
    wake();
    m_state = State::Pause;
    m_stateFor = 0;
    m_velocity = QPointF();
    say(chats[next].first, 3000);
    if (m_chatter) {
        m_talkReply = chats[next].second;
        m_talkReplyIn = m_bubbleLeft + 0.3;
        m_pauseFor = m_talkReplyIn + 5.0;
    }
}

void PetWidget::pet()
{
    const State before = m_state;
    celebrate();
    m_affection = qMin(10.0, m_affection + 1.0);
    spawnBurst(ParticleKind::Heart, 2 + qRound(m_affection / 3.0), QColor(255, 92, 156));
    if (m_chatter) {
        if (before == State::Sleep)
            say(tr("five more minutes... but keep doing that"), 2300);
        else if (before == State::Play)
            say(tr("you caught me. this changes nothing."), 2200);
        else if (before == State::Orbit)
            say(tr("affection logged. experiment still running."), 2300);
        else
            say(m_affection > 7.0 ? tr("okay okay I love you too") : pick(petLines()), 2100);
    }
}

void PetWidget::feed()
{
    celebrate();
    m_affection = qMin(10.0, m_affection + 2.0);
    spawnBurst(ParticleKind::Crumb, 12, QColor(255, 196, 82));
    if (m_chatter)
        say(pick(snackLines()), 2400);
}

void PetWidget::startPlay()
{
    if (m_scene != Scene::None)
        endEncounter(true);
    wake();
    m_state = State::Play;
    m_stateFor = 0.0;
    m_playLeft = 12.0;
    m_playCatchCooldown = 0.0;
    if (m_chatter)
        say(tr("catch the cursor? you're on."), 2200);
}

void PetWidget::orbit(const QPoint &screenCenter, qreal radius)
{
    wake();
    m_orbitCenter   = QPointF(screenCenter);
    m_orbitRadius   = qMax<qreal>(40.0, radius);
    // Start from wherever the pet currently is so it slides into orbit
    // instead of teleporting.
    const QPointF d = m_center - m_orbitCenter;
    m_orbitAngle    = std::atan2(d.y(), d.x());
    m_orbitBanterIn = frand(6.0, 10.0);
    m_state         = State::Orbit;
    m_stateFor      = 0.0;
    m_scene         = Scene::None;    // orbit and encounter don't mix
}

void PetWidget::stopOrbit()
{
    if (m_state == State::Orbit) {
        m_state    = State::Wander;
        m_stateFor = 0.0;
        pickTarget();
    }
}

// Lines the pet chatters while orbiting the flask during a crypt run.
// Vibe: nosy, prying, mildly threatening. It wants to know who the binary
// is for, what it's going to do, and whether you've been showering.
static const QStringList &protectBanterLines()
{
    static const QStringList lines{
        QStringLiteral("doing… thinking…"),
        QStringLiteral("tell me who you're going to infect. c'mon. c'mon."),
        QStringLiteral("you smell…"),
        QStringLiteral("who's the target. spill."),
        QStringLiteral("thinking. hard. very hard. observe."),
        QStringLiteral("so who's the lucky victim"),
        QStringLiteral("is it your ex. tell me it's your ex."),
        QStringLiteral("does this thing have a name? give it a name."),
        QStringLiteral("I'm not judging. I am. but tell me anyway."),
        QStringLiteral("this is for a friend, right. right??"),
        QStringLiteral("you last showered when."),
        QStringLiteral("hmmmm smells like felony 🕵️"),
        QStringLiteral("processing… processing… wow you're weird"),
        QStringLiteral("who did this to you. who hurt you."),
        QStringLiteral("this binary is gonna do WHAT to who now"),
        QStringLiteral("say the name. out loud. I dare you."),
        QStringLiteral("I can smell the poor decisions from here"),
        QStringLiteral("you look guilty already and it hasn't even shipped"),
        QStringLiteral("thonking 🧠"),
        QStringLiteral("give me the IP. no reason."),
        QStringLiteral("is this the payload or are you just happy to see me"),
        QStringLiteral("hey. hey. HEY. tell me the plan."),
        QStringLiteral("your keyboard smells like Doritos and sin"),
        QStringLiteral("I bet you don't even have a target list"),
        QStringLiteral("who's the mark. names. addresses. blood types."),
        QStringLiteral("did you at least get consent lol"),
        QStringLiteral("is this a corporate hit or personal beef"),
        QStringLiteral("I need three (3) reasons and none of them can be 'lol'"),
        QStringLiteral("smells like C2 in here 👃"),
        QStringLiteral("do NOT tell me you didn't test this on a VM first"),
        QStringLiteral("does mommy know what you're doing"),
        QStringLiteral("computing thoughts about YOU specifically 👀"),
        QStringLiteral("who signed off on this. anyone. Bueller."),
        QStringLiteral("okay but WHY tho"),
        QStringLiteral("nose in your business, always"),
        QStringLiteral("what does your therapist think of all this"),
        QStringLiteral("cook, king. cook. but tell me who dies first."),
        QStringLiteral("… you good?"),
        QStringLiteral("thinking about your DNS history rn"),
        QStringLiteral("your MAC address is showing"),
        QStringLiteral("bro your fan is louder than my convictions"),
        QStringLiteral("real quick — how many laws are we breaking"),
        QStringLiteral("this ain't for grandma is it"),
        QStringLiteral("I smell OPSEC violations from a mile out"),
        QStringLiteral("who taught you to type. I want a word."),
        QStringLiteral("you got a license for that binary?"),
        QStringLiteral("is the C2 in a country I'm allowed to say"),
        QStringLiteral("thinking about your browser bookmarks. concerning."),
        QStringLiteral("did you clean your webcam lens. I saw."),
        QStringLiteral("thinking. thonking. contemplating your crimes."),
        QStringLiteral("what's the plan after this — witness protection?"),
        QStringLiteral("you talk to real people ever or just me"),
        QStringLiteral("bro when did you last blink"),
        QStringLiteral("sooo… hypothetically… how many machines"),
        QStringLiteral("wait wait wait — is this THE guy's PC"),
        QStringLiteral("your posture is worse than your OPSEC"),
        QStringLiteral("thinking about how quiet you got when I asked"),
        QStringLiteral("your search history could power a novel"),
        QStringLiteral("smells like a Monday and a mistake"),
        QStringLiteral("come on, one name, off the record"),
        QStringLiteral("processing your life choices, please hold"),
        QStringLiteral("you got a burner for this or are you RAW-dogging it"),
        QStringLiteral("your keyboard has crumbs. focus on that first."),
        QStringLiteral("what does the target's mother look like"),
        QStringLiteral("hypothetical, hypothetical, real question: names?"),
        QStringLiteral("you have that 'I moved back home' energy"),
        QStringLiteral("bro I can HEAR your CPU sweating"),
        QStringLiteral("would you say this is 'harmless mischief' or a felony"),
        QStringLiteral("I would like to file a witness statement in advance"),
        QStringLiteral("who hurt you. specifically. IP please."),
        QStringLiteral("this smells expensive. legally."),
        QStringLiteral("thinking about how you haven't touched grass this month"),
        QStringLiteral("is the target rich. asking for me."),
        QStringLiteral("show me the pcap. no reason."),
        QStringLiteral("does your VPN cover this or is it more of a vibe VPN"),
        QStringLiteral("processing… processing… you disappoint me"),
        QStringLiteral("hypothetically who's paying for this — cash? crypto? feelings?"),
        QStringLiteral("your USB port smells like drama"),
        QStringLiteral("tell me the mark works for the government. do it."),
        QStringLiteral("thinking about how bold you are for someone with 3% battery"),
        QStringLiteral("did you at least code the killswitch. DID YOU."),
        QStringLiteral("bruh whose subnet"),
        QStringLiteral("please tell me the target isn't a hospital 🥲"),
        QStringLiteral("is this for love or money. wrong answer either way."),
        QStringLiteral("thinking about your DMs. I have theories."),
        QStringLiteral("hey. real talk. do you sleep."),
        QStringLiteral("your build folder is cursed and so are you"),
        QStringLiteral("bro your soul stinks a lil"),
        QStringLiteral("we doing terrorism or just tomfoolery, need to know"),
        QStringLiteral("would hold up in court? yes/no/spectacularly no?"),
        QStringLiteral("thinking about how you spelled 'recieve' three times today"),
        QStringLiteral("nose sniffing intensifies 👃👃"),
        QStringLiteral("this is the part of the movie where I'd narc"),
    };
    return lines;
}

void PetWidget::react(Mood mood)
{
    // themed one-liners so the pet actually reacts to what the app is doing.
    static const QStringList protectStart{
        QStringLiteral("virtualizing your crap. hold my plasmid."),
        QStringLiteral("eating this binary alive 🦠"),
        QStringLiteral("wrapping your code in bubble wrap and spite"),
        QStringLiteral("good luck reversing THIS, nerds"),
        QStringLiteral("chewing opcodes. do not disturb."),
        QStringLiteral("time to make some reverse engineer cry"),
        QStringLiteral("into the flask I go. wish me luck. or don't."),
        QStringLiteral("initiating operation SPICY BINARY"),
        QStringLiteral("hold on I need to put my safety goggles on"),
        QStringLiteral("stirring the pot with a flagellum, hell yeah"),
        QStringLiteral("cooking. mid-cook. do NOT open the flask"),
        QStringLiteral("this one's for the shareholders"),
        QStringLiteral("virtualizing louder than my mother yelled at me"),
        QStringLiteral("look busy, IDA is watching"),
    };
    static const QStringList protectDone{
        QStringLiteral("done. that binary's got a restraining order now."),
        QStringLiteral("protected. cracked in 20 years, maybe."),
        QStringLiteral("boom. unreadable. you're welcome."),
        QStringLiteral("sealed it. tighter than my cell wall."),
        QStringLiteral("ship it before I change my mind"),
        QStringLiteral("chef's kiss. binary is now inedible."),
        QStringLiteral("done. go outside. see the sun. brag."),
        QStringLiteral("Ghidra just filed for early retirement"),
        QStringLiteral("locked. bolted. yeeted into the void."),
        QStringLiteral("that .exe fears god now"),
        QStringLiteral("100% cooked. medium rare. delicious."),
        QStringLiteral("victory hiss 🐍"),
    };
    static const QStringList protectFail{
        QStringLiteral("it choked. skill issue. not mine."),
        QStringLiteral("that function fought back. rude."),
        QStringLiteral("nope. that one's cursed. check the log."),
        QStringLiteral("failed. don't look at me like that."),
        QStringLiteral("ugh. give me a function that isn't garbage."),
        QStringLiteral("bvm quit. bvm has a family."),
        QStringLiteral("flask exploded. metaphorically. mostly."),
        QStringLiteral("I did NOT sign the waiver for this"),
        QStringLiteral("the binary sends its regards. it's fine btw."),
        QStringLiteral("try again. or don't. I'm not your dad."),
        QStringLiteral("segfault energy. respect."),
    };
    static const QStringList mining{
        QStringLiteral("mining? in this heat? for you? fine."),
        QStringLiteral("digging up hashes, breaking my flagella"),
        QStringLiteral("your GPU is screaming and so am I"),
        QStringLiteral("we're rich. spiritually. not financially."),
        QStringLiteral("your electricity bill is my problem now"),
        QStringLiteral("nonce found. felt cute."),
        QStringLiteral("fan curve? never heard of her."),
        QStringLiteral("proof of work, proof of pain"),
    };
    static const QStringList angry{
        QStringLiteral("f*ck it, I'm gonna multiply"),
        QStringLiteral("keep clicking. see what evolves."),
        QStringLiteral("I have a nucleoid and a temper"),
        QStringLiteral("touch me one more time"),
        QStringLiteral("I will chew through your motherboard"),
        QStringLiteral("release the phages"),
    };

    const QStringList *pool = nullptr;
    switch (mood) {
    case Mood::ProtectStart: pool = &protectStart; wake(); m_squash = 0.8; break;
    case Mood::ProtectDone:  pool = &protectDone;  celebrate();            break;
    case Mood::ProtectFail:  pool = &protectFail;  wake();                 break;
    case Mood::Mining:       pool = &mining;       wake();                 break;
    case Mood::Angry:        pool = &angry;        wake(); m_squash = 1.0; break;
    case Mood::Idle:         pool = &idleLines();                          break;
    }
    if (pool && !pool->isEmpty())
        say(pick(*pool), 5000);
    else
        say(pick(idleLines()), 5000);
}

void PetWidget::wake()
{
    if (m_state == State::Sleep) {
        m_state    = State::Wander;
        m_stateFor = 0.0;
        pickTarget();
    }
}

void PetWidget::friendSay(const QString &text, int ms)
{
    if (!m_chatter || text.isEmpty())
        return;
    m_friendBubble     = text;
    m_friendBubbleLeft = qMax(ms, qBound(2200, 900 + int(text.size()) * 45, 6500)) / 1000.0;

    QFont f = font();
    f.setPixelSize(qMax(9, qRound(11.0 * s())));
    const QFontMetricsF fm(f);

    const qreal maxW = 170.0 * s();
    const QRectF r = fm.boundingRect(QRectF(0, 0, maxW, 600),
                                     Qt::TextWordWrap | Qt::AlignCenter, text);
    m_friendBubbleSize = QSizeF(qMin(maxW, r.width()) + 22.0 * s(),
                                r.height() + 16.0 * s());
    updateWindowGeometry();
}

// --------------------------------------------------------- the encounter ----

void PetWidget::startEncounter()
{
    if (m_scene != Scene::None || m_state == State::Held)
        return;

    wake();
    m_state    = State::Wander;
    m_stateFor = 0.0;

    // Head for the floor, but only somewhere with room to the right for the
    // visitor — walking into a corner and then talking to the wall looks daft.
    const QRectF area = availableArea();
    const qreal  r    = reach();
    const qreal  lo   = area.left() + r + 12.0;
    const qreal  hi   = area.right() - sceneHalfWidth() - 12.0;

    const qreal x = hi > lo ? qBound(lo, m_center.x(), hi) : area.center().x();
    const qreal y = qMax(area.top() + r, area.bottom() - r - 16.0);
    m_target = QPointF(x, y);

    m_friendX       = friendRestX() + r * 0.9;
    m_friendAlpha   = 0.0;
    m_friendHeading = 180.0;
    m_dividePulse   = 0.0;
    const int count = encounterScripts().size();
    m_scenario = QRandomGenerator::global()->bounded(count);
    if (count > 1 && m_scenario == m_lastScenario)
        m_scenario = (m_scenario + 1) % count;
    m_lastScenario = m_scenario;
    setSceneStep(Scene::Descend);
}

void PetWidget::setSceneStep(Scene step)
{
    m_scene  = step;
    m_sceneT = 0.0;
    updateWindowGeometry();
}

void PetWidget::endEncounter(bool immediate)
{
    if (m_scene == Scene::None)
        return;

    if (!immediate && m_scene != Scene::Farewell) {
        setSceneStep(Scene::Farewell);      // let the visitor swim off first
        return;
    }

    m_scene            = Scene::None;
    m_sceneT           = 0.0;
    m_friendAlpha      = 0.0;
    m_friendBubble.clear();
    m_friendBubbleLeft = 0.0;
    m_dividePulse      = 0.0;
    m_sceneIn          = frand(150.0, 300.0);

    m_state    = State::Wander;
    m_stateFor = 0.0;
    pickTarget();
    updateWindowGeometry();
}

void PetWidget::updateScene(qreal dt)
{
    if (m_friendBubbleLeft > 0.0) {
        m_friendBubbleLeft -= dt;
        if (m_friendBubbleLeft <= 0.0) {
            m_friendBubble.clear();
            updateWindowGeometry();
        }
    }

    if (m_scene == Scene::None) {
        if (!m_chatter || m_follow || m_state == State::Held ||
            !m_talkReply.isEmpty() || m_bubbleLeft > 0.0)
            return;
        m_sceneIn -= dt;
        if (m_sceneIn <= 0.0 && (m_state == State::Wander || m_state == State::Pause))
            startEncounter();
        return;
    }

    m_sceneT      += dt;
    m_friendPhase += dt * 4.2;

    const qreal rest = friendRestX();
    const EncounterScript &script = encounterScripts().at(m_scenario);

    switch (m_scene) {
    case Scene::Descend:
        // handled by the steering in tick(); advance once it has settled
        if (length(m_target - m_center) < 14.0 || m_sceneT > 9.0) {
            m_velocity *= 0.2;
            setSceneStep(Scene::Greet);
        }
        break;

    case Scene::Greet: {
        const qreal t = ease(m_sceneT / 1.1);
        m_friendAlpha = t;
        m_friendX     = rest + reach() * 0.9 * (1.0 - t);
        if (m_sceneT >= 1.15) {
            setSceneStep(Scene::LineOne);
            say(QString::fromUtf8(script.petOne), 2800);
        }
        break;
    }

    case Scene::LineOne:
        m_friendX     = rest;
        m_friendAlpha = 1.0;
        if (m_sceneT > 2.2 && m_bubbleLeft <= 0.0) {
            setSceneStep(Scene::LineTwo);
            friendSay(QString::fromUtf8(script.friendOne), 2400);
        }
        break;

    case Scene::LineTwo:
        if (m_sceneT > 1.9 && m_friendBubbleLeft <= 0.0) {
            setSceneStep(Scene::LineThree);
            say(QString::fromUtf8(script.petTwo), 2500);
            m_squash = 1.0;
        }
        break;

    case Scene::LineThree:
        if (m_sceneT > 1.5 && m_bubbleLeft <= 0.0) {
            setSceneStep(Scene::Divide);
            friendSay(QString::fromUtf8(script.friendTwo), 2100);
            if (m_scenario == 3)
                spawnBurst(ParticleKind::Spark, 10, QColor(108, 235, 210));
        }
        break;

    case Scene::Divide:
        m_dividePulse = std::sin(qBound(0.0, m_sceneT / 1.9, 1.0) * M_PI);
        if (m_scenario == 1) {
            m_squash = qMax(m_squash, m_dividePulse * 0.75);
            m_friendHeading = 180.0 + std::sin(m_sceneT * 24.0) * 8.0;
        } else if (m_scenario == 2) {
            m_friendX = rest + m_dividePulse * reach() * 0.42;
        }
        if (m_sceneT > 1.9 && m_friendBubbleLeft <= 0.0) {
            m_dividePulse = 0.0;
            setSceneStep(Scene::Farewell);
        }
        break;

    case Scene::Farewell: {
        const qreal t = ease(m_sceneT / 1.5);
        m_friendX     = rest + reach() * 1.2 * t;
        m_friendAlpha = 1.0 - t;
        if (m_sceneT > 1.55)
            endEncounter(true);
        break;
    }

    case Scene::None:
        break;
    }

    updateWindowGeometry();
}

// ---------------------------------------------------------------- loop ----

void PetWidget::tick()
{
    if (!isVisible() || m_menuOpen)
        return;

    const qreal dt = qBound(0.001, m_clock.isValid() ? m_clock.restart() / 1000.0 : 0.016, 0.05);
    m_stateFor += dt;
    m_phase    += dt * (3.0 + m_speed * 3.0);
    m_affection = qMax(0.0, m_affection - dt * 0.025);
    m_playCatchCooldown = qMax(0.0, m_playCatchCooldown - dt);
    updateParticles(dt);
    if (!m_talkReply.isEmpty()) {
        m_talkReplyIn -= dt;
        if (m_talkReplyIn <= 0.0) {
            const QString reply = m_talkReply;
            say(reply, 4200);
        }
    }

    // ------------------------------------------------------------ speech --
    if (m_bubbleLeft > 0.0) {
        m_bubbleLeft -= dt;
        if (m_bubbleLeft <= 0.0) {
            m_bubble.clear();
            updateWindowGeometry();
        }
    } else if (m_chatter && m_scene == Scene::None && m_talkReply.isEmpty() &&
               (m_state == State::Wander || m_state == State::Pause)) {
        m_sinceChatter += dt;
        if (m_sinceChatter > m_chatterAt) {
            m_sinceChatter = 0.0;
            m_chatterAt    = frand(25.0, 60.0);
            if (!m_follow && QRandomGenerator::global()->bounded(4) == 0)
                talk();
            else
                say(pick(idleLines()));
        }
    }

    // ------------------------------------------------------------- blink --
    if (m_blinkFor > 0.0) {
        m_blinkFor -= dt;
    } else {
        m_blinkIn -= dt;
        if (m_blinkIn <= 0.0) {
            m_blinkFor = 0.13;
            m_blinkIn  = frand(2.2, 6.5);
        }
    }

    updateScene(dt);

    // ---------------------------------------------------------- movement --
    const qreal maxSpeed = 128.0 * m_speed * s();
    QPointF desired;

    if (m_state == State::Held) {
        desired = QPointF();
    } else if (m_scene != Scene::None) {
        if (m_scene == Scene::Descend) {
            // ease into the meeting spot instead of stopping dead on arrival
            const QPointF delta = m_target - m_center;
            const qreal   dist  = length(delta);
            if (dist > 6.0)
                desired = delta / dist * maxSpeed
                          * qMin(1.0, dist / (reach() * 2.5) + 0.30);
        }
    } else if (m_follow && m_state == State::Wander) {
        const QPointF delta = QPointF(QCursor::pos()) - m_center;
        const qreal   dist  = length(delta);
        const qreal   keep  = 110.0 * s();
        if (dist > keep)
            desired = delta / dist * maxSpeed * qMin(1.6, dist / (keep * 3.0) + 0.6);
        m_state = State::Wander;
    } else {
        switch (m_state) {
        case State::Wander: {
            const QPointF delta = m_target - m_center;
            const qreal   dist  = length(delta);
            if (dist < 26.0) {
                m_state    = State::Pause;
                m_stateFor = 0.0;
                m_pauseFor = frand(1.4, 7.0);
            } else {
                desired = delta / dist * maxSpeed;
            }
            break;
        }
        case State::Pause:
            if (m_stateFor > m_pauseFor) {
                if (m_pauseFor > 5.3 && QRandomGenerator::global()->bounded(3) == 0) {
                    m_state    = State::Sleep;
                    m_stateFor = 0.0;
                } else {
                    m_state    = State::Wander;
                    m_stateFor = 0.0;
                    pickTarget();
                }
            }
            break;
        case State::Sleep:
            break;
        case State::Excited:
            if (m_stateFor > 1.6) {
                m_state    = State::Wander;
                m_stateFor = 0.0;
                pickTarget();
            }
            break;
        case State::Held:
            break;
        case State::Orbit: {
            // Advance the orbit angle at ~40 deg/s (scales with the speed
            // slider). The target sits on the orbit circle; velocity is
            // whatever gets us there, so the motion feels curved rather
            // than snapping onto the ring.
            m_orbitAngle += dt * qDegreesToRadians(40.0) * (0.6 + 0.4 * m_speed);
            const QPointF ring(m_orbitCenter.x() + m_orbitRadius * std::cos(m_orbitAngle),
                               m_orbitCenter.y() + m_orbitRadius * std::sin(m_orbitAngle));
            const QPointF delta = ring - m_center;
            const qreal dist = length(delta);
            if (dist > 1.0)
                desired = delta / dist * maxSpeed * qMin(1.5, dist / (reach() * 2.0) + 0.6);

            // Cackle occasionally while orbiting.
            m_orbitBanterIn -= dt;
            if (m_orbitBanterIn <= 0.0) {
                m_orbitBanterIn = frand(6.0, 11.0);
                if (m_chatter && m_bubbleLeft <= 0.0)
                    say(pick(protectBanterLines()), 5000);
            }
            break;
        }
        case State::Play: {
            m_playLeft -= dt;
            if (m_playLeft <= 0.0) {
                m_state = State::Pause;
                m_stateFor = 0.0;
                m_pauseFor = 2.0;
                if (m_chatter)
                    say(tr("good game. I remain undefeated."), 2300);
                break;
            }

            const QPointF cursor(QCursor::pos());
            const QPointF playfulOffset(std::cos(m_phase * 1.7) * 24.0 * s(),
                                        std::sin(m_phase * 2.2) * 18.0 * s());
            const QPointF delta = cursor + playfulOffset - m_center;
            const qreal dist = length(delta);
            if (dist > 3.0)
                desired = delta / dist * maxSpeed * 1.35;

            if (dist < 34.0 * s() && m_playCatchCooldown <= 0.0) {
                m_playCatchCooldown = 1.2;
                m_squash = 1.0;
                spawnBurst(ParticleKind::Spark, 7, m_color.lighter(145));
            }
            break;
        }
        case State::Stunned:
            if (m_stateFor > 1.8) {
                m_state = State::Wander;
                m_stateFor = 0.0;
                pickTarget();
            }
            break;
        }
    }

    if (m_state != State::Held) {
        m_velocity += (desired - m_velocity) * qMin(1.0, dt * 3.2);
        m_center   += m_velocity * dt;
        clampToScreen();
    }

    // -------------------------------------------------------- appearance --
    const qreal speedNow = length(m_velocity);
    qreal wantHeading = m_heading;
    bool  turn = false;

    if (m_scene != Scene::None && m_scene != Scene::Descend) {
        wantHeading = 0.0;              // face the visitor for the whole chat
        turn = true;
    } else if (speedNow > 6.0) {
        wantHeading = qRadiansToDegrees(std::atan2(m_velocity.y(), m_velocity.x()));
        turn = true;
    }

    if (turn) {
        qreal diff = wantHeading - m_heading;
        while (diff >  180.0) diff -= 360.0;
        while (diff < -180.0) diff += 360.0;
        m_heading += diff * qMin(1.0, dt * 5.0);
    }

    m_bob = std::sin(m_phase * 2.0) * qMin(3.0, speedNow / 40.0) * s();
    if (m_state == State::Excited)
        m_bob = -std::fabs(std::sin(m_phase * 6.0)) * 10.0 * s();
    else if (m_state == State::Stunned) {
        m_bob = std::sin(m_phase * 7.0) * 3.0 * s();
        m_heading += std::sin(m_phase * 5.0) * dt * 36.0;
    }
    m_squash = qMax(0.0, m_squash - dt * 1.6);

    m_trailIn -= dt;
    if (m_state == State::Play && speedNow > 80.0 && m_trailIn <= 0.0) {
        m_trailIn = 0.08;
        spawnBurst(ParticleKind::Spark, 1, m_color.lighter(140));
    }

    updateWindowGeometry();
    update();
}

// -------------------------------------------------------------- drawing ----

void PetWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const qreal   r       = reach();
    const qreal   bodyLen = bodyLength();
    const QPointF petCenter = m_center - QPointF(x(), y());

    // A soft proximity glow makes the pet feel aware before it is even clicked.
    const qreal cursorDistance = length(QPointF(QCursor::pos()) - m_center);
    const qreal hover = 1.0 - qBound(0.0, cursorDistance / (r * 1.65), 1.0);
    {
        QRadialGradient glow(petCenter, r * (0.72 + hover * 0.18));
        QColor glowColor = m_color.lighter(145);
        glowColor.setAlpha(qRound(hover * hover * 45.0));
        glow.setColorAt(0.0, glowColor);
        glowColor.setAlpha(0);
        glow.setColorAt(1.0, glowColor);
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(petCenter, r, r);
    }

    // ---------------------------------------------------- speech bubbles ----
    const auto drawBubble = [&](const QString &text, bool visitor,
                                const QColor &edge, qreal pixels, qreal opacity)
    {
        if (text.isEmpty() || opacity <= 0.01)
            return;

        const QRectF rect = speechRect(visitor).translated(-QPointF(x(), y()));

        QPainterPath path;
        path.addRoundedRect(rect, 12.0 * s(), 12.0 * s());

        QPainterPath tail;
        const qreal anchorX = petCenter.x() + (visitor ? m_friendX : 0.0);
        const qreal tx = qBound(rect.left() + 16 * s(), anchorX, rect.right() - 16 * s());
        const bool below = rect.center().y() > petCenter.y();
        const qreal ty = below ? rect.top() + 1.0 : rect.bottom() - 1.0;
        tail.moveTo(tx - 7.0 * s(), ty);
        tail.lineTo(tx + 7.0 * s(), ty);
        tail.lineTo(tx, ty + (below ? -10.0 : 10.0) * s());
        tail.closeSubpath();
        path.addPath(tail);

        p.save();
        p.setOpacity(opacity);
        p.setPen(QPen(edge, 1.4));
        p.setBrush(QColor(18, 18, 24, 242));
        p.drawPath(path.simplified());

        QFont f = font();
        f.setPixelSize(qMax(9, qRound(pixels * s())));
        p.setFont(f);
        p.setPen(QColor(246, 246, 250));
        p.drawText(rect.adjusted(10.0 * s(), 6.0 * s(), -10.0 * s(), -6.0 * s()),
                   Qt::AlignCenter | Qt::TextWordWrap, text);
        p.restore();
    };

    drawBubble(m_bubble, false, m_color, 12.0, qMin(1.0, m_bubbleLeft / 0.20));
    drawBubble(m_friendBubble, true, Theme::Teal, 11.0,
               m_friendAlpha * qMin(1.0, m_friendBubbleLeft / 0.20));

    // ------------------------------------------------------ one bacterium ----
    const auto drawOne = [&](const QPointF &at, qreal heading, qreal len,
                             const QColor &color, qreal phase, bool blink,
                             bool dizzy, bool smiling, qreal squash, const QPointF &gaze)
    {
        p.save();
        p.translate(at);
        p.rotate(heading);
        if (std::cos(qDegreesToRadians(heading)) < 0.0)
            p.scale(1.0, -1.0);                    // mirror rather than invert
        p.scale(1.0 + squash * 0.12, 1.0 - squash * 0.14);

        paintPetBody(p, len, phase, color);

        const qreal eyeX = len * 0.20;
        const qreal eyeY = len * 0.105;
        const qreal eyeR = len * 0.083;
        for (int i = -1; i <= 1; i += 2) {
            const QPointF e(eyeX, i * eyeY);
            if (dizzy) {
                p.setPen(QPen(QColor(20, 20, 26), qMax<qreal>(1.2, len * 0.025),
                              Qt::SolidLine, Qt::RoundCap));
                p.drawLine(e + QPointF(-eyeR * 0.7, -eyeR * 0.7),
                           e + QPointF( eyeR * 0.7,  eyeR * 0.7));
                p.drawLine(e + QPointF(-eyeR * 0.7,  eyeR * 0.7),
                           e + QPointF( eyeR * 0.7, -eyeR * 0.7));
            } else if (blink) {
                p.setPen(QPen(QColor(20, 20, 26), qMax<qreal>(1.2, len * 0.028),
                              Qt::SolidLine, Qt::RoundCap));
                p.drawLine(e + QPointF(-eyeR, 0), e + QPointF(eyeR, 0));
            } else {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(244, 244, 248));
                p.drawEllipse(e, eyeR, eyeR);
                p.setBrush(QColor(16, 16, 22));
                p.drawEllipse(e + gaze * (eyeR * 0.34), eyeR * 0.5, eyeR * 0.5);
                p.setBrush(QColor(255, 255, 255, 190));
                p.drawEllipse(e + gaze * (eyeR * 0.34) + QPointF(-eyeR * 0.14, -eyeR * 0.16),
                              eyeR * 0.12, eyeR * 0.12);
            }
        }

        if (dizzy) {
            p.setBrush(QColor(25, 21, 34));
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(len * 0.36, 0), len * 0.037, len * 0.05);
        } else if (smiling) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 174, 193, 145));
            p.drawEllipse(QPointF(len * 0.28, -len * 0.19), len * 0.055, len * 0.025);
            p.drawEllipse(QPointF(len * 0.28, len * 0.19), len * 0.055, len * 0.025);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(20, 20, 26), qMax<qreal>(1.2, len * 0.026),
                          Qt::SolidLine, Qt::RoundCap));
            QPainterPath smile;
            smile.moveTo(len * 0.33, -len * 0.047);
            smile.quadTo(len * 0.42, 0, len * 0.33, len * 0.047);
            p.drawPath(smile);
        } else if (!blink) {
            p.setPen(QPen(QColor(20, 20, 26, 190), qMax<qreal>(1.0, len * 0.018),
                          Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(len * 0.36, -len * 0.025), QPointF(len * 0.36, len * 0.025));
        }
        p.restore();
    };

    // the visitor sits behind the pet in z-order
    if (m_friendAlpha > 0.01) {
        p.save();
        p.setOpacity(m_friendAlpha);
        drawOne(QPointF(petCenter.x() + m_friendX, petCenter.y()),
                m_friendHeading, bodyLen * 0.94, Theme::Teal, m_friendPhase,
                false, false,
                m_scene == Scene::LineTwo || m_scene == Scene::Divide || m_scenario == 4,
                m_dividePulse * 0.5, QPointF(0.8, 0.0));
        p.restore();
    }

    // offspring, popping into being between the two on the divide beat
    if (m_dividePulse > 0.01 && m_scenario == 0) {
        const qreal k = m_dividePulse;
        for (int i = -1; i <= 1; i += 2) {
            p.save();
            p.setOpacity(qMin(1.0, k * 1.5) * 0.92);
            p.translate(petCenter.x() + m_friendX * 0.5 + i * m_friendX * 0.22 * k,
                        petCenter.y() - r * 0.30 * k);
            p.scale(0.34 + 0.20 * k, 0.34 + 0.20 * k);
            p.rotate(i * 22.0);
            paintPetBody(p, bodyLen, m_phase * 3.0 + i, i < 0 ? m_color : Theme::Teal);
            p.restore();
        }
    }

    // Each visitor story gets a distinct visual payoff instead of always multiplying.
    if (m_scene == Scene::Divide && m_dividePulse > 0.01 && m_scenario == 3) {
        const QPointF orb(petCenter.x() + m_friendX * 0.5, petCenter.y() - r * 0.18);
        QRadialGradient plasmid(orb, 15.0 * s());
        plasmid.setColorAt(0.0, QColor(255, 255, 255, 245));
        plasmid.setColorAt(0.25, QColor(108, 235, 210, 220));
        plasmid.setColorAt(1.0, QColor(108, 235, 210, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(plasmid);
        p.drawEllipse(orb, 15.0 * s() * m_dividePulse, 15.0 * s() * m_dividePulse);
    } else if (m_scene == Scene::Divide && m_dividePulse > 0.01 && m_scenario == 2) {
        p.setPen(QPen(QColor(m_color.red(), m_color.green(), m_color.blue(),
                             qRound(120.0 * m_dividePulse)), 2.0 * s(), Qt::DashLine));
        for (int i = -1; i <= 1; ++i)
            p.drawLine(QPointF(petCenter.x() - 34.0 * s(), petCenter.y() + i * 9.0 * s()),
                       QPointF(petCenter.x() - 8.0 * s(), petCenter.y() + i * 9.0 * s()));
    }

    const QPointF cursorDelta = QPointF(QCursor::pos()) - m_center;
    const qreal cursorLen = qMax<qreal>(1.0, length(cursorDelta));
    const qreal a = qDegreesToRadians(-m_heading);
    QPointF gaze((cursorDelta.x() * std::cos(a) - cursorDelta.y() * std::sin(a)) / cursorLen,
                 (cursorDelta.x() * std::sin(a) + cursorDelta.y() * std::cos(a)) / cursorLen);
    if (std::cos(qDegreesToRadians(m_heading)) < 0.0)
        gaze.setY(-gaze.y());

    const qreal breathe = 1.0 + std::sin(m_phase * (m_state == State::Sleep ? 0.32 : 0.55)) * 0.018;
    drawOne(QPointF(petCenter.x(), petCenter.y() + m_bob), m_heading, bodyLen * breathe,
            m_color, m_phase * 2.2,
            m_blinkFor > 0.0 || m_state == State::Sleep,
            m_state == State::Stunned,
            m_state == State::Excited || m_state == State::Play ||
                m_scene == Scene::Divide || m_affection > 4.0 ||
                (m_scene != Scene::None && m_scenario == 4),
            m_squash,
            gaze);

    if (m_state == State::Stunned) {
        // Three little stars orbit the head for the entire recovery beat.
        p.setBrush(QColor(255, 218, 97));
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 3; ++i) {
            const qreal angle = m_stateFor * 4.5 + i * M_PI * 2 / 3;
            const QPointF star = petCenter + QPointF(std::cos(angle) * 27 * s(),
                                                     (-29 + std::sin(angle) * 7) * s());
            QPainterPath shape;
            for (int k = 0; k < 10; ++k) {
                const qreal a = k * M_PI / 5 - M_PI / 2;
                const qreal radius = (k % 2 ? 2.3 : 5.4) * s();
                const QPointF point = star + QPointF(std::cos(a), std::sin(a)) * radius;
                if (!k) shape.moveTo(point); else shape.lineTo(point);
            }
            shape.closeSubpath();
            p.drawPath(shape);
        }
    }

    // Lightweight particles provide immediate, readable feedback for every action.
    for (const Particle &fx : m_particles) {
        const qreal alpha = qBound(0.0, fx.life / fx.ttl, 1.0);
        const QPointF at = fx.pos - QPointF(x(), y());
        QColor c = fx.color;
        c.setAlpha(qRound(235.0 * alpha));
        p.setPen(Qt::NoPen);
        p.setBrush(c);

        if (fx.kind == ParticleKind::Heart) {
            QPainterPath heart;
            heart.moveTo(at.x(), at.y() + fx.size * 0.8);
            heart.cubicTo(at.x() - fx.size * 1.5, at.y() - fx.size * 0.15,
                          at.x() - fx.size * 0.65, at.y() - fx.size,
                          at.x(), at.y() - fx.size * 0.35);
            heart.cubicTo(at.x() + fx.size * 0.65, at.y() - fx.size,
                          at.x() + fx.size * 1.5, at.y() - fx.size * 0.15,
                          at.x(), at.y() + fx.size * 0.8);
            p.drawPath(heart);
        } else if (fx.kind == ParticleKind::Crumb) {
            p.drawRoundedRect(QRectF(at.x() - fx.size * 0.6, at.y() - fx.size * 0.35,
                                     fx.size * 1.2, fx.size * 0.7), fx.size * 0.25, fx.size * 0.25);
        } else {
            QPainterPath spark;
            for (int k = 0; k < 8; ++k) {
                const qreal angle = k * M_PI / 4.0;
                const qreal radius = (k % 2 == 0 ? fx.size : fx.size * 0.35) * alpha;
                const QPointF point = at + QPointF(std::cos(angle), std::sin(angle)) * radius;
                if (k == 0)
                    spark.moveTo(point);
                else
                    spark.lineTo(point);
            }
            spark.closeSubpath();
            p.drawPath(spark);
        }
    }

    // ------------------------------------------------------------- sleeping --
    if (m_state == State::Sleep) {
        QFont f = font();
        p.setPen(QPen(m_color));
        for (int i = 0; i < 3; ++i) {
            const qreal t = std::fmod(m_phase * 0.35 + i * 0.33, 1.0);
            f.setPixelSize(qMax(8, qRound((9.0 + i * 3.0) * s())));
            p.setFont(f);
            QColor c = m_color;
            c.setAlpha(int(220 * (1.0 - t)));
            p.setPen(c);
            p.drawText(QPointF(petCenter.x() + bodyLen * 0.32 + t * 16.0 * s(),
                               petCenter.y() - bodyLen * 0.35 - t * 30.0 * s()),
                       QStringLiteral("z"));
        }
    }
}

// --------------------------------------------------------- interaction ----

void PetWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(e);
        return;
    }

    m_stateBeforeHold = m_state;
    m_talkReply.clear();
    if (m_scene != Scene::None) {
        endEncounter(true);
        m_stateBeforeHold = State::Wander;
    }
    m_state      = State::Held;
    m_stateFor   = 0.0;
    m_velocity   = QPointF();
    m_dragOffset = QPointF(QCursor::pos()) - m_center;
    m_pressScreen = QPointF(QCursor::pos());
    m_dragged     = false;
    setCursor(Qt::ClosedHandCursor);
    e->accept();
}

void PetWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (m_state != State::Held) {
        QWidget::mouseMoveEvent(e);
        return;
    }

    const QPointF cursor(QCursor::pos());
    if (!m_dragged && length(cursor - m_pressScreen) < 5.0 * s()) {
        e->accept();
        return;
    }
    m_dragged = true;
    const QPointF next = cursor - m_dragOffset;
    m_velocity = (next - m_center) * 8.0;     // carries over as a small toss
    m_center   = next;
    clampToScreen();
    updateWindowGeometry();
    update();
    e->accept();
}

void PetWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || m_state != State::Held) {
        QWidget::mouseReleaseEvent(e);
        return;
    }

    if (!m_dragged) {
        setCursor(Qt::OpenHandCursor);
        m_state = m_stateBeforeHold;
        pet();
        e->accept();
        return;
    }

    m_state    = State::Wander;
    m_stateFor = 0.0;
    setCursor(Qt::OpenHandCursor);
    pickTarget();

    const qreal toss = length(m_velocity);
    m_velocity = toss > 600.0 ? m_velocity / toss * 600.0 : m_velocity;
    const QRectF area = availableArea();
    const qreal r = reach();
    const bool atEdge = m_center.x() <= area.left() + r + 1.0 ||
                        m_center.x() >= area.right() - r - 1.0 ||
                        m_center.y() <= area.top() + r + 1.0 ||
                        m_center.y() >= area.bottom() - r - 1.0;
    if (atEdge && toss > 185.0 * s()) {
        stunAtEdge();
    } else if (toss > 260.0) {
        spawnBurst(ParticleKind::Spark, qBound(4, qRound(toss / 90.0), 9), m_color.lighter(145));
        if (m_chatter)
            say(pick(dropLines()), 1800);
    }

    e->accept();
}

void PetWidget::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        setCursor(Qt::OpenHandCursor);
        celebrate();
        spawnBurst(ParticleKind::Spark, 8, QColor(255, 214, 84));
        if (m_chatter)
            say(pick(pokeLines()), 2200);
        e->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(e);
}

void PetWidget::contextMenuEvent(QContextMenuEvent *e)
{
    // a right-click during a drag would otherwise leave it stuck to the cursor
    if (m_state == State::Held) {
        m_state = State::Wander;
        setCursor(Qt::OpenHandCursor);
    }

    QMenu menu(this);

    QAction *open = menu.addAction(tr("Open dashboard"));
    connect(open, &QAction::triggered, this, &PetWidget::openDashboardRequested);

    QAction *follow = menu.addAction(tr("Follow the cursor"));
    follow->setCheckable(true);
    follow->setChecked(m_follow);
    connect(follow, &QAction::toggled, this, [this](bool on) {
        setFollowCursor(on);
        emit followCursorToggled(on);
    });

    menu.addSeparator();

    QAction *meet = menu.addAction(tr("Call a friend over"));
    meet->setEnabled(m_scene == Scene::None);
    connect(meet, &QAction::triggered, this, &PetWidget::startEncounter);

    QAction *poke = menu.addAction(tr("Poke"));
    connect(poke, &QAction::triggered, this, [this] {
        celebrate();
        if (m_chatter)
            say(pick(pokeLines()), 2200);
    });

    QAction *petAction = menu.addAction(tr("Pet gently"));
    connect(petAction, &QAction::triggered, this, &PetWidget::pet);

    QAction *chat = menu.addAction(tr("Tell me something"));
    chat->setEnabled(m_chatter && m_state != State::Orbit);
    connect(chat, &QAction::triggered, this, &PetWidget::talk);

    QAction *feedAction = menu.addAction(tr("Give a snack"));
    connect(feedAction, &QAction::triggered, this, &PetWidget::feed);

    QAction *playAction = menu.addAction(tr("Play chase (12 sec)"));
    playAction->setEnabled(m_state != State::Orbit);
    connect(playAction, &QAction::triggered, this, &PetWidget::startPlay);

    QAction *nap = menu.addAction(m_state == State::Sleep ? tr("Wake up") : tr("Take a nap"));
    connect(nap, &QAction::triggered, this, [this] {
        m_talkReply.clear();
        endEncounter(true);
        if (m_state == State::Sleep) {
            wake();
        } else {
            m_state    = State::Sleep;
            m_stateFor = 0.0;
            m_velocity = QPointF();
            say(QStringLiteral("zzz…"), 2000);
        }
    });

    menu.addSeparator();

    QAction *dismiss = menu.addAction(tr("Send it away"));
    connect(dismiss, &QAction::triggered, this, [this] {
        this->hide();
        emit dismissed();
    });

    m_menuOpen = true;
    menu.exec(e->globalPos());
    m_menuOpen = false;
    m_clock.restart();
    e->accept();
}

void PetWidget::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    m_clock.restart();
    if (!m_timer->isActive())
        m_timer->start();
}

void PetWidget::hideEvent(QHideEvent *e)
{
    m_timer->stop();
    if (m_state == State::Held) {
        m_state = State::Wander;
        m_velocity = QPointF();
        setCursor(Qt::OpenHandCursor);
    }
    QWidget::hideEvent(e);
}
