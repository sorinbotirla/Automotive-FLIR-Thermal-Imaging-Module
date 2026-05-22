#include "Config.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>

static int jInt(const QJsonObject& o, const char* k, int def) {
    return o.contains(k) ? o.value(k).toInt(def) : def;
}
static double jDbl(const QJsonObject& o, const char* k, double def) {
    return o.contains(k) ? o.value(k).toDouble(def) : def;
}
static bool jBool(const QJsonObject& o, const char* k, bool def) {
    return o.contains(k) ? o.value(k).toBool(def) : def;
}
static QString jStr(const QJsonObject& o, const char* k, const QString& def) {
    return o.contains(k) ? o.value(k).toString(def) : def;
}

static void loadLayer(const QJsonObject& o, LayerCfg& L) {
    L.offset_x   = jInt(o, "offset_x", L.offset_x);
    L.offset_y   = jInt(o, "offset_y", L.offset_y);
    L.rotate_deg = jInt(o, "rotate_deg", L.rotate_deg);
    L.scale      = jDbl(o, "scale", L.scale);
    L.opacity    = jDbl(o, "opacity", L.opacity);
    L.flip_h     = jBool(o, "flip_h", L.flip_h);
    L.flip_v     = jBool(o, "flip_v", L.flip_v);
}

static QJsonObject saveLayer(const LayerCfg& L) {
    QJsonObject o;
    o["offset_x"] = L.offset_x;
    o["offset_y"] = L.offset_y;
    o["rotate_deg"] = L.rotate_deg;
    o["scale"] = L.scale;
    o["opacity"] = L.opacity;
    o["flip_h"] = L.flip_h;
    o["flip_v"] = L.flip_v;
    return o;
}

bool ConfigIO::load(const QString& path, AppCfg& out) {
    QFile f(path);
    if (!f.exists()) return false;
    if (!f.open(QIODevice::ReadOnly)) return false;

    QByteArray data = f.readAll();
    if (data.size() == 0) return false;

    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;

    auto root = doc.object();
    out.background = jStr(root, "background", out.background);

    if (root.contains("usb_cam") && root["usb_cam"].isObject()) {
        auto u = root["usb_cam"].toObject();
        out.usb.enabled = jBool(u, "enabled", out.usb.enabled);
        out.usb.device  = jStr(u, "device", out.usb.device);
        out.usb.width   = jInt(u, "width", out.usb.width);
        out.usb.height  = jInt(u, "height", out.usb.height);
        out.usb.fps     = jInt(u, "fps", out.usb.fps);
        out.usb.emboss  = jBool(u, "emboss", out.usb.emboss);
        loadLayer(u, out.usb.xform);
    }

    if (root.contains("thermal") && root["thermal"].isObject()) {
        auto t = root["thermal"].toObject();
        out.thermal.enabled = jBool(t, "enabled", out.thermal.enabled);
        out.thermal.smooth  = jInt(t, "smooth", out.thermal.smooth);
        out.thermal.palette = jStr(t, "palette", out.thermal.palette).toLower();
        out.thermal.color_gate = jBool(t, "color_gate", out.thermal.color_gate);
        out.thermal.color_min_spread = jInt(t, "color_min_spread", out.thermal.color_min_spread);
        out.thermal.color_hot_percent = jInt(t, "color_hot_percent", out.thermal.color_hot_percent);
        out.thermal.boxes = jBool(t, "boxes", out.thermal.boxes);
        out.thermal.box_min_area = jInt(t, "box_min_area", out.thermal.box_min_area);
        out.thermal.ignore_enabled = jBool(t, "ignore_enabled", out.thermal.ignore_enabled);
        out.thermal.ignore_preview = jBool(t, "ignore_preview", out.thermal.ignore_preview);
        out.thermal.ignore_x = jInt(t, "ignore_x", out.thermal.ignore_x);
        out.thermal.ignore_y = jInt(t, "ignore_y", out.thermal.ignore_y);
        out.thermal.ignore_w = jInt(t, "ignore_w", out.thermal.ignore_w);
        out.thermal.ignore_h = jInt(t, "ignore_h", out.thermal.ignore_h);
        if (out.thermal.color_hot_percent < 0) out.thermal.color_hot_percent = 0;
        if (out.thermal.color_hot_percent > 100) out.thermal.color_hot_percent = 100;
        if (out.thermal.box_min_area < 1) out.thermal.box_min_area = 1;
        if (out.thermal.ignore_x < 0) out.thermal.ignore_x = 0;
        if (out.thermal.ignore_y < 0) out.thermal.ignore_y = 0;
        if (out.thermal.ignore_w < 0) out.thermal.ignore_w = 0;
        if (out.thermal.ignore_h < 0) out.thermal.ignore_h = 0;
        loadLayer(t, out.thermal.xform);
        out.thermal.xform.opacity = jDbl(t, "opacity", out.thermal.xform.opacity);
    }

    if (root.contains("overlays") && root["overlays"].isArray()) {
        out.overlays.clear();
        QJsonArray arr = root["overlays"].toArray();
        for (int i = 0; i < arr.size(); ++i) {
            if (!arr[i].isObject()) continue;
            QJsonObject o = arr[i].toObject();
            OverlayCfg ov;
            ov.file = jStr(o, "file", ov.file);
            ov.enabled = jBool(o, "enabled", ov.enabled);
            ov.x = jInt(o, "x", ov.x);
            ov.y = jInt(o, "y", ov.y);
            ov.w = jInt(o, "w", ov.w);
            ov.h = jInt(o, "h", ov.h);
            ov.opacity = jDbl(o, "opacity", ov.opacity);
            if (!ov.file.isEmpty()) out.overlays.push_back(ov);
        }
    }

    return true;
}



