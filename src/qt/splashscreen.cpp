// Copyright (c) 2011-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <qt/splashscreen.h>

#include <clientversion.h>
#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <qt/guiutil.h>
#include <qt/networkstyle.h>
#include <qt/walletmodel.h>
#include <util/system.h>
#include <util/translation.h>

#include "styleSheet.h"
#include <qt/platformstyle.h>


#include <functional>

#include <QApplication>
#include <QCloseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QScreen>


SplashScreen::SplashScreen(const NetworkStyle* networkStyle)
    : QWidget()
{
    // set sizes
    int logoSize                = 50;
    int logoImageSize           = logoSize - 13;
    int packageTextHeight       = 30;
    int versionTextHeight       = 20;
    int statusHeight            = 30;
    int titleAddTextHeight      = 12;
    int welcomeTextHeight       = 35;
    float fontFactor            = 1.0;
    float devicePixelRatio      = 1.0;
    devicePixelRatio = static_cast<QGuiApplication*>(QCoreApplication::instance())->devicePixelRatio();

    // define text to place
    QString titleText       = PACKAGE_NAME;
    QString versionText     = QString("%1").arg(QString::fromStdString(FormatFullVersion()));
    QString copyrightText   = QString::fromUtf8(CopyrightHolders(strprintf("\xc2\xA9 %u ", COPYRIGHT_YEAR)).c_str());
    const QString& titleAddText    = networkStyle->getTitleAddText();

    QString font            = QApplication::font().toString();

    // create a bitmap according to device pixelratio
    QSize splashSize(480,320);
    pixmap = QPixmap(480*devicePixelRatio,320*devicePixelRatio);

    // change to HiDPI if it makes sense
    pixmap.setDevicePixelRatio(devicePixelRatio);

    QPainter pixPaint(&pixmap);

    QColor foreground_color = GetStringStyleValue("splashscreen/foreground-color", "#ffffff");
    QColor foreground_color_statusbar = GetStringStyleValue("splashscreen/foreground-color-statusbar", "#ffffff");
    QColor logo_frame_color = GetStringStyleValue("splashscreen/logo-frame-color", "#ffffff");

    QRect mainRect(QPoint(0,0), splashSize);
    QColor background_color     = GetStringStyleValue("splashscreen/background-color", "#030509");
    pixPaint.fillRect(mainRect, background_color);

    // Centered composition: soft accent glow, logo, wordmark, version, network tag
    pixPaint.setRenderHint(QPainter::Antialiasing);
    pixPaint.setRenderHint(QPainter::SmoothPixmapTransform);
    QColor accent = StyleSheet::instance().tokenColor("accent");
    QRadialGradient glow(splashSize.width() / 2.0, 118, 190);
    QColor g0 = accent; g0.setAlphaF(0.16);
    QColor g1 = accent; g1.setAlphaF(0.0);
    glow.setColorAt(0, g0);
    glow.setColorAt(1, g1);
    pixPaint.fillRect(mainRect, glow);

    const int logoBox = 64;
    QRect logoRect((splashSize.width() - logoBox) / 2, 72, logoBox, logoBox);
    QPainterPath logoPath;
    logoPath.addRoundedRect(logoRect, 16, 16);
    pixPaint.fillPath(logoPath, StyleSheet::instance().tokenColor("surface-2"));
    pixPaint.setPen(logo_frame_color);
    pixPaint.drawPath(logoPath);
    QPixmap logo = QIcon(":/icons/bitcoin").pixmap(QSize(40, 40));
    pixPaint.drawPixmap(logoRect.x() + 12, logoRect.y() + 12, logo);

    pixPaint.setPen(foreground_color);
    QFont titleFont(font, 22 * fontFactor, QFont::DemiBold);
    pixPaint.setFont(titleFont);
    QRect rectTitle(0, logoRect.bottom() + 20, splashSize.width(), packageTextHeight);
    pixPaint.drawText(rectTitle, Qt::AlignHCenter | Qt::AlignVCenter, titleText);

    QPoint versionPoint(rectTitle.bottomLeft());
    if(!titleAddText.isEmpty())
    {
        QRect titleAddRect(rectTitle.bottomLeft(), QSize(rectTitle.width(), titleAddTextHeight));
        versionPoint = titleAddRect.bottomLeft();
        pixPaint.setPen(accent);
        pixPaint.setFont(QFont(font, 8 * fontFactor, QFont::DemiBold));
        pixPaint.drawText(titleAddRect, Qt::AlignHCenter | Qt::AlignVCenter, titleAddText.toUpper());
    }

    pixPaint.setPen(StyleSheet::instance().tokenColor("text-2-solid"));
    pixPaint.setFont(QFont(font, 9 * fontFactor));
    QRect versionRect(versionPoint, QSize(rectTitle.width(), versionTextHeight));
    pixPaint.drawText(versionRect, Qt::AlignHCenter | Qt::AlignVCenter, versionText);

    // footer: copyright, hairline above
    QRect statusRect(mainRect.left(), mainRect.height() - statusHeight, mainRect.width(), statusHeight);
    pixPaint.setPen(StyleSheet::instance().tokenColor("border-solid"));
    pixPaint.drawLine(statusRect.topLeft(), statusRect.topRight());
    QFont statusFont = QApplication::font();
    statusFont.setPointSizeF(statusFont.pointSizeF() * 0.85);
    pixPaint.setFont(statusFont);
    pixPaint.setPen(StyleSheet::instance().tokenColor("text-3-solid"));
    pixPaint.drawText(statusRect.adjusted(16, 0, -16, 0), Qt::AlignHCenter | Qt::AlignVCenter, copyrightText);
    pixPaint.end();

    // Set window title
    setWindowTitle(titleText + " " + titleAddText);

    // Resize window and move to center of desktop, disallow resizing
    QRect r(QPoint(), QSize(pixmap.size().width()/devicePixelRatio,pixmap.size().height()/devicePixelRatio));
    resize(r.size());
    setFixedSize(r.size());
    move(QGuiApplication::primaryScreen()->geometry().center() - r.center());

    installEventFilter(this);

    GUIUtil::handleCloseWindowShortcut(this);
}

