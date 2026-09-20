// Copyright (c) 2026 The DigiWage developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/uitour.h>

#include <qt/bitcoingui.h>
#include <qt/styleSheet.h>

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QMetaObject>
#include <QPixmap>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QWidget>

#include <cstdlib>
#include <functional>

namespace {

struct Step {
    QString name;
    std::function<void()> run;   // may block in a nested event loop (modal dialogs)
    bool modal;
};

class Tour : public QObject
{
public:
    Tour(BitcoinGUI* w, const QString& dir) : m_win(w), m_dir(dir) { QDir().mkpath(dir); }

    void start()
    {
        build();
        next();
    }

private:
    void slot(const char* name) { QMetaObject::invokeMethod(m_win, name, Qt::DirectConnection); }

    void action(const QString& text)
    {
        for (QAction* a : m_win->findChildren<QAction*>()) {
            if (a->text().remove('&').contains(text, Qt::CaseInsensitive) && a->isEnabled()) { a->trigger(); return; }
        }
    }

    void shot(const QString& name)
    {
        QWidget* target = QApplication::activeModalWidget();
        if (!target) target = QApplication::activePopupWidget();
        if (!target) {
            for (QWidget* w : QApplication::topLevelWidgets()) {
                if (w != m_win && w->isVisible() && qobject_cast<QDialog*>(w)) target = w;
            }
        }
        if (!target) target = m_win;
        target->grab().save(QString("%1/%2-%3.png").arg(m_dir, m_mode, name));
    }

    void page(const QString& name, const char* slotName, bool takesAddress = false)
    {
        m_steps.push_back({name, [this, slotName, takesAddress]{
            if (takesAddress) QMetaObject::invokeMethod(m_win, slotName, Qt::DirectConnection, Q_ARG(QString, QString()));
            else slot(slotName);
        }, false});
    }

    void dialog(const QString& name, std::function<void()> opener)
    {
        m_steps.push_back({name, opener, true});
    }

    void build()
    {
        m_steps.clear();
        page("overview", "gotoOverviewPage");
                page("backup-prompt", "gotoOverviewPage");
        m_steps.push_back({"hide-prompt", [this]{ for (QPushButton* b : m_win->findChildren<QPushButton*>()) if (b->text() == "Maybe later") { b->click(); break; } }, false});
        page("send", "gotoSendCoinsPage", true);
        page("receive", "gotoReceiveCoinsPage");
        page("transactions", "gotoHistoryPage");
        page("staking", "gotoStakePage");
        page("contract-create", "gotoCreateContractPage");
        page("contract-send", "gotoSendToContractPage");
        page("contract-call", "gotoCallContractPage");
        page("tokens", "gotoTokenPage");
        page("delegation", "gotoDelegationPage");
        page("super-staker", "gotoSuperStakerPage");
        dialog("address-book", [this]{ action("Sending addresses"); });
        dialog("options", [this]{ slot("optionsClicked"); });
        dialog("options-appearance", [this]{
            QTimer::singleShot(600, this, []{
                if (QWidget* w = QApplication::activeModalWidget())
                    if (QTabWidget* t = w->findChild<QTabWidget*>()) t->setCurrentIndex(t->count() - 1);
            });
            slot("optionsClicked");
        });
        dialog("about", [this]{ slot("aboutClicked"); });
        dialog("rpc-console", [this]{ slot("showDebugWindowActivateConsole"); });
        dialog("help", [this]{ slot("showHelpMessageClicked"); });
        dialog("sign-message", [this]{ QMetaObject::invokeMethod(m_win, "gotoSignMessageTab", Qt::DirectConnection, Q_ARG(QString, QString())); });
        dialog("encrypt", [this]{ action("Encrypt Wallet"); });
        dialog("backup", [this]{ action("Backup Wallet"); });
        dialog("change-passphrase", [this]{ action("Change Passphrase"); });
    }

    void finishMode()
    {
        if (m_mode == "dark") {
            m_mode = "light";
            StyleSheet::instance().setMode(AppearanceMode::Light);
            m_index = 0;
            QTimer::singleShot(1200, this, [this]{ next(); });
        } else {
            std::_Exit(0);
        }
    }

    void next()
    {
        if (m_index >= (int)m_steps.size()) { finishMode(); return; }
        const Step step = m_steps[m_index++];
        if (step.modal) {
            // The opener blocks in a modal event loop: capture and close it from a timer.
            QTimer::singleShot(1500, this, [this, step]{
                shot(step.name);
                if (QWidget* w = QApplication::activeModalWidget()) w->close();
                else if (QWidget* w2 = QApplication::activeWindow()) { if (w2 != m_win) w2->close(); }
            });
            step.run();
            // Non-modal dialogs open and return immediately: close stray windows after the shot
            QTimer::singleShot(2200, this, [this]{ closeStrays(); next(); });
        } else {
            step.run();
            QTimer::singleShot(900, this, [this, step]{ if (!step.name.startsWith("hide")) shot(step.name); closeStrays(); next(); });
        }
    }

    void closeStrays()
    {
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (w != m_win && w->isVisible() && w->isWindow() && !w->objectName().contains("splash", Qt::CaseInsensitive)) w->close();
        }
    }

    BitcoinGUI* m_win;
    QString m_dir;
    QString m_mode{"dark"};
    std::vector<Step> m_steps;
    int m_index{0};
};

} // namespace

void RunUiTour(BitcoinGUI* window, const QString& outDir)
{
    StyleSheet::instance().setMode(AppearanceMode::Dark);
    window->resize(1280, 800);
    Tour* tour = new Tour(window, outDir);
    QTimer::singleShot(9000, tour, [tour]{ tour->start(); });
}
