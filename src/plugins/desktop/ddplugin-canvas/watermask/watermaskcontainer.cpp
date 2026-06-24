// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "watermaskcontainer.h"
#include "watermasksystem.h"
#include "watermaskframe.h"
#include "customwatermasklabel.h"

#include <QFile>
#include <QTextStream>
#include <QJsonParseError>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

using namespace ddplugin_canvas;

static constexpr char kConfFile[] = "/usr/share/deepin/dde-desktop-watermask.json";

WatermaskContainer::WatermaskContainer(QWidget *parent) : QObject(parent)
{
    custom = new CustomWaterMaskLabel(parent);
    if (WatermaskSystem::isEnable()) {
        fmInfo() << "Using WatermaskSystem for water mask display";
        control = new WatermaskSystem(parent);

        custom->lower();
        control->stackUnder(custom);

        connect(control, &WatermaskSystem::showedOn, custom, &CustomWaterMaskLabel::onSystemMaskShow);
    } else {
        fmInfo() << "Using WaterMaskFrame for water mask display";
        frame = new WaterMaskFrame(kConfFile, parent);

        custom->lower();
        frame->stackUnder(custom);

        connect(frame, &WaterMaskFrame::showMask, custom, &CustomWaterMaskLabel::onSystemMaskShow);
    }
}

bool WatermaskContainer::isEnable()
{
    static int on = -1;
    if (on > -1) {
        return on > 0;
    }

    // Check for Alpha/development version - always enable watermark on Alpha
    {
        QFile osRelease("/etc/os-release");
        if (osRelease.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&osRelease);
            while (!in.atEnd()) {
                QString line = in.readLine();
                if (line.startsWith("VERSION_TYPE=")) {
                    QString vt = line.mid(13).trimmed();
                    if (vt.startsWith('"') && vt.endsWith('"'))
                        vt = vt.mid(1, vt.length() - 2);
                    else if (vt.startsWith('\'') && vt.endsWith('\''))
                        vt = vt.mid(1, vt.length() - 2);
                    if (vt.compare("Alpha", Qt::CaseInsensitive) == 0) {
                        on = 1;
                        fmInfo() << "Alpha version detected, water mask always enabled";
                        return true;
                    }
                    break;
                }
            }
        }
    }

    fmDebug() << "Checking water mask configuration from:" << kConfFile;
    QFile file(kConfFile);
    if (!file.open(QFile::ReadOnly)) {
        fmWarning() << "Water mask config file doesn't exist:" << kConfFile;
        on = 0;
        return on;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error == QJsonParseError::NoError) {
        auto json = doc.toVariant().toMap();
        if (json.contains("isMaskAlwaysOn")) {
            bool enabled = json.value("isMaskAlwaysOn", false).toBool();
            on = enabled ? 1 : 0;
            fmInfo() << "Water mask configuration loaded - isMaskAlwaysOn:" << enabled;
        } else {
            fmWarning() << "Water mask config missing 'isMaskAlwaysOn' property";
            on = 0;
        }
    } else {
        fmCritical() << "Invalid water mask config file" << kConfFile << "- error:" << error.errorString();
        on = 0;
    }

    return on;
}


void WatermaskContainer::refresh()
{
    if (control)
        control->refresh();
    else if (frame)
        frame->refresh();

    custom->refresh();
}

void WatermaskContainer::updatePosition()
{
    if (control)
        control->updatePosition();
    else if (frame)
        frame->updatePosition();
}
