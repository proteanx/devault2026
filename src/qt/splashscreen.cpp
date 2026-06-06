// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2020-2021 The Bitcoin developers
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
#include <ui_interface.h>
#include <util/system.h>
#include <version.h>

#include <QApplication>
#include <QCloseEvent>
#include <QPainter>
#include <QRadialGradient>
#include <QScreen>

#include <memory>

SplashScreen::SplashScreen(interfaces::Node &node,
                           const NetworkStyle *networkStyle)
    : QWidget(nullptr), curAlignment(0), m_node(node) {
    // DeVault splash: the full "Terraform" art is the background, with title/version/copyright text
    // centered near the bottom (matching the legacy DeVault splash layout).
    int paddingTop = int(720 * 0.85);
    int vSpace = 10;

    float fontFactor = 1.0;
    float devicePixelRatio = 1.0;
#if QT_VERSION > 0x050100
    devicePixelRatio = static_cast<QGuiApplication *>(QCoreApplication::instance())->devicePixelRatio();
#endif

    // define text to place
    QString titleText = PACKAGE_NAME;
    QString versionText = QString("Version %1").arg(QString::fromStdString(FormatFullVersion()));
    QString copyrightText = QString::fromStdString(CopyrightHolders(strprintf("\xC2\xA9 %u-%u ", 2009, COPYRIGHT_YEAR)));
    QString titleAddText = networkStyle->getTitleAddText();

    QString font = QApplication::font().toString();

    // create a bitmap according to device pixelratio (DeVault splash size: 1440x720)
    QSize splashSize(1440 * devicePixelRatio, 720 * devicePixelRatio);
    pixmap = QPixmap(splashSize);

#if QT_VERSION > 0x050100
    // change to HiDPI if it makes sense
    pixmap.setDevicePixelRatio(devicePixelRatio);
#endif

    QPainter pixPaint(&pixmap);
    pixPaint.setPen(QColor(0xD9, 0xD9, 0xD9));

    // draw the DeVault splash art as the full background
    QPixmap header(":/icons/terraform");
    pixPaint.drawPixmap(QRect(QPoint(0, 0), QSize(1440, 720)), header);

    // title, centered near the bottom
    pixPaint.setFont(QFont(font, 28 * fontFactor));
    QFontMetrics fm = pixPaint.fontMetrics();
    int titleTextWidth = GUIUtil::TextWidth(fm, titleText);
    if (titleTextWidth > 480) {
        fontFactor = fontFactor * 480 / titleTextWidth;
    }
    pixPaint.setFont(QFont(font, 24 * fontFactor));
    fm = pixPaint.fontMetrics();
    titleTextWidth = GUIUtil::TextWidth(fm, titleText);
    pixPaint.drawText(pixmap.width() / 2 / devicePixelRatio - titleTextWidth / 2, paddingTop, titleText);

    // version, centered below the title
    pixPaint.setFont(QFont(font, 15 * fontFactor));
    fm = pixPaint.fontMetrics();
    const int textHeight = fm.height();
    const int versionTextWidth = GUIUtil::TextWidth(fm, versionText);
    pixPaint.drawText(pixmap.width() / 2 / devicePixelRatio - versionTextWidth / 2,
                      paddingTop + textHeight + vSpace, versionText);

    // copyright, centered below the version
    pixPaint.setFont(QFont(font, 10 * fontFactor));
    fm = pixPaint.fontMetrics();
    const int copyrightTextWidth = GUIUtil::TextWidth(fm, copyrightText);
    pixPaint.drawText(pixmap.width() / 2 / devicePixelRatio - copyrightTextWidth / 2,
                      paddingTop + 2 * (textHeight + vSpace), copyrightText);

    // draw additional text if special network (e.g. [testnet]) next to the title
    if (!titleAddText.isEmpty()) {
        QFont boldFont = QFont(font, 10 * fontFactor);
        boldFont.setWeight(QFont::Bold);
        pixPaint.setFont(boldFont);
        pixPaint.drawText(pixmap.width() / 2 / devicePixelRatio + titleTextWidth / 2 + 10, paddingTop,
                          titleAddText);
    }

    pixPaint.end();

    // Set window title
    setWindowTitle(titleAddText.isEmpty() ? titleText : titleText + " " + titleAddText);

    // Resize window and move to center of desktop, disallow resizing
    QRect r(QPoint(), QSize(pixmap.size().width() / devicePixelRatio, pixmap.size().height() / devicePixelRatio));
    resize(r.size());
    setFixedSize(r.size());
    move(QGuiApplication::primaryScreen()->geometry().center() - r.center());

    subscribeToCoreSignals();
    installEventFilter(this);
}

