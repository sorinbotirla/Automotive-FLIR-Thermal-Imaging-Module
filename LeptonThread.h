#ifndef TEXTTHREAD
#define TEXTTHREAD

#include <ctime>
#include <stdint.h>

#include <QThread>
#include <QtCore>
#include <QPixmap>
#include <QImage>
#include <QString>

#define PACKET_SIZE 164
#define PACKET_SIZE_UINT16 (PACKET_SIZE/2)
#define PACKETS_PER_FRAME 60
#define FRAME_SIZE_UINT16 (PACKET_SIZE_UINT16*PACKETS_PER_FRAME)

class LeptonThread : public QThread
{
  Q_OBJECT;

public:
  LeptonThread();
  ~LeptonThread();

  void setLogLevel(uint16_t);
  void useColormap(int);
  void useLepton(int);
  void useSpiSpeedMhz(unsigned int);
  void setAutomaticScalingRange();
  void useRangeMinValue(uint16_t);
  void useRangeMaxValue(uint16_t);
  void setBackgroundMode(const QString& mode);
  void setThermalRenderSettings(const QString& palette, bool colorGate, int colorMinSpread, int colorHotPercent);
  void setThermalBoxSettings(bool boxes, int boxMinArea);
  void setThermalIgnoreSettings(bool enabled, bool preview, int x, int y, int w, int h);
  void run();

public slots:
  void performFFC();

signals:
  void updateText(QString);
  void updateImage(QImage);

private:

  void log_message(uint16_t, std::string);
  uint16_t loglevel;
  int typeColormap;
  const int *selectedColormap;
  int selectedColormapSize;
  int typeLepton;
  unsigned int spiSpeed;
  bool autoRangeMin;
  bool autoRangeMax;
  uint16_t rangeMin;
  uint16_t rangeMax;
  int myImageWidth;
  int myImageHeight;
  QImage myImage;
  QString m_backgroundMode = "black";

  // 0=color, 1=gated color, 2=mono
  int m_thermalPaletteMode = 1;
  bool m_colorGate = true;
  int m_colorMinSpread = 700;
  int m_colorHotPercent = 70;
  bool m_thermalBoxes = false;
  int m_boxMinArea = 20;

  bool m_ignoreEnabled = false;
  bool m_ignorePreview = false;
  int m_ignoreX = 65;
  int m_ignoreY = 42;
  int m_ignoreW = 15;
  int m_ignoreH = 18;

  uint8_t result[PACKET_SIZE*PACKETS_PER_FRAME];
  uint8_t shelf[4][PACKET_SIZE*PACKETS_PER_FRAME];
  uint16_t *frameBuffer;

};

#endif
