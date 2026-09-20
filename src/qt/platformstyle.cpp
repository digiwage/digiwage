// Copyright (c) 2015-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/platformstyle.h>
#include <qt/guiconstants.h>
#include "styleSheet.h"
#include <qt/themedicon.h>
#include <QApplication>
#include <QColor>
#include <QImage>
#include <QPalette>

static const struct {
    const char *platformId;
    /** Show images on push buttons */
    const bool imagesOnButtons;
    /** Colorize single-color icons */
    const bool colorizeIcons;
    /** Extra padding/spacing in transactionview */
    const bool useExtraSpacing;
} platform_styles[] = {
    {"macosx", true, true, false},
    {"windows", true, true, false},
    /* Other: linux, unix, ... */
    {"other", true, true, false}
};

namespace {
/* Local functions for colorizing single-color images */

void MakeSingleColorImage(QImage& img, const QColor& colorbase, double opacity = 1)
{
    //Opacity representation in percentage (0, 1) i.e. (0%, 100%)
    if(opacity > 1 && opacity < 0) opacity = 1;

    img = img.convertToFormat(QImage::Format_ARGB32);
    for (int x = img.width(); x--; )
    {
        for (int y = img.height(); y--; )
        {
            const QRgb rgb = img.pixel(x, y);
            img.setPixel(x, y, qRgba(colorbase.red(), colorbase.green(), colorbase.blue(), opacity * qAlpha(rgb)));
        }
    }
}
QPixmap MakeSingleColorPixmap(QImage& img, const QColor& colorbase, double opacity = 1)
{
    MakeSingleColorImage(img, colorbase, opacity);
    return QPixmap::fromImage(img);
}

QIcon ColorizeIcon(const QIcon& ico, const QColor& colorbase, double opacity = 1)
{
    QIcon new_ico;
    for (const QSize& sz : ico.availableSizes())
    {
        QImage img(ico.pixmap(sz).toImage());
        MakeSingleColorImage(img, colorbase, opacity);
        new_ico.addPixmap(QPixmap::fromImage(img));
    }
    return new_ico;
}

QImage ColorizeImage(const QString& filename, const QColor& colorbase, double opacity = 1)
{
    QImage img(filename);
    MakeSingleColorImage(img, colorbase, opacity);
    return img;
}

QIcon ColorizeIcon(const QString& filename, const QColor& colorbase, double opacity = 1) 
{
    return QIcon(QPixmap::fromImage(ColorizeImage(filename, colorbase, opacity)));
}

}


PlatformStyle::PlatformStyle(const QString &_name, bool _imagesOnButtons, bool _colorizeIcons, bool _useExtraSpacing):
    name(_name),
    version(1),
    imagesOnButtons(_imagesOnButtons),
    colorizeIcons(_colorizeIcons),
    useExtraSpacing(_useExtraSpacing),
    singleColor(0,0,0),
    textColor(0,0,0),
    menuColor(0,0,0)
{
}

QColor PlatformStyle::TextColor() const { return StyleSheet::instance().tokenColor("text"); }
QColor PlatformStyle::SingleColor() const { return StyleSheet::instance().tokenColor("text-2-solid"); }
QColor PlatformStyle::MenuColor() const { return StyleSheet::instance().tokenColor("text-2-solid"); }

QImage PlatformStyle::SingleColorImage(const QString& filename) const
{
    if (!colorizeIcons) return QImage(filename);
    return ColorizeImage(filename, SingleColor(), 0.8);
}

QIcon PlatformStyle::SingleColorIcon(const QString& filename) const
{
    if (!colorizeIcons) return QIcon(filename);
    return ThemedIcon::create(filename, ThemedIcon::Single);
}

QIcon PlatformStyle::SingleColorIcon(const QIcon& icon) const
{
    if (!colorizeIcons) return icon;
    return ColorizeIcon(icon, SingleColor());
}

QIcon PlatformStyle::TextColorIcon(const QIcon& icon) const
{
    return ColorizeIcon(icon, TextColor(), 0.6);
}

QIcon PlatformStyle::MenuColorIcon(const QString &filename) const
{
    return ThemedIcon::create(filename, ThemedIcon::Menu);
}

QIcon PlatformStyle::MultiStatesIcon(const QString &resourcename, StateType type) const
{
    switch (type) {
    case NavBar: return ThemedIcon::create(resourcename, ThemedIcon::NavBar);
    case PushButtonLight: return ThemedIcon::create(resourcename, ThemedIcon::ButtonLight);
    case PushButton:
    case PushButtonIcon:
    default: return ThemedIcon::create(resourcename, ThemedIcon::Button);
    }
}

void PlatformStyle::TableColor(PlatformStyle::TableColorType type, QColor &color, double &opacity) const
{
    StyleSheet& style = StyleSheet::instance();
    opacity = 1;
    switch (type) {
    case Normal: color = style.tokenColor("text-2-solid"); break;
    case Input: color = style.tokenColor("accent-text"); break;
    case Inout:
    case Output: color = style.tokenColor("success"); break;
    case Error: color = style.tokenColor("danger"); break;
    default: color = style.tokenColor("text"); break;
    }
}

QIcon PlatformStyle::TableColorIcon(const QString &resourcename, TableColorType type) const
{
    QColor color; double opacity = 1;
    TableColor(type, color, opacity);
    QIcon icon;
    QImage img1(resourcename), img2(resourcename);
    QPixmap pix1 = MakeSingleColorPixmap(img1, color, opacity);
    QPixmap pix2 = MakeSingleColorPixmap(img2, StyleSheet::instance().tokenColor("text"), 1);
    icon.addPixmap(pix1, QIcon::Normal, QIcon::On);
    icon.addPixmap(pix1, QIcon::Normal, QIcon::Off);
    icon.addPixmap(pix2, QIcon::Selected, QIcon::On);
    icon.addPixmap(pix2, QIcon::Selected, QIcon::Off);
    return icon;
}

QImage PlatformStyle::TableColorImage(const QString &resourcename, PlatformStyle::TableColorType type) const
{
    QImage img(resourcename);
    QColor color; double opacity = 1;
    TableColor(type, color, opacity);
    MakeSingleColorImage(img, color, opacity);
    return img;
}

void PlatformStyle::SingleColorImage(QImage &img, const QColor &colorbase, double opacity)
{
    MakeSingleColorImage(img, colorbase, opacity);
}

QIcon PlatformStyle::SingleColorIcon(const QString &resourcename, const QColor &colorbase, double opacity)
{
    return ColorizeIcon(resourcename, colorbase, opacity);
}

QIcon PlatformStyle::SingleColorIcon(const QIcon &icon, const QColor &colorbase, double opacity)
{
    return ColorizeIcon(icon, colorbase, opacity);
}

const PlatformStyle *PlatformStyle::instantiate(const QString &platformId)
{
    for (const auto& platform_style : platform_styles) {
        if (platformId == platform_style.platformId) {
            return new PlatformStyle(platform_style.platformId, platform_style.imagesOnButtons,
                                     platform_style.colorizeIcons, platform_style.useExtraSpacing);
        }
    }
    return nullptr;
}