SplashScreen::~SplashScreen() {
    unsubscribeFromCoreSignals();
}

bool SplashScreen::eventFilter(QObject *obj, QEvent *ev) {
    if (ev->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(ev);
        if (keyEvent->key() == Qt::Key_Q) {
            m_node.startShutdown();
        }
    }
    return QObject::eventFilter(obj, ev);
}

void SplashScreen::slotFinish(QWidget *mainWin) {
    Q_UNUSED(mainWin);

    /* If the window is minimized, hide() will be ignored. */
    /* Make sure we de-minimize the splashscreen window before hiding */
    if (isMinimized()) {
        showNormal();
    }
    hide();
    // No more need for this
    deleteLater();
}

static void InitMessage(SplashScreen *splash, const std::string &message) {
    QMetaObject::invokeMethod(splash, "showMessage", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(message)),
                              Q_ARG(int, Qt::AlignBottom | Qt::AlignHCenter),
                              Q_ARG(QColor, QColor(0xD9, 0xD9, 0xD9)));
}

static void ShowProgress(SplashScreen *splash, const std::string &title,
                         int nProgress, bool resume_possible) {
    InitMessage(splash, title + std::string("\n") +
                            (resume_possible
                                 ? _("(press q to shutdown and continue later)")
                                 : _("press q to shutdown")) +
                            strprintf("\n%d", nProgress) + "%");
}
#ifdef ENABLE_WALLET
void SplashScreen::ConnectWallet(std::unique_ptr<interfaces::Wallet> wallet) {
    m_connected_wallet_handlers.emplace_back(wallet->handleShowProgress(
        std::bind(ShowProgress, this, std::placeholders::_1,
                  std::placeholders::_2, false)));
    m_connected_wallets.emplace_back(std::move(wallet));
}
#endif

void SplashScreen::subscribeToCoreSignals() {
    // Connect signals to client
    m_handler_init_message = m_node.handleInitMessage(
        std::bind(InitMessage, this, std::placeholders::_1));
    m_handler_show_progress = m_node.handleShowProgress(
        std::bind(ShowProgress, this, std::placeholders::_1,
                  std::placeholders::_2, std::placeholders::_3));
#ifdef ENABLE_WALLET
    m_handler_load_wallet = m_node.handleLoadWallet(
        [this](std::unique_ptr<interfaces::Wallet> wallet) {
            ConnectWallet(std::move(wallet));
        });
#endif
}

void SplashScreen::unsubscribeFromCoreSignals() {
    // Disconnect signals from client
    m_handler_init_message->disconnect();
    m_handler_show_progress->disconnect();
    for (const auto &handler : m_connected_wallet_handlers) {
        handler->disconnect();
    }
    m_connected_wallet_handlers.clear();
    m_connected_wallets.clear();
}

void SplashScreen::showMessage(const QString &message, int alignment,
                               const QColor &color) {
    curMessage = message;
    curAlignment = alignment;
    curColor = color;
    update();
}

void SplashScreen::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    painter.drawPixmap(0, 0, pixmap);
    QRect r = rect().adjusted(5, 5, -5, -5);
    painter.setPen(curColor);
    painter.drawText(r, curAlignment, curMessage);
}

void SplashScreen::closeEvent(QCloseEvent *event) {
    // allows an "emergency" shutdown during startup
    m_node.startShutdown();
    event->ignore();
}
