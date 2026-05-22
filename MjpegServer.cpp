#include "MjpegServer.h"
#include "MyLabel.h"
#include "Config.h"

#include <QDateTime>
#include <QImage>
#include <QTimer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QBuffer>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

static QList<QByteArray> splitByteArray(const QByteArray& data, const QByteArray& sep)
{
    QList<QByteArray> out;

    if (sep.isEmpty()) {
        out.append(data);
        return out;
    }

    int start = 0;

    while (true) {
        int pos = data.indexOf(sep, start);
        if (pos < 0) {
            out.append(data.mid(start));
            break;
        }

        out.append(data.mid(start, pos - start));
        start = pos + sep.size();
    }

    return out;
}

static QByteArray mimeFor(const QString& fn)
{
    QString f = fn.toLower();
    if (f.endsWith(".html")) return "text/html; charset=utf-8";
    if (f.endsWith(".css"))  return "text/css; charset=utf-8";
    if (f.endsWith(".js"))   return "application/javascript; charset=utf-8";
    if (f.endsWith(".png"))  return "image/png";
    if (f.endsWith(".jpg") || f.endsWith(".jpeg")) return "image/jpeg";
    if (f.endsWith(".svg"))  return "image/svg+xml";
    return "application/octet-stream";
}

static QByteArray httpResponse(const QByteArray& body, const QByteArray& contentType = "text/html; charset=utf-8")
{
    QByteArray h;
    h += "HTTP/1.1 200 OK\r\n";
    h += "Connection: close\r\n";
    h += "Cache-Control: no-cache\r\n";
    h += "Pragma: no-cache\r\n";
    h += "Content-Type: " + contentType + "\r\n";
    h += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    h += "\r\n";
    return h + body;
}

static QByteArray httpNotFound()
{
    QByteArray body = "Not found";
    QByteArray h;
    h += "HTTP/1.1 404 Not Found\r\n";
    h += "Connection: close\r\n";
    h += "Content-Type: text/plain\r\n";
    h += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    h += "\r\n";
    return h + body;
}

static QByteArray queryValue(const QByteArray& url, const QByteArray& key)
{
    int q = url.indexOf('?');
    if (q < 0) return QByteArray();

    QByteArray qs = url.mid(q + 1);
    QList<QByteArray> items = qs.split('&');
    for (int i = 0; i < items.size(); i++) {
        QByteArray kv = items[i];
        int eq = kv.indexOf('=');
        if (eq < 0) continue;
        QByteArray k = QByteArray::fromPercentEncoding(kv.left(eq));
        QByteArray v = QByteArray::fromPercentEncoding(kv.mid(eq + 1));
        if (k == key) return v;
    }
    return QByteArray();
}

static QString safeFileName(QString name)
{
    name = QFileInfo(name).fileName();
    name.replace(QRegularExpression("[^A-Za-z0-9_.-]"), "_");
    if (name.isEmpty()) name = "overlay.png";
    return name;
}

QString MjpegServer::webRoot() const
{
    return QDir(QCoreApplication::applicationDirPath()).filePath("webapp");
}

QString MjpegServer::imagesDir() const
{
    return QDir(webRoot()).filePath("images");
}

bool MjpegServer::saveConfig()
{
    if (!m_cfg) return false;
    QString cfgPath = QCoreApplication::applicationDirPath() + "/config.json";
    bool ok = ConfigIO::save(cfgPath, *m_cfg);
    if (ok && m_source) m_source->setConfig(*m_cfg);
    return ok;
}