SplashScreen::~SplashScreen()
{
    if (m_node) unsubscribeFromCoreSignals();
}

void SplashScreen::setNode(interfaces::Node& node)
{
    assert(!m_node);
    m_node = &node;
    subscribeToCoreSignals();
    if (m_shutdown) m_node->startShutdown();
}

void SplashScreen::shutdown()
{
    m_shutdown = true;
    if (m_node) m_node->startShutdown();
}

bool SplashScreen::eventFilter(QObject * obj, QEvent * ev) {
    if (ev->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(ev);
        if (keyEvent->key() == Qt::Key_Q) {
            shutdown();
        }
    }
    return QObject::eventFilter(obj, ev);
}

static void InitMessage(SplashScreen *splash, const std::string &message)
{
    QColor foreground_color = GetStringStyleValue("splashscreen/foreground-color_statusbar", "#ffffff");
    bool invoked = QMetaObject::invokeMethod(splash, "showMessage",
        Qt::QueuedConnection,
        Q_ARG(QString, QString::fromStdString(message)),
        Q_ARG(int, Qt::AlignBottom|Qt::AlignLeft),
        Q_ARG(QColor, foreground_color));
    assert(invoked);
}

static void ShowProgress(SplashScreen *splash, const std::string &title, int nProgress, bool resume_possible)
{
    InitMessage(splash, title + std::string("\n") +
            (resume_possible ? SplashScreen::tr("(press q to shutdown and continue later)").toStdString()
                                : SplashScreen::tr("press q to shutdown").toStdString()) +
            strprintf("\n%d", nProgress) + "%");
}

void SplashScreen::subscribeToCoreSignals()
{
    // Connect signals to client
    m_handler_init_message = m_node->handleInitMessage(std::bind(InitMessage, this, std::placeholders::_1));
    m_handler_show_progress = m_node->handleShowProgress(std::bind(ShowProgress, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    m_handler_init_wallet = m_node->handleInitWallet([this]() { handleLoadWallet(); });
}

void SplashScreen::handleLoadWallet()
{
#ifdef ENABLE_WALLET
    if (!WalletModel::isWalletEnabled()) return;
    m_handler_load_wallet = m_node->walletLoader().handleLoadWallet([this](std::unique_ptr<interfaces::Wallet> wallet) {
        m_connected_wallet_handlers.emplace_back(wallet->handleShowProgress(std::bind(ShowProgress, this, std::placeholders::_1, std::placeholders::_2, false)));
        m_connected_wallets.emplace_back(std::move(wallet));
    });
#endif
}

void SplashScreen::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
    m_handler_init_message->disconnect();
    m_handler_show_progress->disconnect();
    for (const auto& handler : m_connected_wallet_handlers) {
        handler->disconnect();
    }
    m_connected_wallet_handlers.clear();
    m_connected_wallets.clear();
}

void SplashScreen::showMessage(const QString &message, int alignment, const QColor &color)
{
    curMessage = message;
    curAlignment = alignment;
    curColor = color;
    update();
}

void SplashScreen::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.drawPixmap(0, 0, pixmap);
    QRect r = rect().adjusted(10, 10, -10, -10);
    painter.setPen(curColor);
    QFont font = QApplication::font();
    font.setPointSizeF(font.pointSizeF() * 0.9);
    painter.setFont(font);
    painter.drawText(r, curAlignment, curMessage);
}

void SplashScreen::closeEvent(QCloseEvent *event)
{
    shutdown(); // allows an "emergency" shutdown during startup
    event->ignore();
}
