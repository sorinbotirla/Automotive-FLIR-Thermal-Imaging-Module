#include <iostream>

#include "LeptonThread.h"

#include "Palettes.h"
#include "SPI.h"
#include "Lepton_I2C.h"
#include <QElapsedTimer>
#include <vector>


#define PACKET_SIZE 164
#define PACKET_SIZE_UINT16 (PACKET_SIZE/2)
#define PACKETS_PER_FRAME 60
#define FRAME_SIZE_UINT16 (PACKET_SIZE_UINT16*PACKETS_PER_FRAME)
#define FPS 27;

LeptonThread::LeptonThread() : QThread()
{
	//
	loglevel = 0;

	//
	typeColormap = 3; // 1:colormap_rainbow  /  2:colormap_grayscale  /  3:colormap_ironblack(default)
	selectedColormap = colormap_ironblack;
	selectedColormapSize = get_size_colormap_ironblack();

	//
	typeLepton = 2; // 2:Lepton 2.x  / 3:Lepton 3.x
	myImageWidth = 80;
	myImageHeight = 60;

	//
	spiSpeed = 20 * 1000 * 1000; // SPI bus speed 20MHz

	// min/max value for scaling
	autoRangeMin = true;
	autoRangeMax = true;
	rangeMin = 30000;
	rangeMax = 32000;
}

LeptonThread::~LeptonThread() {
}

void LeptonThread::setLogLevel(uint16_t newLoglevel)
{
	loglevel = newLoglevel;
}

void LeptonThread::useColormap(int newTypeColormap)
{
	switch (newTypeColormap) {
	case 1:
		typeColormap = 1;
		selectedColormap = colormap_rainbow;
		selectedColormapSize = get_size_colormap_rainbow();
		break;
	case 2:
		typeColormap = 2;
		selectedColormap = colormap_grayscale;
		selectedColormapSize = get_size_colormap_grayscale();
		break;
	default:
		typeColormap = 3;
		selectedColormap = colormap_ironblack;
		selectedColormapSize = get_size_colormap_ironblack();
		break;
	}
}

void LeptonThread::useLepton(int newTypeLepton)
{
	switch (newTypeLepton) {
	case 3:
		typeLepton = 3;
		myImageWidth = 160;
		myImageHeight = 120;
		break;
	default:
		typeLepton = 2;
		myImageWidth = 80;
		myImageHeight = 60;
	}
}

void LeptonThread::useSpiSpeedMhz(unsigned int newSpiSpeed)
{
	spiSpeed = newSpiSpeed * 1000 * 1000;
}

void LeptonThread::setAutomaticScalingRange()
{
	autoRangeMin = true;
	autoRangeMax = true;
}

void LeptonThread::useRangeMinValue(uint16_t newMinValue)
{
	autoRangeMin = false;
	rangeMin = newMinValue;
}

void LeptonThread::useRangeMaxValue(uint16_t newMaxValue)
{
	autoRangeMax = false;
	rangeMax = newMaxValue;
}