QByteArray MjpegServer::configJson() const
{
    if (!m_cfg) return "{}";

    QJsonObject root;
    root["background"] = m_cfg->background;

    QJsonObject usb;
    usb["enabled"]  = m_cfg->usb.enabled;
    usb["device"]   = m_cfg->usb.device;
    usb["width"]    = m_cfg->usb.width;
    usb["height"]   = m_cfg->usb.height;
    usb["fps"]      = m_cfg->usb.fps;
    usb["emboss"]   = m_cfg->usb.emboss;
    usb["offset_x"] = m_cfg->usb.xform.offset_x;
    usb["offset_y"] = m_cfg->usb.xform.offset_y;
    usb["scale"]    = m_cfg->usb.xform.scale;
    usb["opacity"]  = m_cfg->usb.xform.opacity;
    usb["rotate"]   = m_cfg->usb.xform.rotate_deg;
    usb["flip_h"]   = m_cfg->usb.xform.flip_h;
    usb["flip_v"]   = m_cfg->usb.xform.flip_v;
    root["usb_cam"] = usb;

    QJsonObject th;
    th["enabled"]  = m_cfg->thermal.enabled;
    th["smooth"]   = m_cfg->thermal.smooth;
    th["palette"]  = m_cfg->thermal.palette;
    th["color_gate"] = m_cfg->thermal.color_gate;
    th["color_min_spread"] = m_cfg->thermal.color_min_spread;
    th["color_hot_percent"] = m_cfg->thermal.color_hot_percent;
    th["boxes"] = m_cfg->thermal.boxes;
    th["box_min_area"] = m_cfg->thermal.box_min_area;
    th["ignore_enabled"] = m_cfg->thermal.ignore_enabled;
    th["ignore_preview"] = m_cfg->thermal.ignore_preview;
    th["ignore_x"] = m_cfg->thermal.ignore_x;
    th["ignore_y"] = m_cfg->thermal.ignore_y;
    th["ignore_w"] = m_cfg->thermal.ignore_w;
    th["ignore_h"] = m_cfg->thermal.ignore_h;
    th["offset_x"] = m_cfg->thermal.xform.offset_x;
    th["offset_y"] = m_cfg->thermal.xform.offset_y;
    th["scale"]    = m_cfg->thermal.xform.scale;
    th["opacity"]  = m_cfg->thermal.xform.opacity;
    th["rotate"]   = m_cfg->thermal.xform.rotate_deg;
    th["flip_h"]   = m_cfg->thermal.xform.flip_h;
    th["flip_v"]   = m_cfg->thermal.xform.flip_v;
    root["thermal"] = th;

    QJsonArray overlays;
    for (int i = 0; i < m_cfg->overlays.size(); ++i) {
        const OverlayCfg& ov = m_cfg->overlays[i];
        QJsonObject o;
        o["file"] = ov.file;
        o["url"] = QString("/images/") + ov.file;
        o["enabled"] = ov.enabled;
        o["x"] = ov.x;
        o["y"] = ov.y;
        o["w"] = ov.w;
        o["h"] = ov.h;
        o["opacity"] = ov.opacity;
        overlays.append(o);
    }
    root["overlays"] = overlays;

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray MjpegServer::loadStatic(const QByteArray& urlPath, QByteArray* outContentType)
{
    QString p = QString::fromUtf8(urlPath);
    if (p == "/" || p.isEmpty()) p = "/index.html";
    if (p.startsWith("/")) p = p.mid(1);

    p = QDir::cleanPath(p);
    if (p.startsWith("..")) return QByteArray();

    QString full = QDir(webRoot()).filePath(p);
    QFileInfo fi(full);
    if (!fi.exists() || !fi.isFile()) return QByteArray();

    QFile f(full);
    if (!f.open(QIODevice::ReadOnly)) return QByteArray();

    if (outContentType) *outContentType = mimeFor(full);
    return f.readAll();
}

bool MjpegServer::writeFifoLine(const QByteArray& line)
{
    QFile f("/tmp/lepton_cmd");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append)) return false;
    f.write(line);
    if (!line.endsWith("\n")) f.write("\n");
    f.flush();
    return true;
}

MjpegServer::MjpegServer(MyLabel* source, AppCfg* cfg, quint16 port, QObject* parent)
    : QTcpServer(parent), m_source(source), m_cfg(cfg), m_port(port)
{
    listen(QHostAddress::Any, m_port);
}

void MjpegServer::incomingConnection(qintptr socketDescriptor)
{
    auto* s = new QTcpSocket(this);
    if (!s->setSocketDescriptor(socketDescriptor)) {
        s->deleteLater();
        return;
    }
    handleClient(s);
}

