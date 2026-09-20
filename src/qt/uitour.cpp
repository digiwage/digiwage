// Copyright (c) 2026 The DigiWage developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/uitour.h>

#include <qt/bitcoinamountfield.h>
#include <qt/bitcoingui.h>
#include <qt/sendcoinsdialog.h>
#include <qt/styleSheet.h>

#include <consensus/amount.h>

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QPixmap>
#include <QPushButton>
#include <QTabWidget>
#include <QTextStream>
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

namespace {

/** Drives the real Receive and Send pages/widgets end to end, including the
 *  Send confirmation dialog, so the result proves the GUI flow works rather
 *  than just that the RPC layer does. */
class FunctionalTest : public QObject
{
public:
    FunctionalTest(BitcoinGUI* w, QString dest, QString amountStr, QString outFile)
        : m_win(w), m_dest(std::move(dest)), m_amountStr(std::move(amountStr)), m_outFile(std::move(outFile)) {}

    void start()
    {
        QMetaObject::invokeMethod(m_win, "gotoReceiveCoinsPage", Qt::DirectConnection);
        QTimer::singleShot(1200, this, [this]{ captureReceiveAddress(); });
    }

private:
    void writeLine(const QString& line)
    {
        QFile f(m_outFile);
        f.open(QIODevice::Append | QIODevice::Text);
        QTextStream(&f) << line << "\n";
    }

    void captureReceiveAddress()
    {
        if (QLabel* addr = m_win->findChild<QLabel*>("address_content")) {
            m_receiveAddress = addr->text().trimmed();
        }
        writeLine("receive_address=" + m_receiveAddress);
        QMetaObject::invokeMethod(m_win, "gotoSendCoinsPage", Qt::DirectConnection, Q_ARG(QString, QString()));
        QTimer::singleShot(1200, this, [this]{ fillAndSend(); });
    }

    void fillAndSend()
    {
        QLineEdit* payTo = m_win->findChild<QLineEdit*>("payTo");
        BitcoinAmountField* payAmount = m_win->findChild<BitcoinAmountField*>("payAmount");
        QPushButton* sendButton = m_win->findChild<QPushButton*>("sendButton");
        writeLine(QString("step=fillAndSend widgets=%1,%2,%3").arg(!!payTo).arg(!!payAmount).arg(!!sendButton));
        if (!payTo || !payAmount || !sendButton) {
            writeLine("error=send form widgets not found");
            std::_Exit(1);
        }
        payTo->setText(m_dest);
        Q_EMIT payTo->textChanged(m_dest);
        payAmount->setValue(static_cast<CAmount>(m_amountStr.toDouble() * COIN + 0.5));
        writeLine(QString("step=filled payTo=%1 amount=%2").arg(payTo->text()).arg(payAmount->value()));

        // Use a generous custom fee rate: on this small isolated test network the
        // default smart-fee estimate can land right on a peer's relay-fee filter.
        if (QAbstractButton* customFeeRadio = m_win->findChild<QAbstractButton*>("radioCustomFee")) {
            customFeeRadio->click();
            if (BitcoinAmountField* customFee = m_win->findChild<BitcoinAmountField*>("customFee")) {
                customFee->setValue(2 * COIN / 100); // 0.02 DIGIWAGE/kvB
            }
        }

        // The Send confirmation dialog opens a nested modal loop from inside
        // sendButton's click handler; schedule the click-through before
        // triggering it so the timer fires while that loop is spinning.
        QTimer::singleShot(1000, this, [this]{ confirmSend(0); });
        sendButton->click();
        writeLine("step=sendButton-click-returned");
        // If nothing was ever sent (e.g. validation failed silently), give up.
        QTimer::singleShot(30000, this, [this]{
            if (!m_confirmed) { writeLine("error=send confirmation dialog never appeared"); std::_Exit(1); }
        });
    }

    void confirmSend(int attempt)
    {
        connectCoinsSent();
        QWidget* active = QApplication::activeModalWidget();
        QMessageBox* box = qobject_cast<QMessageBox*>(active);
        if (attempt % 5 == 0) writeLine(QString("step=confirmSend attempt=%1 activeModal=%2 isMessageBox=%3").arg(attempt).arg(active ? active->metaObject()->className() : "null").arg(!!box));
        if (!box) {
            // The 3s safety countdown or the confirmation dialog may not be up yet.
            if (attempt < 60) QTimer::singleShot(500, this, [this, attempt]{ confirmSend(attempt + 1); });
            else { writeLine("error=no QMessageBox appeared after 30s"); std::_Exit(1); }
            return;
        }
        m_confirmed = true;
        if (QAbstractButton* yes = box->button(QMessageBox::Yes)) {
            if (!yes->isEnabled()) {
                // Still inside the anti-footgun countdown: wait it out.
                QTimer::singleShot(500, this, [this, attempt]{ confirmSend(attempt + 1); });
                m_confirmed = false;
                return;
            }
            writeLine("step=clicking-yes");
            yes->click();
        } else {
            writeLine("error=confirmation dialog has no Yes button");
        }
    }

    void connectCoinsSent()
    {
        if (m_connected) return;
        SendCoinsDialog* sendDlg = m_win->findChild<SendCoinsDialog*>();
        if (!sendDlg) return;
        m_connected = true;
        connect(sendDlg, &SendCoinsDialog::coinsSent, this, [this](const uint256& txid) {
            writeLine("send_txid=" + QString::fromStdString(txid.GetHex()));
            writeLine("status=ok");
            // Give the net thread time to actually relay the tx to our peer
            // (INV batching can take a couple of seconds) before we exit.
            QTimer::singleShot(8000, this, []{ std::_Exit(0); });
        });
    }

private:
    BitcoinGUI* m_win;
    QString m_dest;
    QString m_amountStr;
    QString m_outFile;
    QString m_receiveAddress;
    bool m_confirmed{false};
    bool m_connected{false};
};

} // namespace

void RunUiFunctionalTest(BitcoinGUI* window, const QString& destAddress, const QString& amount, const QString& outFile)
{
    StyleSheet::instance().setMode(AppearanceMode::Dark);
    window->resize(1280, 800);
    QFile::remove(outFile);
    auto* test = new FunctionalTest(window, destAddress, amount, outFile);
    QTimer::singleShot(9000, test, [test]{ test->start(); });
}
