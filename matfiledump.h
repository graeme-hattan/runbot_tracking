#ifndef MATFILEDUMP_H
#define MATFILEDUMP_H

#include <QFile>

class MatFileDump : private QFile
{
public:
  MatFileDump() : QFile(), finalised(true) {}
  MatFileDump(quint32 rows, const QString &name="variable_dump.mat");
  ~MatFileDump();

  void newFile(quint32 rows, const QString &name="variable_dump.mat");

  void writeDouble(const double &data);
  void operator<<(const double &data);

  void finaliseAndClose();

private:
  void writeFail();
  void seekFail(qint64 pos);

  quint32 rows;
  quint32 valuesWritten;

  bool finalised;

  static const qint64 elementSizeOffset = 0x84;
  static const qint64 columnsRowsOffset = 0xA0;
  static const qint64 totalDataBytesOffset = 0xC4;

  static const quint32 elementHeaderSize = 64;

  static const char matFileHeaderDoubleLE2D[];
};

#endif // MATFILEDUMP_H
