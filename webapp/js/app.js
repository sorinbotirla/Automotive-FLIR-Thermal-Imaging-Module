/* global $, window, FormData */

var FlirCam = function () {
    var _self = this;

    _self.waiters = {};
    _self.repeaters = {};
    _self.cfg = null;

    _self.apiGet = function (url, cb) {
        $.getJSON(url, function (d) {
            if (cb) cb(d);
        });
    };

    _self.apiCmd = function (line, cb) {
        $.getJSON("/api/cmd?line=" + encodeURIComponent(line), function (d) {
            if (cb) cb(d);
        });
    };

    _self.setVal = function (id, val, def) {
        if (typeof val === "undefined" || val === null) val = def;
        $("#" + id).val(val);
        $("#" + id + "_val").text(val);
    };

    _self.isIgnoreSlider = function (id) {
        return id === "thermal_ignore_x" || id === "thermal_ignore_y" || id === "thermal_ignore_w" || id === "thermal_ignore_h";
    };

    _self.showIgnorePreview = function () {
        _self.apiCmd("set thermal ignore_preview 1");
        clearTimeout(_self.waiters.ignorePreview);
        _self.waiters.ignorePreview = setTimeout(function () {
            if (!$("#thermal_ignore_preview").prop("checked")) {
                _self.apiCmd("set thermal ignore_preview 0");
            }
        }, 1200);
    };

    _self.showPane = function (pane) {
        $(".pane-btn").removeClass("active");
        $('.pane-btn[data-pane="' + pane + '"]').addClass("active");
        $(".pane").removeClass("active");
        $('.pane[data-pane="' + pane + '"]').addClass("active");
    };

    _self.loadConfig = function () {
        _self.apiGet("/api/config?ts=" + Date.now(), function (cfg) {
            _self.cfg = cfg;
            _self.applyConfigToUI();
        });
    };

    _self.applyConfigToUI = function () {
        var c = _self.cfg;
        if (!c) return;

        $("#bg_mode").val(c.background);

        if (c.thermal) {
            $("#thermal_enabled").prop("checked", c.thermal.enabled);
            $("#thermal_palette").val(c.thermal.palette || "gated");
            $("#thermal_boxes").prop("checked", !!c.thermal.boxes);
            $("#thermal_ignore_enabled").prop("checked", !!c.thermal.ignore_enabled);
            $("#thermal_ignore_preview").prop("checked", !!c.thermal.ignore_preview);

            _self.setVal("thermal_smooth", c.thermal.smooth, 0);
            _self.setVal("thermal_color_min_spread", c.thermal.color_min_spread, 700);
            _self.setVal("thermal_color_hot_percent", c.thermal.color_hot_percent, 70);
            _self.setVal("thermal_box_min_area", c.thermal.box_min_area, 20);
            _self.setVal("thermal_ignore_x", c.thermal.ignore_x, 65);
            _self.setVal("thermal_ignore_y", c.thermal.ignore_y, 42);
            _self.setVal("thermal_ignore_w", c.thermal.ignore_w, 15);
            _self.setVal("thermal_ignore_h", c.thermal.ignore_h, 18);

            _self.setVal("th_offx", c.thermal.offset_x, 0);
            _self.setVal("th_offy", c.thermal.offset_y, 0);
            _self.setVal("th_scale", c.thermal.scale, 1);
            _self.setVal("th_opacity", c.thermal.opacity, 1);
            _self.setVal("th_rot", c.thermal.rotate, 0);
        }

        if (c.usb_cam) {
            $("#cam_enabled").prop("checked", c.usb_cam.enabled);
            $("#cam_emboss").prop("checked", c.usb_cam.emboss);

            _self.setVal("cam_offx", c.usb_cam.offset_x, 0);
            _self.setVal("cam_offy", c.usb_cam.offset_y, 0);
            _self.setVal("cam_scale", c.usb_cam.scale, 1);
            _self.setVal("cam_opacity", c.usb_cam.opacity, 1);
            _self.setVal("cam_rot", c.usb_cam.rotate, 0);
        }

        _self.renderOverlays(c.overlays || []);
    };

    _self.sendSlider = function (id, v) {
        var map = {
            thermal_smooth: { src: "thermal", key: "smooth" },
            thermal_color_min_spread: { src: "thermal", key: "color_min_spread" },
            thermal_color_hot_percent: { src: "thermal", key: "color_hot_percent" },
            thermal_box_min_area: { src: "thermal", key: "box_min_area" },
            thermal_ignore_x: { src: "thermal", key: "ignore_x" },
            thermal_ignore_y: { src: "thermal", key: "ignore_y" },
            thermal_ignore_w: { src: "thermal", key: "ignore_w" },
            thermal_ignore_h: { src: "thermal", key: "ignore_h" },

            th_offx: { src: "thermal", key: "offset_x" },
            th_offy: { src: "thermal", key: "offset_y" },
            th_scale: { src: "thermal", key: "scale" },
            th_opacity: { src: "thermal", key: "opacity" },
            th_rot: { src: "thermal", key: "rotate_deg" },

            cam_offx: { src: "usb", key: "offset_x" },
            cam_offy: { src: "usb", key: "offset_y" },
            cam_scale: { src: "usb", key: "scale" },
            cam_opacity: { src: "usb", key: "opacity" },
            cam_rot: { src: "usb", key: "rotate_deg" }
        };

        if (id.indexOf("overlay_") === 0) {
            _self.sendOverlaySlider(id, v);
            return;
        }

        if (!map[id]) return;
        _self.apiCmd("set " + map[id].src + " " + map[id].key + " " + v);
    };

    _self.renderOverlays = function (items) {
        var html = "";
        if (!items || items.length === 0) {
            html += '<div class="empty-note">No overlay images uploaded.</div>';
        }

        for (var i = 0; i < items.length; i++) {
            var o = items[i];
            var url = o.url || ("/images/" + o.file);
            html += '<div class="overlay-item" data-index="' + i + '">';
            html += '<div class="overlay-head">';
            html += '<img class="overlay-thumb" src="' + url + '?ts=' + Date.now() + '" alt="thumb">';
            html += '<div class="overlay-name">' + o.file + '</div>';
            html += '<button type="button" class="overlay-delete" data-index="' + i + '">Delete</button>';
            html += '</div>';

            html += '<label class="chk"><input type="checkbox" class="overlay-enabled" data-index="' + i + '" ' + (o.enabled ? 'checked' : '') + '><span>Enabled</span></label>';
            html += _self.overlaySlider(i, "x", "x", -500, 1000, 1, o.x);
            html += _self.overlaySlider(i, "y", "y", -500, 1000, 1, o.y);
            html += _self.overlaySlider(i, "w", "width", 1, 1200, 1, o.w);
            html += _self.overlaySlider(i, "h", "height", 1, 1200, 1, o.h);
            html += _self.overlaySlider(i, "opacity", "opacity", 0, 1, 0.01, o.opacity);
            html += '</div>';
        }
        $("#overlay_list").html(html);
    };

    _self.overlaySlider = function (i, key, label, min, max, step, value) {
        var id = "overlay_" + i + "_" + key;
        return '' +
            '<label class="slider overlay-slider-row">' +
            '<span>' + label + '</span>' +
            '<button type="button" class="step-btn" data-target="' + id + '" data-dir="-1">-</button>' +
            '<input type="range" class="overlay-slider" id="' + id + '" data-index="' + i + '" data-key="' + key + '" min="' + min + '" max="' + max + '" step="' + step + '" value="' + value + '">' +
            '<button type="button" class="step-btn" data-target="' + id + '" data-dir="1">+</button>' +
            '<span class="val" id="' + id + '_val">' + value + '</span>' +
            '</label>';
    };

    _self.sendOverlaySlider = function (id, v) {
        var $el = $("#" + id);
        var i = $el.attr("data-index");
        var key = $el.attr("data-key");
        if (typeof i === "undefined" || !key) return;
        _self.overlaySet(i, key, v);
    };

    _self.overlaySet = function (i, key, value, cb) {
        $.getJSON("/api/overlay_set?i=" + encodeURIComponent(i) + "&key=" + encodeURIComponent(key) + "&value=" + encodeURIComponent(value), function (d) {
            if (cb) cb(d);
        });
    };

    _self.uploadOverlays = function () {
        var input = document.getElementById("overlay_files");
        if (!input || !input.files || input.files.length === 0) return;

        var fd = new FormData();
        for (var i = 0; i < input.files.length; i++) {
            fd.append("images", input.files[i]);
        }

        $.ajax({
            url: "/api/upload",
            type: "POST",
            data: fd,
            processData: false,
            contentType: false,
            success: function () {
                input.value = "";
                _self.loadConfig();
            }
        });
    };

    _self.handleEvents = function () {
        $("body").on("click", ".pane-btn", function () {
            _self.showPane($(this).attr("data-pane"));
        });

        $("body").on("input", ".slider input", function () {
            var id = this.id;
            $("#" + id + "_val").text(this.value);

            if (_self.isIgnoreSlider(id)) {
                _self.showIgnorePreview();
            }

            if (id.indexOf("overlay_") === 0 || _self.isIgnoreSlider(id)) {
                clearTimeout(_self.waiters[id]);
                _self.waiters[id] = setTimeout(function () {
                    _self.sendSlider(id, $("#" + id).val());
                }, 80);
            }
        });

        $("body").on("change mouseup touchend", ".slider input", function () {
            if (_self.isIgnoreSlider(this.id)) {
                _self.showIgnorePreview();
            }
            _self.sendSlider(this.id, this.value);
        });

        $("body").on("change", "#bg_mode", function () {
            _self.apiCmd("bg " + this.value);
        });

        $("body").on("change", "#thermal_palette", function () {
            _self.apiCmd("set thermal palette " + this.value);
        });

        $("body").on("change", "#thermal_boxes", function () {
            _self.apiCmd("set thermal boxes " + (this.checked ? "1" : "0"));
        });

        $("body").on("change", "#thermal_enabled", function () {
            _self.apiCmd("set thermal enabled " + (this.checked ? "1" : "0"));
        });

        $("body").on("change", "#cam_enabled", function () {
            _self.apiCmd("set usb enabled " + (this.checked ? "1" : "0"));
        });

        $("body").on("change", "#thermal_ignore_enabled", function () {
            _self.apiCmd("set thermal ignore_enabled " + (this.checked ? "1" : "0"));
        });

        $("body").on("change", "#thermal_ignore_preview", function () {
            _self.apiCmd("set thermal ignore_preview " + (this.checked ? "1" : "0"));
        });

        $("body").on("change", "#cam_emboss", function () {
            _self.apiCmd("set usb emboss " + (this.checked ? "1" : "0"));
        });

        $("body").on("change", ".overlay-enabled", function () {
            _self.overlaySet($(this).attr("data-index"), "enabled", this.checked ? "1" : "0");
        });

        $("body").on("click", ".overlay-delete", function () {
            var i = $(this).attr("data-index");
            if (!window.confirm("Delete this overlay image?")) return;
            $.getJSON("/api/overlay_delete?i=" + encodeURIComponent(i), function () {
                _self.loadConfig();
            });
        });

        $("body").on("click", "#btn_upload_overlays", function () {
            _self.uploadOverlays();
        });

        $("body").on("click", ".cmd-btn", function () {
            var cmd = $(this).attr("data-cmd");
            if (cmd) _self.apiCmd(cmd);
        });

        $("body").on("click", "#btn_reload", function () {
            $("#stream").attr("src", "/mjpeg?ts=" + Date.now());
        });

        $("body").on("click", ".step-btn", function () {
            var target = $(this).attr("data-target");
            var dir = parseFloat($(this).attr("data-dir") || "0");
            var $inp = $("#" + target);
            if ($inp.length === 0) return;

            var step = parseFloat($inp.attr("step") || "1");
            var min = parseFloat($inp.attr("min"));
            var max = parseFloat($inp.attr("max"));
            var cur = parseFloat($inp.val());

            var next = cur + (dir * step);
            if (!isNaN(min)) next = Math.max(min, next);
            if (!isNaN(max)) next = Math.min(max, next);

            if (String(step).indexOf(".") >= 0) {
                next = Math.round(next * 100) / 100;
            }

            $inp.val(next);
            $("#" + target + "_val").text(String(next));

            if (_self.isIgnoreSlider(target)) {
                _self.showIgnorePreview();
            }

            _self.sendSlider(target, next);
        });
    };

    _self.init = function () {
        _self.handleEvents();
        _self.showPane("theme");
        _self.loadConfig();
        console.log("FlirCam initialized");
    };
};
