#include <QCoreApplication>
#include <QtDebug>

#include "matfiledump.h"

#if Q_BYTE_ORDER == Q_BIG_ENDIAN
#error MatFileDump class currently only works on little endian machines
#endif

const char MatFileDump::matFileHeaderDoubleLE2D[] =
{
    0x53, 0x74, 0x61, 0x6e, 0x64, 0x61, 0x72, 0x64,
    0x20, 0x2e, 0x6d, 0x61, 0x74, 0x20, 0x68, 0x65,
    0x61, 0x64, 0x65, 0x72, 0x20, 0x74, 0x6f, 0x20,
    0x64, 0x75, 0x6d, 0x70, 0x20, 0x32, 0x44, 0x20,
    0x61, 0x72, 0x72, 0x61, 0x79, 0x20, 0x6f, 0x66,
    0x20, 0x4c, 0x45, 0x20, 0x64, 0x6f, 0x75, 0x62,
    0x6c, 0x65, 0x2e, 0x20, 0x4f, 0x66, 0x66, 0x73,
    0x65, 0x74, 0x73, 0x3a, 0x20, 0x41, 0x30, 0x2d,
    0x43, 0x6f, 0x6c, 0x75, 0x6d, 0x6e, 0x73, 0x2c,
    0x41, 0x34, 0x2d, 0x52, 0x6f, 0x77, 0x73, 0x2c,
    0x43, 0x34, 0x2d, 0x54, 0x6f, 0x74, 0x61, 0x6c,
    0x44, 0x61, 0x74, 0x61, 0x42, 0x79, 0x74, 0x65,
    0x73, 0x20, 0x56, 0x61, 0x6c, 0x75, 0x65, 0x73,
    0x3a, 0x20, 0x49, 0x6e, 0x74, 0x33, 0x32, 0x2c,
    0x4c, 0x45, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x00, 0x01, 0x49, 0x4d,
    0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00,
    0x64, 0x61, 0x74, 0x61, 0x5f, 0x61, 0x72, 0x72,
    0x61, 0x79, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

MatFileDump::MatFileDump(quint32 rows, const QString &name) :
    QFile(name),
    rows(rows),
    valuesWritten(0),
    finalised(false)
{
  if( open(QIODevice::WriteOnly | QIODevice::Truncate) == false )
  {
    qDebug() << "Failed opening" << name;
    QCoreApplication::exit(1);
  }

  if( write(matFileHeaderDoubleLE2D, sizeof(matFileHeaderDoubleLE2D)) != sizeof(matFileHeaderDoubleLE2D) )
    writeFail();
}


void MatFileDump::newFile(quint32 rows, const QString &name)
{
  finaliseAndClose();

  setFileName(name);

  if( open(QIODevice::WriteOnly | QIODevice::Truncate) == false )
  {
    qDebug() << "Failed opening" << name;
    QCoreApplication::exit(1);
  }

  if( write(matFileHeaderDoubleLE2D, sizeof(matFileHeaderDoubleLE2D)) != sizeof(matFileHeaderDoubleLE2D) )
    writeFail();

  this->rows = rows;
  valuesWritten = 0;
  finalised = false;
}


MatFileDump::~MatFileDump()
{
  finaliseAndClose();
}


void MatFileDump::writeDouble(const double &data)
{
  if(write((const char*)&data, 8) != 8)
    writeFail();

  ++valuesWritten;
}


void MatFileDump::operator<<(const double &data)
{
  if(write((const char*)&data, 8) != 8)
    writeFail();

  ++valuesWritten;
}


void MatFileDump::finaliseAndClose()
{
  if(finalised)
    return;

  quint32 columns = valuesWritten / rows;
  quint32 remainder = valuesWritten % rows;

  // add zeros to end of file if the last column hasn't been filled
  if(remainder > 0)
  {
    qDebug() << "Last column of" << rows << "row matrix in" << fileName() << "not filled, adding zeros";
    quint32 zeros = rows - remainder;
    double zero = 0;

    valuesWritten += zeros;
    ++columns;

    while(zeros--)
    {
      if( write((char*)&zero, 8) != 8 )
        writeFail();
    }
  }

  quint32 totalDataBytes = valuesWritten * 8;
  quint32 elementSize    = elementHeaderSize + totalDataBytes;


  // write total element size
  if( seek(elementSizeOffset) == false )
    seekFail(elementSizeOffset);

  if( write((char*)&elementSize, 4) != 4 )
    writeFail();


  // write numbers of columns and rows in matrix
  if( seek(columnsRowsOffset) == false )
    seekFail(columnsRowsOffset);

  if( write((char*)&rows, 4) != 4 )
    writeFail();

  if( write((char*)&columns, 4) != 4 )
    writeFail();


  // write the number of bytes of actual data
  if( seek(totalDataBytesOffset) == false )
    seekFail(totalDataBytesOffset);

  if( write((char*)&totalDataBytes, 4) != 4 )
    writeFail();


  close();
  finalised = true;
}


void MatFileDump::writeFail()
{
  qDebug() << "Failed writing to" << fileName();
  QCoreApplication::exit(1);
}


void MatFileDump::seekFail(qint64 pos)
{
  qDebug() << "Failed seeking to" << pos << "in" << fileName();
  QCoreApplication::exit(1);
}
