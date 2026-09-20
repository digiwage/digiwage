// Copyright (c) 2026 The DigiWage developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/styleSheet.h>

#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QFontDatabase>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QProxyStyle>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStyleFactory>
#include <QAbstractButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileDialog>
#include <QInputDialog>
#include <QLayout>
#include <QMessageBox>
#include <QProgressDialog>
#include <QWidget>

namespace {
const QString TEMPLATE_FORMAT = ":/styles/templates/%1.qss";
const QString MODE_CONFIG_FORMAT = ":/styles/%1.ini";
/** Gives every dialog the same generous 24px content padding and 12px rhythm on first show. */
class DialogPolisher : public QObject
{
public:
    bool eventFilter(QObject* obj, QEvent* event) override
    {
        if (event->type() != QEvent::Show) return false;
        QDialog* dialog = qobject_cast<QDialog*>(obj);
        if (!dialog || dialog->property("dwPadded").toBool() || !dialog->layout()) return false;
        if (qobject_cast<QMessageBox*>(dialog) || qobject_cast<QFileDialog*>(dialog) ||
            qobject_cast<QProgressDialog*>(dialog) || qobject_cast<QInputDialog*>(dialog)) return false;
        dialog->setProperty("dwPadded", true);
        // Stock button-box icons come from the system theme and clash with the design: text-only buttons
        for (QDialogButtonBox* box : dialog->findChildren<QDialogButtonBox*>())
            for (QAbstractButton* button : box->buttons()) button->setIcon(QIcon());
        QLayout* layout = dialog->layout();
        const QMargins m = layout->contentsMargins();
        if (m.left() < 20) layout->setContentsMargins(24, 24, 24, 24);
        if (layout->spacing() >= 0 && layout->spacing() < 10) layout->setSpacing(12);
        return false;
    }
};

const char* const SETTINGS_KEY = "Appearance";

/** Style name (as used by the code base) -> QSS template file. */
const QMap<QString, QString>& templateFor()
{
    static const QMap<QString, QString> map = {
        {"app", "app"}, {"invalid", "invalid"}, {"tableviewlight", "tableview"},
        {"buttongray", "button_primary"}, {"buttonlight", "button_secondary"}, {"buttondark", "button_outline"},
        {"buttontransparent", "button_ghost"}, {"buttontransparentbordered", "button_outline_small"},
        {"navbutton", "navbutton"}, {"navgroupbutton", "navgroupbutton"}, {"navsubgroupbutton", "navsubgroupbutton"},
        {"treeview", "treeview"}, {"scrollbarlight", "scrollbar"}, {"scrollbardark", "scrollbar"}};
    return map;
}

/** Proxy style: rounded combo popups and message box icons in the active mode. */
class DigiWageStyle : public QProxyStyle
{
public:
    using QProxyStyle::polish;
    DigiWageStyle()
    {
        message_info_path = GetStringStyleValue("appstyle/message-info-icon", "");
        message_warning_path = GetStringStyleValue("appstyle/message-warning-icon", "");
        message_critical_path = GetStringStyleValue("appstyle/message-critical-icon", "");
        message_question_path = GetStringStyleValue("appstyle/message-question-icon", "");
        message_icon_size = GetIntStyleValue("appstyle/message-icon-weight", 44);
    }

