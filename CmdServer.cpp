#include "CmdServer.h"
#include "Config.h"
#include "Lepton_I2C.h"

#include <QSocketNotifier>
#include <QDebug>
#include <QFile>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

static LayerCfg* pickLayer(AppCfg* cfg, const QString& src)
{
    if (!cfg) return nullptr;
    if (src == "camera" || src == "usb" || src == "usb_cam") return &cfg->usb.xform;
    if (src == "thermal") return &cfg->thermal.xform;
    return nullptr;
}

CmdServer::CmdServer(const QString& fifoPath,
                     const QString& cfgPath,
                     AppCfg* cfg,
                     QObject* parent)
    : QObject(parent),
      m_fifoPath(fifoPath),
      m_cfgPath(cfgPath),
      m_cfg(cfg)
{
    // Open FIFO non-blocking for read
    m_fd = ::open(m_fifoPath.toUtf8().constData(), O_RDONLY | O_NONBLOCK);
    if (m_fd < 0) {
        qDebug() << "CmdServer: open failed" << m_fifoPath << "errno" << errno;
        return;
    }

    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, SIGNAL(activated(int)), this, SLOT(onReadyRead()));

    qDebug() << "CmdServer: listening on" << m_fifoPath;
}

void CmdServer::onReadyRead()
{
    if (m_fd < 0) return;

    char buf[1024];
    ssize_t n = ::read(m_fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        // If writer closed, read returns 0; re-open to keep accepting future writers
        if (n == 0) {
            if (m_notifier) {
                m_notifier->setEnabled(false);
                m_notifier->deleteLater();
                m_notifier = nullptr;
            }
            ::close(m_fd);

            m_fd = ::open(m_fifoPath.toUtf8().constData(), O_RDONLY | O_NONBLOCK);
            if (m_fd >= 0) {
                m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
                connect(m_notifier, SIGNAL(activated(int)), this, SLOT(onReadyRead()));
            }
        }
        return;
    }

    buf[n] = 0;
    QString chunk = QString::fromUtf8(buf);
    const auto lines = chunk.split('\n', Qt::SkipEmptyParts);

    for (const auto& raw : lines) {
        QString line = raw.trimmed();
        if (line.isEmpty()) continue;

        bool changed = applyLine(line);
        if (changed) emit configChanged();
    }
}

