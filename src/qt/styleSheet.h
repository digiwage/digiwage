#ifndef STYLESHEET_H
#define STYLESHEET_H

#include <QMap>
#include <QString>
#include <QStringList>
#include <QSettings>
#include <QPointer>
#include <QVector>
#include <QPair>
#include <QObject>
#include <QColor>

class QWidget;
class QApplication;

#define SetObjectStyleSheet(object, name) StyleSheet::instance().setStyleSheet(object, name)

#define GetStyleValue(key, defaultValue) StyleSheet::instance().getStyleValue(key, defaultValue)
#define GetStringStyleValue(key, defaultValue) GetStyleValue(key, defaultValue).toString()
#define GetIntStyleValue(key, defaultValue) GetStyleValue(key, defaultValue).toInt()
#define GetPercentStyleValue(key, defaultValue) GetIntStyleValue(key, defaultValue) / 100.0
#define GetColorStyleValue(key, defaultValue) GetStyleValue(key, defaultValue.name()).toString()

/** Names of the styles that will be used for the GUI components appearance
 */
namespace StyleSheetNames 
{
    static const QString App                         = "app";
    static const QString Invalid                     = "invalid";
    static const QString TableViewLight              = "tableviewlight";
    static const QString ButtonDark                  = "buttondark";
    static const QString ButtonLight                 = "buttonlight";
    static const QString ButtonGray                  = "buttongray";
    static const QString ButtonTransparent           = "buttontransparent";
    static const QString ButtonTransparentBordered   = "buttontransparentbordered";
    static const QString NavButton                   = "navbutton";
    static const QString NavGroupButton              = "navgroupbutton";
    static const QString NavSubGroupButton           = "navsubgroupbutton";
    static const QString TreeView                    = "treeview";
    static const QString ScrollBarLight              = "scrollbarlight";
    static const QString ScrollBarDark               = "scrollbardark";
}

/** The wallet has exactly two appearance modes. */
enum class AppearanceMode { Dark, Light };

/** Emits modeChanged() after the appearance was switched, so widgets that cache
 *  colours or pixmaps can refresh. */
class StyleSheetNotifier : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    void modeChanged();
};

/** Singleton that loads the QSS templates (res/styles/templates), substitutes the
 *  design tokens of the active mode (res/styles/{dark,light}.ini) and applies them.
 *  Switching the mode re-applies everything live. */
class StyleSheet
{
public:
    static StyleSheet& instance();
    void setStyleSheet(QWidget* widget, const QString& style_name);
    void setStyleSheet(QApplication* app, const QString& style_name);
    QVariant getStyleValue(const QString& key, const QVariant &defaultValue);

    AppearanceMode mode() const { return m_mode; }
    bool isDark() const { return m_mode == AppearanceMode::Dark; }
    /** Switch mode, persist it and re-apply all styles. */
    void setMode(AppearanceMode mode);
    void toggleMode() { setMode(isDark() ? AppearanceMode::Light : AppearanceMode::Dark); }
    /** Colour of a design token ("accent", "text-2-solid", ...). Understands #rrggbb and rgba(r,g,b,a). */
    QColor tokenColor(const QString& token) const;
    static QColor parseColor(const QString& value);
    QStringList knownStyleNames() const;
    StyleSheetNotifier* notifier() { return &m_notifier; }
    /** Animations off when the user asked for reduced motion. */
    static bool reducedMotion();
    static int motionMs(int ms) { return reducedMotion() ? 0 : ms; }

private:
    template<typename T>
    void setObjectStyleSheet(T* object, const QString& style_name);
    QString getStyleSheet(const QString& style_name);
    void loadMode(AppearanceMode mode);
    void applyToRegistered();

    explicit StyleSheet();
    QMap<QString, QString> m_cacheStyles;
    AppearanceMode m_mode{AppearanceMode::Dark};
    QPointer<QSettings> m_config;
    QMap<QString, QString> m_tokens;
    QVector<QPair<QPointer<QWidget>, QString>> m_registered;
    QString m_appStyleName;
    StyleSheetNotifier m_notifier;
};
#endif // STYLESHEET_H