void MjpegServer::handleClient(QTcpSocket* s)
{
    s->setSocketOption(QAbstractSocket::LowDelayOption, 1);

    QByteArray* request = new QByteArray();
    connect(s, &QTcpSocket::destroyed, this, [request]() { delete request; });

    QObject::connect(s, &QTcpSocket::readyRead, this, [this, s, request]() {
        request->append(s->readAll());

        int headerEnd = request->indexOf("\r\n\r\n");
        if (headerEnd < 0) return;

        QByteArray headers = request->left(headerEnd);
        int contentLength = 0;
        QList<QByteArray> lines = headers.split('\n');
        for (int i = 0; i < lines.size(); ++i) {
            QByteArray l = lines[i].trimmed();
            if (l.toLower().startsWith("content-length:")) {
                contentLength = l.mid(15).trimmed().toInt();
            }
        }

        if (request->size() < headerEnd + 4 + contentLength) return;
        processRequest(s, *request);
    });

    QObject::connect(s, &QTcpSocket::disconnected, s, &QTcpSocket::deleteLater);
}

void MjpegServer::processRequest(QTcpSocket* s, const QByteArray& req)
{
    int eol = req.indexOf("\r\n");
    if (eol < 0) return;

    QByteArray first = req.left(eol);
    QList<QByteArray> parts = first.split(' ');
    if (parts.size() < 2) return;

    QByteArray method = parts[0];
    QByteArray url = parts[1];
    QByteArray path = url;
    int qpos = path.indexOf('?');
    if (qpos >= 0) path = path.left(qpos);

    if (path == "/api/config") {
        s->write(httpResponse(configJson(), "application/json; charset=utf-8"));
        s->flush();
        s->disconnectFromHost();
        return;
    }

    if (path == "/api/cmd") {
        QByteArray line = queryValue(url, "line");
        bool ok = (!line.trimmed().isEmpty()) && writeFifoLine(line);
        QByteArray body = ok ? "{\"ok\":true}\n" : "{\"ok\":false}\n";
        s->write(httpResponse(body, "application/json; charset=utf-8"));
        s->flush();
        s->disconnectFromHost();
        return;
    }

    if (path == "/api/overlay_set" && m_cfg) {
        int i = queryValue(url, "i").toInt();
        QString key = QString::fromUtf8(queryValue(url, "key")).toLower();
        QString val = QString::fromUtf8(queryValue(url, "value"));

        bool ok = (0 <= i && i < m_cfg->overlays.size());
        if (ok) {
            OverlayCfg& ov = m_cfg->overlays[i];
            if (key == "enabled") ov.enabled = (val == "1" || val == "true" || val == "on");
            else if (key == "x") ov.x = val.toInt();
            else if (key == "y") ov.y = val.toInt();
            else if (key == "w") ov.w = val.toInt();
            else if (key == "h") ov.h = val.toInt();
            else if (key == "opacity") ov.opacity = val.toDouble();
            else ok = false;
            if (ov.opacity < 0) ov.opacity = 0;
            if (ov.opacity > 1) ov.opacity = 1;
            if (ov.w < 1) ov.w = 1;
            if (ov.h < 1) ov.h = 1;
            if (ok) ok = saveConfig();
        }

        QByteArray body = ok ? "{\"ok\":true}\n" : "{\"ok\":false}\n";
        s->write(httpResponse(body, "application/json; charset=utf-8"));
        s->flush();
        s->disconnectFromHost();
        return;
    }

    if (path == "/api/overlay_delete" && m_cfg) {
        int i = queryValue(url, "i").toInt();
        bool ok = (0 <= i && i < m_cfg->overlays.size());
        if (ok) {
            QString file = m_cfg->overlays[i].file;
            m_cfg->overlays.remove(i);
            QFile::remove(QDir(imagesDir()).filePath(file));
            ok = saveConfig();
        }

        QByteArray body = ok ? "{\"ok\":true}\n" : "{\"ok\":false}\n";
        s->write(httpResponse(body, "application/json; charset=utf-8"));
        s->flush();
        s->disconnectFromHost();
        return;
    }

    if (path == "/api/upload" && method == "POST" && m_cfg) {
        int headerEnd = req.indexOf("\r\n\r\n");
        QByteArray headers = req.left(headerEnd);
        QByteArray body = req.mid(headerEnd + 4);

        QByteArray boundary;
        QList<QByteArray> headerLines = headers.split('\n');
        for (int i = 0; i < headerLines.size(); ++i) {
            QByteArray l = headerLines[i].trimmed();
            QByteArray lower = l.toLower();
            if (lower.startsWith("content-type:") && lower.contains("boundary=")) {
                int b = lower.indexOf("boundary=");
                boundary = l.mid(b + 9).trimmed();
                if (boundary.startsWith('"') && boundary.endsWith('"')) boundary = boundary.mid(1, boundary.size() - 2);
            }
        }

        bool ok = !boundary.isEmpty();
        int added = 0;
        if (ok) {
            QDir().mkpath(imagesDir());
            QByteArray sep = "--" + boundary;
            QList<QByteArray> parts = splitByteArray(body, sep);
            for (int pi = 0; pi < parts.size(); ++pi) {
                QByteArray part = parts[pi];
                if (part.contains("--\r\n") && part.trimmed() == "--") continue;
                int pe = part.indexOf("\r\n\r\n");
                if (pe < 0) continue;

                QByteArray ph = part.left(pe);
                QByteArray data = part.mid(pe + 4);
                if (data.endsWith("\r\n")) data.chop(2);
                if (data.isEmpty()) continue;

                QString fileName;
                QRegularExpression re("filename=\"([^\"]+)\"");
                QRegularExpressionMatch m = re.match(QString::fromUtf8(ph));
                if (m.hasMatch()) fileName = safeFileName(m.captured(1));
                if (fileName.isEmpty()) continue;

                QString lower = fileName.toLower();
                if (!(lower.endsWith(".png") || lower.endsWith(".jpg") || lower.endsWith(".jpeg"))) continue;

                QString base = QFileInfo(fileName).completeBaseName();
                QString ext = QFileInfo(fileName).suffix().toLower();
                QString unique = base + "_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + "_" + QString::number(added) + "." + ext;
                QString full = QDir(imagesDir()).filePath(unique);

                QFile out(full);
                if (!out.open(QIODevice::WriteOnly)) continue;
                out.write(data);
                out.close();

                QImage img(full);
                OverlayCfg ov;
                ov.file = unique;
                ov.enabled = true;
                ov.x = 0;
                ov.y = 0;
                ov.w = img.isNull() ? 160 : img.width();
                ov.h = img.isNull() ? 80 : img.height();
                ov.opacity = 1.0;
                m_cfg->overlays.push_back(ov);
                added++;
            }
            ok = (added > 0) && saveConfig();
        }

        QByteArray resp = "{\"ok\":" + QByteArray(ok ? "true" : "false") + ",\"added\":" + QByteArray::number(added) + "}\n";
        s->write(httpResponse(resp, "application/json; charset=utf-8"));
        s->flush();
        s->disconnectFromHost();
        return;
    }

    if (path == "/mjpeg") {
        QByteArray h;
        h += "HTTP/1.1 200 OK\r\n";
        h += "Connection: close\r\n";
        h += "Cache-Control: no-cache\r\n";
        h += "Pragma: no-cache\r\n";
        h += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n";
        h += "\r\n";
        s->write(h);
        s->flush();

        auto* t = new QTimer(s);
        t->setInterval(100);
        QObject::connect(t, &QTimer::timeout, this, [this, s]() {
            if (!s->isOpen()) return;

            QImage img = m_source ? m_source->getLastComposite() : QImage();
            if (img.isNull()) return;

            QByteArray jpg;
            QBuffer buf(&jpg);
            buf.open(QIODevice::WriteOnly);
            img.convertToFormat(QImage::Format_RGB888).save(&buf, "JPG", 70);

            QByteArray part;
            part += "--frame\r\n";
            part += "Content-Type: image/jpeg\r\n";
            part += "Content-Length: " + QByteArray::number(jpg.size()) + "\r\n";
            part += "\r\n";
            s->write(part);
            s->write(jpg);
            s->write("\r\n");
            s->flush();
        });
        t->start();

        QObject::connect(s, &QTcpSocket::disconnected, t, &QTimer::deleteLater);
        return;
    }

    QByteArray ct;
    QByteArray fileBody = loadStatic(path, &ct);
    if (!fileBody.isEmpty()) {
        s->write(httpResponse(fileBody, ct));
        s->flush();
        s->disconnectFromHost();
        return;
    }

    s->write(httpNotFound());
    s->flush();
    s->disconnectFromHost();
}