void LeptonThread::run()
{
    // create the initial image
    myImage = QImage(myImageWidth, myImageHeight, QImage::Format_RGB16);

    const int *colormap = selectedColormap;
    const int colormapSize = selectedColormapSize;

    uint16_t minValue = rangeMin;
    uint16_t maxValue = rangeMax;

    float diff = float(maxValue) - float(minValue);
    if (diff < 1.0f) diff = 1.0f;
    float scale = 255.0f / diff;

    uint16_t n_wrong_segment = 0;
    uint16_t n_zero_value_drop_frame = 0;

    // open spi port
    SpiOpenPort(0, spiSpeed);

    // ---- FFC control ----
    QElapsedTimer ffcTimer;
    ffcTimer.start();

    // Startup FFC
    usleep(200000); // 200 ms
    performFFC();
    ffcTimer.restart();

    while (true) {

        // Periodic FFC
//        if (ffcTimer.elapsed() >= 120000) { // 2 minutes
//            performFFC();
//            ffcTimer.restart();
//        }

        // read data packets from lepton over SPI
        int resets = 0;
        int segmentNumber = -1;

        for (int j = 0; j < PACKETS_PER_FRAME; j++) {
            read(spi_cs0_fd, result + sizeof(uint8_t) * PACKET_SIZE * j, sizeof(uint8_t) * PACKET_SIZE);

            int packetNumber = result[j * PACKET_SIZE + 1];
            if (packetNumber != j) {
                j = -1;
                resets += 1;
                usleep(1000);

                if (resets == 750) {
                    SpiClosePort(0);
                    lepton_reboot();
                    n_wrong_segment = 0;
                    n_zero_value_drop_frame = 0;
                    usleep(750000);
                    SpiOpenPort(0, spiSpeed);

                    performFFC();
                    ffcTimer.restart();
                }
                continue;
            }

            if ((typeLepton == 3) && (packetNumber == 20)) {
                segmentNumber = (result[j * PACKET_SIZE] >> 4) & 0x0f;
                if ((segmentNumber < 1) || (4 < segmentNumber)) {
                    log_message(10, "[ERROR] Wrong segment number " + std::to_string(segmentNumber));
                    break;
                }
            }
        }

        if (resets >= 30) {
            log_message(3, "done reading, resets: " + std::to_string(resets));
        }

        int iSegmentStart = 1;
        int iSegmentStop = 1;

        if (typeLepton == 3) {
            if ((segmentNumber < 1) || (4 < segmentNumber)) {
                n_wrong_segment++;
                if ((n_wrong_segment % 12) == 0) {
                    log_message(5, "[WARNING] Got wrong segment number continuously " + std::to_string(n_wrong_segment) + " times");
                }
                continue;
            }

            if (n_wrong_segment != 0) {
                log_message(8, "[WARNING] Got wrong segment number continuously " + std::to_string(n_wrong_segment) +
                                  " times [RECOVERED] : " + std::to_string(segmentNumber));
                n_wrong_segment = 0;
            }

            memcpy(shelf[segmentNumber - 1], result, sizeof(uint8_t) * PACKET_SIZE * PACKETS_PER_FRAME);
            if (segmentNumber != 4) {
                continue;
            }
            iSegmentStop = 4;
        } else {
            memcpy(shelf[0], result, sizeof(uint8_t) * PACKET_SIZE * PACKETS_PER_FRAME);
            iSegmentStop = 1;
        }

        // auto-range
        if (autoRangeMin || autoRangeMax) {
            if (autoRangeMin) minValue = 65535;
            if (autoRangeMax) maxValue = 0;

            for (int iSegment = iSegmentStart; iSegment <= iSegmentStop; iSegment++) {
                for (int i = 0; i < FRAME_SIZE_UINT16; i++) {

                    if (i % PACKET_SIZE_UINT16 < 2) continue;

                    uint16_t v = (shelf[iSegment - 1][i * 2] << 8) + shelf[iSegment - 1][i * 2 + 1];
                    if (v == 0) continue;

                    if (autoRangeMax && (v > maxValue)) maxValue = v;
                    if (autoRangeMin && (v < minValue)) minValue = v;
                }
            }

            diff = float(maxValue) - float(minValue);
            if (diff < 1.0f) diff = 1.0f;
            scale = 255.0f / diff;
        }

        uint16_t sceneSpread = (maxValue > minValue) ? (maxValue - minValue) : 0;

        int paletteMode = m_thermalPaletteMode;
        bool colorGate = m_colorGate;
        int colorMinSpread = m_colorMinSpread;
        int colorHotPercent = m_colorHotPercent;

        if (colorMinSpread < 0) colorMinSpread = 0;
        if (colorHotPercent < 0) colorHotPercent = 0;
        if (colorHotPercent > 100) colorHotPercent = 100;

        uint16_t hotLimit = minValue + uint16_t((uint32_t(sceneSpread) * uint32_t(colorHotPercent)) / 100u);
        bool suppressAllColor = colorGate && (sceneSpread < uint16_t(colorMinSpread));

        bool drawBoxes = m_thermalBoxes;
        int boxMinArea = m_boxMinArea;
        if (boxMinArea < 1) boxMinArea = 1;

        bool ignoreEnabled = m_ignoreEnabled;
        bool ignorePreview = m_ignorePreview;
        int ignoreX = m_ignoreX;
        int ignoreY = m_ignoreY;
        int ignoreW = m_ignoreW;
        int ignoreH = m_ignoreH;

        if (ignoreX < 0) ignoreX = 0;
        if (ignoreY < 0) ignoreY = 0;
        if (ignoreW < 0) ignoreW = 0;
        if (ignoreH < 0) ignoreH = 0;

        int ignoreX2 = ignoreX + ignoreW;
        int ignoreY2 = ignoreY + ignoreH;

        if (ignoreX2 > myImageWidth) ignoreX2 = myImageWidth;
        if (ignoreY2 > myImageHeight) ignoreY2 = myImageHeight;

        if (ignoreX >= myImageWidth) ignoreEnabled = false;
        if (ignoreY >= myImageHeight) ignoreEnabled = false;
        if (ignoreX2 <= ignoreX) ignoreEnabled = false;
        if (ignoreY2 <= ignoreY) ignoreEnabled = false;

        std::vector<unsigned char> hotMask(myImageWidth * myImageHeight, 0);

        // draw pixels
        int row = 0, column = 0;
        uint16_t valueFrameBuffer = 0;
        QRgb color = 0;

        for (int iSegment = iSegmentStart; iSegment <= iSegmentStop; iSegment++) {
            int ofsRow = 30 * (iSegment - 1);

            for (int i = 0; i < FRAME_SIZE_UINT16; i++) {

                if (i % PACKET_SIZE_UINT16 < 2) continue;

                valueFrameBuffer = (shelf[iSegment - 1][i * 2] << 8) + shelf[iSegment - 1][i * 2 + 1];

                if (valueFrameBuffer == 0) {
                    n_zero_value_drop_frame++;
                    if ((n_zero_value_drop_frame % 12) == 0) {
                        log_message(5, "[WARNING] Found zero-value. Drop the frame continuously " +
                                        std::to_string(n_zero_value_drop_frame) + " times");
                    }
                    break;
                }

                int v = int((float(valueFrameBuffer) - float(minValue)) * scale);
                if (v < 0) v = 0;
                if (v > 255) v = 255;

                int ofs_r = 3 * v + 0; if (colormapSize <= ofs_r) ofs_r = colormapSize - 1;
                int ofs_g = 3 * v + 1; if (colormapSize <= ofs_g) ofs_g = colormapSize - 1;
                int ofs_b = 3 * v + 2; if (colormapSize <= ofs_b) ofs_b = colormapSize - 1;

                QRgb paletteColor = qRgb(colormap[ofs_r], colormap[ofs_g], colormap[ofs_b]);
                QRgb grayColor = qRgb(v, v, v);

                if (typeLepton == 3) {
                    column = (i % PACKET_SIZE_UINT16) - 2 +
                             (myImageWidth / 2) * ((i % (PACKET_SIZE_UINT16 * 2)) / PACKET_SIZE_UINT16);
                    row = i / PACKET_SIZE_UINT16 / 2 + ofsRow;
                } else {
                    column = (i % PACKET_SIZE_UINT16) - 2;
                    row = i / PACKET_SIZE_UINT16;
                }

                if ((0 <= column) && (column < myImageWidth) && (0 <= row) && (row < myImageHeight)) {
                    bool ignoredPixel = false;

                    if (ignoreEnabled) {
                        ignoredPixel =
                            (column >= ignoreX) &&
                            (row >= ignoreY) &&
                            (column < ignoreX2) &&
                            (row < ignoreY2);
                    }

                    bool monoMode = (paletteMode == 2);
                    bool gatedMode = (paletteMode == 1);

                    if (monoMode) {
                        color = grayColor;
                    }
                    else if (ignoredPixel) {
                        color = (m_backgroundMode == "black") ? qRgb(0, 0, 0) : grayColor;
                    }
                    else if (gatedMode) {
                        if (!suppressAllColor && valueFrameBuffer >= hotLimit) {
                            color = paletteColor;
                        } else {
                            color = grayColor;
                        }
                    }
                    else {
                        color = paletteColor;
                    }

                    bool blackBg = (m_backgroundMode == "black");
                    if (blackBg && !monoMode) {
                        int r = qRed(color), g = qGreen(color), b = qBlue(color);
                        if (r == g && g == b) {
                            color = qRgb(0, 0, 0);
                        }
                    }

                    myImage.setPixel(column, row, color);

                    if (drawBoxes && !ignoredPixel && !suppressAllColor && valueFrameBuffer >= hotLimit) {
                        hotMask[row * myImageWidth + column] = 1;
                    }
                }
            }
        }

        if (ignoreEnabled) {
            for (int y = ignoreY; y < ignoreY2; y++) {
                for (int x = ignoreX; x < ignoreX2; x++) {
                    hotMask[y * myImageWidth + x] = 0;
                }
            }
        }

        if (drawBoxes) {
            std::vector<unsigned char> seen(myImageWidth * myImageHeight, 0);
            std::vector<int> stack;
            stack.reserve(myImageWidth * myImageHeight);

            for (int sy = 0; sy < myImageHeight; sy++) {
                for (int sx = 0; sx < myImageWidth; sx++) {
                    int start = sy * myImageWidth + sx;
                    if (!hotMask[start] || seen[start]) continue;

                    int minX = sx;
                    int maxX = sx;
                    int minY = sy;
                    int maxY = sy;
                    int area = 0;

                    stack.clear();
                    stack.push_back(start);
                    seen[start] = 1;

                    while (!stack.empty()) {
                        int idx = stack.back();
                        stack.pop_back();

                        int x = idx % myImageWidth;
                        int y = idx / myImageWidth;

                        area++;
                        if (x < minX) minX = x;
                        if (x > maxX) maxX = x;
                        if (y < minY) minY = y;
                        if (y > maxY) maxY = y;

                        const int dx[4] = {1, -1, 0, 0};
                        const int dy[4] = {0, 0, 1, -1};

                        for (int n = 0; n < 4; n++) {
                            int nx = x + dx[n];
                            int ny = y + dy[n];
                            if (nx < 0 || nx >= myImageWidth || ny < 0 || ny >= myImageHeight) continue;

                            int ni = ny * myImageWidth + nx;
                            if (!hotMask[ni] || seen[ni]) continue;

                            seen[ni] = 1;
                            stack.push_back(ni);
                        }
                    }

                    if (area >= boxMinArea) {
                        if (minX > 0) minX--;
                        if (minY > 0) minY--;
                        if (maxX < myImageWidth - 1) maxX++;
                        if (maxY < myImageHeight - 1) maxY++;

                        bool boxOverlapsIgnore = false;
                        if (ignoreEnabled) {
                            boxOverlapsIgnore =
                                (minX < ignoreX2) &&
                                (maxX >= ignoreX) &&
                                (minY < ignoreY2) &&
                                (maxY >= ignoreY);
                        }
                        if (boxOverlapsIgnore) continue;

                        QRgb boxColor = qRgb(0, 255, 0);

                        for (int x = minX; x <= maxX; x++) {
                            myImage.setPixel(x, minY, boxColor);
                            myImage.setPixel(x, maxY, boxColor);
                        }

                        for (int y = minY; y <= maxY; y++) {
                            myImage.setPixel(minX, y, boxColor);
                            myImage.setPixel(maxX, y, boxColor);
                        }
                    }
                }
            }
        }

        if (ignoreEnabled && ignorePreview) {
            QRgb ignoreBoxColor = qRgb(0, 255, 255);

            for (int x = ignoreX; x < ignoreX2; x++) {
                myImage.setPixel(x, ignoreY, ignoreBoxColor);
                myImage.setPixel(x, ignoreY2 - 1, ignoreBoxColor);
            }

            for (int y = ignoreY; y < ignoreY2; y++) {
                myImage.setPixel(ignoreX, y, ignoreBoxColor);
                myImage.setPixel(ignoreX2 - 1, y, ignoreBoxColor);
            }
        }

        if (n_zero_value_drop_frame != 0) {
            log_message(8, "[WARNING] Found zero-value. Drop the frame continuously " +
                            std::to_string(n_zero_value_drop_frame) + " times [RECOVERED]");
            n_zero_value_drop_frame = 0;
        }

        emit updateImage(myImage);
    }

    SpiClosePort(0);
}