bool ConfigIO::save(const QString& path, const AppCfg& in) {
    QJsonObject root;
    root["background"] = in.background;

    QJsonObject u;
    u["enabled"] = in.usb.enabled;
    u["device"] = in.usb.device;
    u["width"] = in.usb.width;
    u["height"] = in.usb.height;
    u["fps"] = in.usb.fps;
    u["emboss"] = in.usb.emboss;
    auto ux = saveLayer(in.usb.xform);
    for (auto it = ux.begin(); it != ux.end(); ++it) u[it.key()] = it.value();
    root["usb_cam"] = u;

    QJsonObject t;
    t["enabled"] = in.thermal.enabled;
    t["smooth"] = in.thermal.smooth;
    t["palette"] = in.thermal.palette;
    t["color_gate"] = in.thermal.color_gate;
    t["color_min_spread"] = in.thermal.color_min_spread;
    t["color_hot_percent"] = in.thermal.color_hot_percent;
    t["boxes"] = in.thermal.boxes;
    t["box_min_area"] = in.thermal.box_min_area;
    t["ignore_enabled"] = in.thermal.ignore_enabled;
    t["ignore_preview"] = in.thermal.ignore_preview;
    t["ignore_x"] = in.thermal.ignore_x;
    t["ignore_y"] = in.thermal.ignore_y;
    t["ignore_w"] = in.thermal.ignore_w;
    t["ignore_h"] = in.thermal.ignore_h;
    auto tx = saveLayer(in.thermal.xform);
    for (auto it = tx.begin(); it != tx.end(); ++it) t[it.key()] = it.value();
    t["opacity"] = in.thermal.xform.opacity;
    root["thermal"] = t;

    QJsonArray overlays;
    for (const auto& ov : in.overlays) {
        if (ov.file.isEmpty()) continue;
        QJsonObject o;
        o["file"] = ov.file;
        o["enabled"] = ov.enabled;
        o["x"] = ov.x;
        o["y"] = ov.y;
        o["w"] = ov.w;
        o["h"] = ov.h;
        o["opacity"] = ov.opacity;
        overlays.append(o);
    }
    root["overlays"] = overlays;

    QJsonDocument doc(root);
    QByteArray bytes = doc.toJson(QJsonDocument::Indented);

    // Optional: keep a simple backup of the last good file
    QFile::remove(path + ".bak");
    if (QFile::exists(path)) QFile::copy(path, path + ".bak");

    QSaveFile f(path);
    f.setDirectWriteFallback(true); // safer on some filesystems
    if (!f.open(QIODevice::WriteOnly)) return false;

    if (f.write(bytes) != bytes.size()) return false;
    return f.commit(); // atomic replace
}

