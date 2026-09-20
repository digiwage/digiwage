// Copyright (c) 2026 The DigiWage developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/themedicon.h>

#include <qt/styleSheet.h>

#include <QCache>
#include <QFile>
#include <QGuiApplication>
#include <QIconEngine>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace {
// The colour the generated SVGs are drawn in; replaced with the themed colour when rendering.
const char* const SVG_PLACEHOLDER_COLOR = "#8e8e9a";

class ThemedIconEngine : public QIconEngine
{
public:
    ThemedIconEngine(const QString& resource, ThemedIcon::Role role, double opacity)
        : m_resource(resource), m_role(role), m_opacity(opacity) {}

    QIconEngine* clone() const override { return new ThemedIconEngine(m_resource, m_role, m_opacity); }
    QString key() const override { return QStringLiteral("digiwage-themed"); }
    QSize actualSize(const QSize& size, QIcon::Mode, QIcon::State) override { return size; }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override
    {
        const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        painter->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, dpr));
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale)
    {
        if (size.isEmpty()) return QPixmap();
        const QColor color = colorFor(mode, state);
        const QString key = QString("%1|%2|%3x%4@%5").arg(m_resource, color.name(QColor::HexArgb)).arg(size.width()).arg(size.height()).arg(scale);
        static QCache<QString, QPixmap> cache(512);
        if (QPixmap* hit = cache.object(key)) return *hit;

        QImage image(size * scale, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        image.setDevicePixelRatio(scale);
        QFile file(m_resource);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readAll();
            if (data.contains("<svg")) {
                QColor svgColor = color;
                svgColor.setAlpha(255);
                data.replace(SVG_PLACEHOLDER_COLOR, svgColor.name().toLatin1());
                QSvgRenderer renderer(data);
                QPainter p(&image);
                p.setRenderHint(QPainter::Antialiasing);
                p.setOpacity(color.alphaF());
                renderer.render(&p, QRectF(QPointF(0, 0), QSizeF(size)));
            } else {
                // Raster fallback: recolour the alpha mask.
                QImage src(m_resource);
                src = src.scaled(size * scale, Qt::KeepAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32);
                for (int y = 0; y < src.height(); ++y)
                    for (int x = 0; x < src.width(); ++x)
                        src.setPixel(x, y, qRgba(color.red(), color.green(), color.blue(), int(qAlpha(src.pixel(x, y)) * color.alphaF())));
                QPainter p(&image);
                p.drawImage(QPointF(0, 0), src);
            }
        }
        QPixmap pix = QPixmap::fromImage(image);
        cache.insert(key, new QPixmap(pix));
        return pix;
    }

private:
    QColor colorFor(QIcon::Mode mode, QIcon::State state) const
    {
        StyleSheet& style = StyleSheet::instance();
        QColor color;
        if (mode == QIcon::Disabled) {
            color = style.tokenColor("text-3-solid");
        } else {
            switch (m_role) {
            case ThemedIcon::NavBar:
                color = (mode == QIcon::Selected || state == QIcon::On) ? style.tokenColor("accent-text") : style.tokenColor("text-2-solid");
                break;
            case ThemedIcon::Button:
                color = style.tokenColor("text");
                break;
            case ThemedIcon::ButtonLight:
            case ThemedIcon::Text:
            case ThemedIcon::Menu:
                color = (mode == QIcon::Active) ? style.tokenColor("text") : style.tokenColor("text-2-solid");
                break;
            case ThemedIcon::Single:
            default:
                color = (mode == QIcon::Active) ? style.tokenColor("text") : style.tokenColor("text-2-solid");
                break;
            }
        }
        color.setAlphaF(color.alphaF() * m_opacity);
        return color;
    }

    QString m_resource;
    ThemedIcon::Role m_role;
    double m_opacity;
};
} // namespace

QIcon ThemedIcon::create(const QString& resource, Role role, double opacity)
{
    return QIcon(new ThemedIconEngine(resource, role, opacity));
}