    void polish(QWidget *widget) override
    {
        if (widget && widget->inherits("QComboBox")) {
            QComboBox* comboBox = (QComboBox*)widget;
            if (comboBox->view() && comboBox->view()->inherits("QComboBoxListView")) {
                comboBox->setView(new QListView());
                qApp->processEvents();
            }
            if (comboBox->view() && comboBox->view()->parentWidget()) {
                QWidget* parent = comboBox->view()->parentWidget();
                parent->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
                parent->setAttribute(Qt::WA_TranslucentBackground);
            }
        }
        if (widget && widget->inherits("QMessageBox")) {
            QMessageBox* messageBox = (QMessageBox*)widget;
            QString path;
            switch (messageBox->icon()) {
            case QMessageBox::Information: path = message_info_path; break;
            case QMessageBox::Warning: path = message_warning_path; break;
            case QMessageBox::Critical: path = message_critical_path; break;
            case QMessageBox::Question: path = message_question_path; break;
            default: QProxyStyle::polish(widget); return;
            }
            const QPixmap pix(path);
            if (!pix.isNull()) messageBox->setIconPixmap(pix.scaled(message_icon_size, message_icon_size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        if (widget && widget->inherits("QLineEdit")) {
            QLineEdit* lineEdit = (QLineEdit*)widget;
            if (lineEdit->isReadOnly()) lineEdit->setFocusPolicy(Qt::ClickFocus);
        }
        QProxyStyle::polish(widget);
    }

private:
    QString message_info_path, message_warning_path, message_critical_path, message_question_path;
    int message_icon_size;
};

AppearanceMode modeFromString(const QString& s) { return s == "light" ? AppearanceMode::Light : AppearanceMode::Dark; }
QString modeToString(AppearanceMode m) { return m == AppearanceMode::Light ? "light" : "dark"; }
} // namespace

StyleSheet& StyleSheet::instance()
{
    static StyleSheet inst;
    return inst;
}

StyleSheet::StyleSheet()
{
    QSettings settings;
    // The old multi-theme selector is gone: drop its value so profiles stay clean.
    if (settings.contains("Theme")) settings.remove("Theme");
    loadMode(modeFromString(settings.value(SETTINGS_KEY, "dark").toString()));
}

bool StyleSheet::reducedMotion()
{
    return QSettings().value("ReduceMotion", false).toBool();
}

void StyleSheet::loadMode(AppearanceMode mode)
{
    m_mode = mode;
    delete m_config.data();
    m_config = new QSettings(MODE_CONFIG_FORMAT.arg(modeToString(mode)), QSettings::IniFormat);
    m_tokens.clear();
    m_config->beginGroup("tokens");
    for (const QString& key : m_config->childKeys()) {
        const QVariant v = m_config->value(key);
        m_tokens[key] = v.type() == QVariant::StringList ? v.toStringList().join(",") : v.toString();
    }
    m_config->endGroup();
    m_cacheStyles.clear();
}

QColor StyleSheet::parseColor(const QString& value)
{
    static const QRegularExpression rgba(R"(^rgba\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)$)");
    const auto m = rgba.match(value.trimmed());
    if (m.hasMatch()) return QColor(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt(), m.captured(4).toInt());
    return QColor(value);
}

QColor StyleSheet::tokenColor(const QString& token) const
{
    return parseColor(m_tokens.value(token, "#ff00ff"));
}

QStringList StyleSheet::knownStyleNames() const { return templateFor().keys(); }

void StyleSheet::setStyleSheet(QWidget *widget, const QString &style_name)
{
    setObjectStyleSheet<QWidget>(widget, style_name);
}

void StyleSheet::setStyleSheet(QApplication *app, const QString& style_name)
{
    static bool fontsLoaded = false;
    if (!fontsLoaded) {
        for (const char* f : {"Inter-Regular", "Inter-Medium", "Inter-SemiBold", "Inter-Bold", "InterTabular-Medium", "InterTabular-SemiBold"})
            QFontDatabase::addApplicationFont(QString(":/fonts/%1.otf").arg(f));
        fontsLoaded = true;
    }
    m_appStyleName = style_name;
    QStyle* mainStyle = QStyleFactory::create("fusion");
    DigiWageStyle* style = new DigiWageStyle;
    style->setBaseStyle(mainStyle);
    app->setStyle(style);

    QPalette palette(app->palette());
    palette.setColor(QPalette::Link, tokenColor("accent-text"));
    palette.setColor(QPalette::Window, tokenColor("bg"));
    palette.setColor(QPalette::WindowText, tokenColor("text"));
    palette.setColor(QPalette::Base, tokenColor("surface-2"));
    palette.setColor(QPalette::Text, tokenColor("text"));
    palette.setColor(QPalette::Button, tokenColor("surface-2"));
    palette.setColor(QPalette::ButtonText, tokenColor("text"));
    palette.setColor(QPalette::Highlight, tokenColor("accent"));
    palette.setColor(QPalette::HighlightedText, tokenColor("on-accent"));
    app->setPalette(palette);

    static DialogPolisher* polisher = nullptr;
    if (!polisher) {
        polisher = new DialogPolisher();
        polisher->setParent(app);
        app->installEventFilter(polisher);
    }

    QFont font("Inter");
    font.setPointSizeF(qMax<qreal>(app->font().pointSizeF(), 9.5));
    font.setStyleStrategy(QFont::PreferAntialias);
    app->setFont(font);
    setObjectStyleSheet<QApplication>(app, style_name);
}

QString StyleSheet::getStyleSheet(const QString &style_name)
{
    QString style;
    QFile file(TEMPLATE_FORMAT.arg(templateFor().value(style_name, style_name)));
    if (file.open(QIODevice::ReadOnly)) {
        style = QString::fromUtf8(file.readAll());
        static const QRegularExpression token(R"(@([a-z0-9-]+)@)");
        QString out;
        int last = 0;
        auto it = token.globalMatch(style);
        while (it.hasNext()) {
            const auto m = it.next();
            out += style.mid(last, m.capturedStart() - last);
            out += m_tokens.value(m.captured(1), QStringLiteral("#ff00ff"));
            last = m.capturedEnd();
        }
        out += style.mid(last);
        style = out;
        m_cacheStyles[style_name] = style;
    }
    return style;
}

template<typename T>
void StyleSheet::setObjectStyleSheet(T *object, const QString &style_name)
{
    if (!object) return;
    const QString style_value = m_cacheStyles.contains(style_name) ? m_cacheStyles[style_name] : getStyleSheet(style_name);
    object->setStyleSheet(style_value);
    if constexpr (std::is_base_of<QWidget, T>::value) {
        for (auto& entry : m_registered) {
            if (entry.first == object) { entry.second = style_name; return; }
        }
        m_registered.append({QPointer<QWidget>(object), style_name});
    }
}

void StyleSheet::applyToRegistered()
{
    for (int i = m_registered.size() - 1; i >= 0; --i) {
        if (!m_registered[i].first) { m_registered.remove(i); continue; }
        m_registered[i].first->setStyleSheet(getStyleSheet(m_registered[i].second));
    }
}

void StyleSheet::setMode(AppearanceMode mode)
{
    if (mode == m_mode) return;
    QSettings().setValue(SETTINGS_KEY, modeToString(mode));
    loadMode(mode);
    if (qApp) {
        QPalette palette(qApp->palette());
        palette.setColor(QPalette::Link, tokenColor("accent-text"));
        palette.setColor(QPalette::Window, tokenColor("bg"));
        palette.setColor(QPalette::WindowText, tokenColor("text"));
        palette.setColor(QPalette::Base, tokenColor("surface-2"));
        palette.setColor(QPalette::Text, tokenColor("text"));
        palette.setColor(QPalette::Button, tokenColor("surface-2"));
        palette.setColor(QPalette::ButtonText, tokenColor("text"));
        palette.setColor(QPalette::Highlight, tokenColor("accent"));
        palette.setColor(QPalette::HighlightedText, tokenColor("on-accent"));
        qApp->setPalette(palette);
        if (!m_appStyleName.isEmpty()) qApp->setStyleSheet(getStyleSheet(m_appStyleName));
    }
    applyToRegistered();
    Q_EMIT m_notifier.modeChanged();
    if (qApp) {
        for (QWidget* w : qApp->allWidgets()) w->update();
    }
}

QVariant StyleSheet::getStyleValue(const QString &key, const QVariant &defaultValue)
{
    if (!m_config) return defaultValue;
    const QVariant v = m_config->value(key, defaultValue);
    if (v.type() != QVariant::StringList) return v;
    // Commas split the ini value into a list: rebuild it, and hand colors out as #AARRGGBB
    const QString joined = v.toStringList().join(",");
    const QColor c = parseColor(joined);
    return joined.startsWith("rgba") && c.isValid() ? QVariant(c.name(QColor::HexArgb)) : QVariant(joined);
}
