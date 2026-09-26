#pragma once

#include <QWidget>
#include <QTimer>
#include <QJsonArray>
#include <QJsonObject>

namespace Ui { class ControlPage; }

class BuildSystem;

/*
 * The operator's control surface for pushing mining config to the colony.
 *
 *   - Edits the mining config (shared keys with the Settings page).
 *   - Generates / exports the Tox operator + bot pair and pushes config live
 *     over the serverless Tox C2 channel.
 *
 * Channel selection (endpoint / tox / config link) lives on the Build page:
 * this page is only the Tox push dashboard.
 */
class ControlPage : public QWidget
{
    Q_OBJECT
public:
    explicit ControlPage(QWidget *parent = nullptr);
    ~ControlPage() override;

     void refresh();
    void onPageShown();
    void onPageHidden();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    void connectWidgets();
    void exportSavedata();
    void setNote(const QString &text, bool ok);

    // Colony dashboard
    void refreshBotsTable();
    void removeSelectedBots();
    void pushToSelected();
    void pollSelectedBots();
    void pollAllBots();
    void setColonyNote(const QString &text, bool ok);
    void toggleAllChecks(bool on);
    void updatePushLabel();
    QSet<QString> checkedBotIds() const;
    // Persists the operator savedata (friend list) after a Tox op that may have
    // added bots, so Discover keeps working across sessions on this machine.
    void persistOperatorSavedata(const QString &updatedHex);
    QJsonArray loadBots() const;
    void saveBots(const QJsonArray &bots);
    void ensureBotsMigrated();
    void normalizeColony();
    void updateBotRow(int row, const QString &botId, const QJsonObject &status, bool online);
    void filterBotsTable();
    void showBotContextMenu(const QPoint &pos);
    void copySelectedBotIds();
    void applyBotsTableUiTweaks();
    void setColonyLoading(bool loading);
    void discoverBots();
    void autoDiscoverBots();
    void recoverDeletedBots();
    // Helpers for sort-safe colony ops (view row != array index when sortingEnabled)
    QSet<QString> selectedBotIds() const;
    // Resolves a display key (device hash, or Tox id) to the real Tox address
    // stored on the bot entry, so queries/pushes target a valid Tox ID.
    QString resolveQueryId(const QString &key) const;
    QSet<QString> selectedBotQueryIds() const;
    QList<int> selectedVisualRows() const;
    int findVisualRowForId(const QString &botId, const QString &hash) const;

    Ui::ControlPage *ui = nullptr;
    BuildSystem *m_buildSystem = nullptr;
    bool m_loading = false;
    QTimer *m_pollTimer = nullptr;
    QTimer *m_discoverTimer = nullptr;
    bool m_polling = false;
    bool m_discovering = false;
    QMap<QString,int> m_missCounts; // key = bot hash/id upper, value = consecutive poll misses
};
