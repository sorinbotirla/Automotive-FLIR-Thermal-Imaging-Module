#pragma once
#include <QString>
#include <QVector>

struct LayerCfg {
    int offset_x = 0;
    int offset_y = 0;
    int rotate_deg = 0;
    double scale = 1.0;
    double opacity = 1.0;
    bool flip_h = false;
    bool flip_v = false;
};

struct UsbCamCfg {
    bool enabled = true;
    QString device = "/dev/video0";
    int width = 640;
    int height = 480;
    int fps = 15;
    bool emboss = false;
    LayerCfg xform;
};

struct ThermalCfg {
    bool enabled = true;
    int smooth = 0; // 0=off, higher=stronger

    // palette: color, gated, mono
    QString palette = "gated";

    // color_gate removes false heat colors when the scene has low real contrast
    bool color_gate = true;

    // raw Lepton count spread below this value means no real heat source, so color is suppressed
    int color_min_spread = 700;

    // only pixels above min + spread * percent / 100 are colored in gated mode
    int color_hot_percent = 70;

    // draw green rectangles around detected hot blobs
    bool boxes = false;

    // minimum connected hot pixels needed before drawing a box
    int box_min_area = 20;

    // ignore a fixed raw Lepton area from color gating and hot boxes
    bool ignore_enabled = false;
    bool ignore_preview = false;
    int ignore_x = 65;
    int ignore_y = 42;
    int ignore_w = 15;
    int ignore_h = 18;

    LayerCfg xform;
};


struct OverlayCfg {
    QString file;
    bool enabled = true;
    int x = 0;
    int y = 0;
    int w = 160;
    int h = 80;
    double opacity = 1.0;
};

struct AppCfg {
    QString background = "black"; // "black" or "grey"
    UsbCamCfg usb;
    ThermalCfg thermal;
    QVector<OverlayCfg> overlays;
};

class ConfigIO {
public:
    static bool load(const QString& path, AppCfg& out);
    static bool save(const QString& path, const AppCfg& in);
};