void LeptonThread::performFFC() {
	//perform FFC or comment to disable it
    //	lepton_perform_ffc();
}

void LeptonThread::log_message(uint16_t level, std::string msg)
{
	if (level <= loglevel) {
		std::cerr << msg << std::endl;
	}
}

void LeptonThread::setBackgroundMode(const QString& mode)
{
    m_backgroundMode = mode.toLower();
}

void LeptonThread::setThermalRenderSettings(const QString& palette, bool colorGate, int colorMinSpread, int colorHotPercent)
{
    QString p = palette.toLower();

    if (p == "mono" || p == "bw" || p == "blackwhite") {
        m_thermalPaletteMode = 2;
    } else if (p == "gated" || p == "gated_color" || p == "heat") {
        m_thermalPaletteMode = 1;
    } else {
        m_thermalPaletteMode = 0;
    }

    m_colorGate = colorGate;

    if (colorMinSpread < 0) colorMinSpread = 0;
    m_colorMinSpread = colorMinSpread;

    if (colorHotPercent < 0) colorHotPercent = 0;
    if (colorHotPercent > 100) colorHotPercent = 100;
    m_colorHotPercent = colorHotPercent;
}

void LeptonThread::setThermalBoxSettings(bool boxes, int boxMinArea)
{
    m_thermalBoxes = boxes;

    if (boxMinArea < 1) boxMinArea = 1;
    m_boxMinArea = boxMinArea;
}

void LeptonThread::setThermalIgnoreSettings(bool enabled, bool preview, int x, int y, int w, int h)
{
    m_ignoreEnabled = enabled;
    m_ignorePreview = preview;

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (w < 0) w = 0;
    if (h < 0) h = 0;

    m_ignoreX = x;
    m_ignoreY = y;
    m_ignoreW = w;
    m_ignoreH = h;
}

