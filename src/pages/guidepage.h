#pragma once

#include <QWidget>

class FlipBook;
class QLabel;
class QAudioOutput;
class QHideEvent;
class QMediaPlayer;
class QPaintEvent;
class QProgressBar;
class QPushButton;
class QShowEvent;

/*
 * The Documentation page: a real flipbook.
 *
 * An open field guide rests on a dark desk (a faint culture of tinted
 * bacteria drifts behind it). Each chapter is a spread — a specimen plate
 * with mounted CC0 artwork on the left, field notes on the right — and the
 * reader turns pages by clicking a page or a corner, with a full page-turn
 * animation. Content is the Builder's documentation, set like a printed
 * biology handbook.
 */
class GuidePage final : public QWidget
{
public:
    explicit GuidePage(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    FlipBook *m_book = nullptr;
    QLabel *m_spreadLabel = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_previousButton = nullptr;
    QPushButton *m_nextButton = nullptr;
    QMediaPlayer *m_music = nullptr;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QAudioOutput *m_audio = nullptr;
#endif
    bool m_hasMusic = false;
};
