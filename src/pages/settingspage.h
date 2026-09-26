#pragma once

#include <QWidget>

namespace Ui { class SettingsPage; }

/*
 * Endpoint + addresses, mining, appearance and the pet.
 *
 * The layout lives in settingspage.ui — open it in Qt Designer to move things
 * around. This file only wires the widgets up: text fields and the mining /
 * build-feature toggles commit on Save (Revert restores them); appearance and
 * pet controls apply immediately.
 */
class SettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsPage(QWidget *parent = nullptr);
    ~SettingsPage() override;

    void reloadFromSettings();

    // Returns true when there are unsaved field or toggle changes.
    bool isDirty() const;

    // Called by MainWindow when the user navigates to this page.
    // Reloads from settings only when there are no unsaved changes, so
    // in-progress edits survive a tab switch and come back intact.
    void onPageShown();

    // Called by MainWindow when the user navigates *away* from this page.
    // Emits unsavedChangesOnLeave() when dirty so the caller can react.
    void onPageHidden();
    void save();

signals:
    void unsavedChangesOnLeave();

private:
    void applyTheme();          // styling Designer cannot preview
    void connectWidgets();
    void updateSaveState();
    void updateMiningWarning();
    // Panel endpoint is required only in Endpoint update mode; in Tox /
    // Config-link mode an empty field must not block Save (buildpage
    // enforces the per-mode requirement at build time).
    void syncEndpointOptional();
    void markTogglesDirty();
    void generateToxPair();
    void exportToxPair();

    Ui::SettingsPage *ui = nullptr;
    bool m_loading = false;
    bool m_togglesDirty = false;
    class BuildSystem *m_buildSystem = nullptr;
};