bool CmdServer::applyLine(const QString& line)
{
    // Commands:
    // bg <black|grey>
    // set <camera|thermal> <offset_x|offset_y|rotate_deg|scale|opacity|flip_h|flip_v|emboss|smooth> <value>
    // ffc <off|manual|on|auto|run|manualrun>

    QStringList t = line.split(' ', Qt::SkipEmptyParts);
    if (t.isEmpty()) return false;

    bool changed = false;

    if (t[0] == "bg" && t.size() >= 2 && m_cfg) {
        QString v = t[1].toLower();

        if (v == "black" || v == "grey") {
            if (m_cfg->background != v) {
                m_cfg->background = v;
                changed = true;
            }
        }
    }
    else if (t[0] == "set" && t.size() >= 4 && m_cfg) {
        QString src = t[1].toLower();
        QString key = t[2].toLower();
        QString val = t[3];

        if (key == "enabled" && src == "thermal") {
            m_cfg->thermal.enabled = (val == "1" || val == "true" || val == "on");
            changed = true;
        }
        else if (key == "enabled" && (src == "camera" || src == "usb" || src == "usb_cam")) {
            m_cfg->usb.enabled = (val == "1" || val == "true" || val == "on");
            changed = true;
        }
        else {
            LayerCfg* L = pickLayer(m_cfg, src);
            if (!L) return false;

            if (key == "offset_x") {
            L->offset_x = val.toInt();
            changed = true;
        }
        else if (key == "offset_y") {
            L->offset_y = val.toInt();
            changed = true;
        }
        else if (key == "rotate_deg") {
            L->rotate_deg = val.toInt();
            changed = true;
        }
        else if (key == "scale") {
            L->scale = val.toDouble();
            changed = true;
        }
        else if (key == "opacity") {
            L->opacity = val.toDouble();
            changed = true;
        }
        else if (key == "flip_h") {
            L->flip_h = (val == "1" || val == "true");
            changed = true;
        }
        else if (key == "flip_v") {
            L->flip_v = (val == "1" || val == "true");
            changed = true;
        }
        else if (key == "emboss" && (src == "camera" || src == "usb" || src == "usb_cam")) {
            m_cfg->usb.emboss = (val == "1" || val == "true" || val == "on");
            changed = true;
        }
        else if (key == "smooth" && src == "thermal") {
            m_cfg->thermal.smooth = val.toInt();
            changed = true;
        }
        else if (key == "palette" && src == "thermal") {
            QString p = val.toLower();
            if (p == "color" || p == "gated" || p == "gated_color" || p == "mono" || p == "bw" || p == "blackwhite") {
                if (p == "gated_color") p = "gated";
                if (p == "bw" || p == "blackwhite") p = "mono";
                m_cfg->thermal.palette = p;
                changed = true;
            }
        }
        else if (key == "color_gate" && src == "thermal") {
            m_cfg->thermal.color_gate = (val == "1" || val == "true" || val == "on");
            changed = true;
        }
        else if (key == "color_min_spread" && src == "thermal") {
            m_cfg->thermal.color_min_spread = val.toInt();
            if (m_cfg->thermal.color_min_spread < 0) m_cfg->thermal.color_min_spread = 0;
            changed = true;
        }
        else if (key == "color_hot_percent" && src == "thermal") {
            m_cfg->thermal.color_hot_percent = val.toInt();
            if (m_cfg->thermal.color_hot_percent < 0) m_cfg->thermal.color_hot_percent = 0;
            if (m_cfg->thermal.color_hot_percent > 100) m_cfg->thermal.color_hot_percent = 100;
            changed = true;
        }
        else if (key == "boxes" && src == "thermal") {
            m_cfg->thermal.boxes = (val == "1" || val == "true" || val == "on");
            changed = true;
        }
        else if (key == "box_min_area" && src == "thermal") {
            m_cfg->thermal.box_min_area = val.toInt();
            if (m_cfg->thermal.box_min_area < 1) m_cfg->thermal.box_min_area = 1;
            changed = true;
        }
        else if (key == "ignore_enabled" && src == "thermal") {
            m_cfg->thermal.ignore_enabled = (val == "1" || val == "true" || val == "on");
            changed = true;
        }
        else if (key == "ignore_preview" && src == "thermal") {
            m_cfg->thermal.ignore_preview = (val == "1" || val == "true" || val == "on");
            changed = true;
        }
        else if (key == "ignore_x" && src == "thermal") {
            m_cfg->thermal.ignore_x = val.toInt();
            if (m_cfg->thermal.ignore_x < 0) m_cfg->thermal.ignore_x = 0;
            changed = true;
        }
        else if (key == "ignore_y" && src == "thermal") {
            m_cfg->thermal.ignore_y = val.toInt();
            if (m_cfg->thermal.ignore_y < 0) m_cfg->thermal.ignore_y = 0;
            changed = true;
        }
        else if (key == "ignore_w" && src == "thermal") {
            m_cfg->thermal.ignore_w = val.toInt();
            if (m_cfg->thermal.ignore_w < 0) m_cfg->thermal.ignore_w = 0;
            changed = true;
        }
        else if (key == "ignore_h" && src == "thermal") {
            m_cfg->thermal.ignore_h = val.toInt();
            if (m_cfg->thermal.ignore_h < 0) m_cfg->thermal.ignore_h = 0;
            changed = true;
        }
        }
    }
    else if (t[0] == "ffc" && t.size() >= 2) {
        QString v = t[1].toLower();

        if (v == "manual" || v == "off") {
            bool ok = lepton_set_ffc_manual();
            qDebug() << "CmdServer: FFC manual/off:" << ok;
            return ok;
        }

        if (v == "auto" || v == "on") {
            bool ok = lepton_set_ffc_auto();
            qDebug() << "CmdServer: FFC auto/on:" << ok;
            return ok;
        }

        if (v == "run" || v == "manualrun") {
            lepton_perform_ffc();
            qDebug() << "CmdServer: manual FFC triggered";
            return true;
        }

        return false;
    }
    else {
        qDebug() << "CmdServer: unknown cmd:" << line;
        return false;
    }

    if (changed) {
        ConfigIO::save(m_cfgPath, *m_cfg);
        qDebug() << "CmdServer: applied:" << line;
    }

    return changed;
}