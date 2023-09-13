/***************************************************************************
      qgsdmfile.h  -  File for DM file
                             -------------------
    begin                : March 2021
    copyright            : orbitalnet.imc
 ***************************************************************************/

#ifndef QGSDMFILE_H
#define QGSDMFILE_H

#include <QStringList>
#include <QRegExp>
#include <QRegularExpression>
#include <QUrl>
#include <QObject>
#include <qgsfields.h>

class QFile;

class Point2d {
public:
	Point2d() {}
	Point2d(double x, double y)
	{
		mX = x;
		mY = y;
	}
	Point2d(const Point2d& other) {
		mX = other.mX;
		mY = other.mY;
	}
	const double x() const { return mX; }
	const double y() const { return mY; }

	void setCoord(double x, double y)
	{
		mX = x;
		mY = y;
	}

protected:
	double mX = 0.0;
	double mY = 0.0;
};

class Point3d : public Point2d {
public:
	Point3d() {}
	Point3d(double x, double y, double z)
		: Point2d(x, y)
	{
		mZ = z;
	}
	double z() const {
		return mZ;	
	}

	void setCoord(double x, double y, double z)
	{
		Point2d::setCoord(x, y);
		mZ = z;
	}

private:
	double mZ = 0;
};


class DmMesh {
public:
	DmMesh() {};
	DmMesh(const QByteArrayList& rows, int modifiedCount, const QList<int>& fCountList);
	const Point2d &originPoint() const { return mOriginPoint;	}
	double tani() const { return mTani; }
	double xCoord(const QString& coordText) const;
	double yCoord(const QString& coordText) const;

private:
	void setTani(int value);

	// メッシュの原点
	Point2d mOriginPoint;
	// 地図情報レベル
	int mLevel = 0;
	// 座標値の単位
	double mTani = 0.0;
};

class DmElement {
public:
	DmElement();
	virtual ~DmElement();

	// 地図分類コード
	virtual int dmcode() const { return mDmcode; }
	// 図形区分
	virtual int zukeiKubun() const { return mZukeiKubun; }
	// 間断区分
	virtual int kandan() const { return mKandan; }
	// 転移区分
	virtual int teni() const { return mTeni; }
	// データ区分
	virtual int dataKubun() const { return mDataKubun; }

	virtual const QList<Point2d>& points() const { return mPoints; }

	virtual QVariant fieldValue(const QString& fieldName) const;

protected:
	bool extractCommonProperty(const QByteArray &header);
	int coordDataCount(const QByteArray &header);
	int coordRecordCount(const QByteArray &header);
	bool read( const QByteArrayList & rows, const DmMesh & mesh, int limitData = 0);
	bool extractCoords(const QByteArrayList & rows, const DmMesh & mesh, int recordCount, int dataCount);
	bool extract2dCoords(const QByteArrayList & rows, const DmMesh & mesh, int recordCount, int dataCount);
	bool extract3dCoords(const QByteArrayList & rows, const DmMesh & mesh, int recordCount, int dataCount);
	virtual bool is2D();
	virtual bool is3D();

	// 地図分類コード
	int mDmcode = 0;
	// 図形区分
	int mZukeiKubun = 0;
	// 間断区分
	int mKandan = 0;
	// 転移区分
	int mTeni = 0;
	// データ区分
	int mDataKubun = 0;

	QList<Point2d> mPoints;
};

class DmPolygon: public DmElement {
public:
	DmPolygon(const QByteArrayList& rows, const DmMesh& mesh);
};

class DmLine : public DmElement {
public:
	DmLine(const QByteArrayList& rows, const DmMesh& mesh);
};

class DmCircle : public DmElement {
public:
	DmCircle(const QByteArrayList& rows, const DmMesh& mesh);
};

class DmArc : public DmElement {
public:
	DmArc(const QByteArrayList& rows, const DmMesh& mesh);

private:
	QList<double> calculateArcAngles(double deg1, double deg2, double deg3);
};

class DmPoint : public DmElement {
public:
	DmPoint(const QByteArrayList& rows, const DmMesh& mesh);
};

class DmDirection : public DmElement {
public:
	DmDirection(const QByteArrayList& rows, const DmMesh& mesh);

	int angle() const { return mAngle; }

	virtual QVariant fieldValue(const QString& fieldName) const;

protected:
	bool is2D() override;

private:
	// 傾き
	double mAngle = 0.0;
};

class DmNote: public DmElement {
public:
	DmNote(const QByteArrayList& rows, const DmMesh& mesh);

	virtual int vangle() const { return mAngle; }
	virtual int tateyoko() const { return mTateyoko; }
	virtual int size() const { return mSize; }
	virtual const QString& vtext() const { return mText; }

	virtual QVariant fieldValue(const QString& fieldName) const;

private:
	// 図形区分
	virtual int zukeiKubun() const { return mZukeiKubun; }
	// 間断区分
	virtual int kandan() const { return mKandan; }

private:
	// 傾き
	double mAngle = 0.0;
	// 縦横区分(0:横書き／1:縦書き)
	int mTateyoko = 0;
	// 字の大きさ(0.1mm)
	int	mSize = 0;
	// 注記データ
	QString mText;
};


/**
\class QgsDmFile
\brief DM file parser extracts records from a QTextStream as a QStringList.
*
*
* The DM parser is used by the QgsDmProvider to parse
* a QTextStream into records of QStringList.  It provides a number of variants
* for parsing each record.  The following options are supported:
* - Basic whitespace parsing.  Each line in the file is treated as a record.
*   Extracts all contiguous sequences of non-whitespace
*   characters.  Leading and trailing whitespace are ignored.
* - Regular expression parsing.  Each line in the file is treated as a record.
*   The string is split into fields based on a regular expression.
* - Character delimited, based on a delimiter character set, a quote character, and
*   an escape character.  The escape treats the next character as a part of a field.
*   Fields may start and end with quote characters, in which case any non-escaped
*   character within the field is treated literally, including end of line characters.
*   The escape character within a string causes the next character to be read literally
*   (this includes new line characters).  If the escape and quote characters are the
*   same, then only quote characters will be escaped (ie to include a quote in a
*   quoted field it is entered as two quotes.  All other characters in quoted fields
*   are treated literally, including newlines.
* - CSV format files - these are a special case of character delimited, in which the
*   delimiter is a comma, and the quote and escape characters are double quotes (")
*
* The delimiters can be encode in and decoded from a QUrl as query items.  The
* items used are:
* - delimiterType, one of plain (delimiter is any of a set of characters),
*   regexp, csv, whitespace
* - delimiter, interpreted according to the type.  For plain characters this is
*   a sequence of characters.  The string \t in the sequence is replaced by a tab.
*   For regexp type delimiters this specifies the reqular expression.
*   The field is ignored for csv and whitespace
* - quoteChar, optional, a single character used for quoting plain fields
* - escapeChar, optional, a single character used for escaping (may be the same as quoteChar)
*/

// Note: this has been implemented as a single class rather than a set of classes based
// on an abstract base class in order to facilitate changing the type of the parser easily,
// e.g., in the provider dialog

class QgsDmFile : public QObject
{

    Q_OBJECT

  public:

    explicit QgsDmFile( const QString &url = QString() );

    explicit QgsDmFile(const QgsDmFile* other);

    ~QgsDmFile() override;

    /**
     * Set the filename
     * \param filename  the name of the file
     */
    void setDirPath( const QString &text );

		const QString& dirPath() const { return mDirPath;  }

		void setDataType(const QString &text);

		const QString& dataType() const { return mDataType; }

		const QString& srid() const { return mSrid; }

		void setSrid(const QString& text) { mSrid = text; }

		int overwritingTimes() const { return mOverwritingTimes; }

		void setOverwritingTimes(int value) { mOverwritingTimes = value; }

    /**
     * Decode the parser settings from a url as a string
     *  \param url  The url from which the delimiter and delimiterType items are read
     */
    bool setFromUrl( const QString &url );

    /**
     * Decode the parser settings from a url
     *  \param url  The url from which the delimiter and delimiterType items are read
     */
    bool setFromUrl( const QUrl &url );

    /**
     * Encode the parser settings into a QUrl
     *  \returns url  The url into which the delimiter and delimiterType items are set
     */
    QUrl url();

    /**
     * Check that provider is valid (filename and definition valid)
     *
     * \returns valid True if the provider is valid
     */
    bool isValid();

		/**
		* Returns the field names read from the header, or default names
		*  field_## if none defined.  Will open and read the head of the file
		*  if required, then reset.  Note that if header record record has
		*  not been read then the field names are empty until records have
		*  been read.  The list may be expanded as the file is read and records
		*  with more fields are loaded.
		*  \returns names  A list of field names in the file
		*/
		QStringList fieldNames() const;

		/**
		* Returns the index of a names field
		*  \param name    The name of the field to find.  This will also accept an
		*                 integer string ("1" = first field).
		*  \returns index  The zero based index of the field name, or -1 if the field
		*                 name does not exist or cannot be inferred
		*/
		int fieldIndex(const QString &name);

		/**
		 * テスト
		 */
		bool test(QMap<QString, bool>& hasMap);

		/**
		 * データ収集
		 */
		bool read();

		const QList<DmPolygon>& polygons() const { return mPolygons; }
		const QList<DmLine>& lines() const { return mLines; }
		const QList<DmCircle>& circles() const { return mCircles; }
		const QList<DmArc>& arcs() const { return mArcs; }
		const QList<DmPoint>& points() const { return mPoints; }
		const QList<DmDirection>& directions() const { return mDirections; }
		const QList<DmNote>& notes() const { return mNotes; }

		const QgsFields& attributeFields() const;

		// リセットして最初から読み直す（イテレーターで使用）
		void reset();

		// 次の地物を取得（イテレーターで使用）
		bool nextElement(DmElement& element);

		QVariant fetchAttribute(const QString& fieldName, long recordid);

		// 現在レコードID取得
		long recordId() const { return mCurrentIndex + 1; }
		bool setNextRecordId(long recordId);

		long recordCount() const;

  private:

		/**
		 * DMファイル読込
		*/
		bool readDmFilie(const QString& filePath);

		void clear();

		void resetDefinition();

		QString mDirPath;
		QString mSrid;
		int mOverwritingTimes = -1;
		QString mDataType;

		QString mGeomType;

		bool mDefinitionValid = false;
		//QByteArrayList mIndexes;
		QList<DmMesh> mMeshes;
		//QByteArrayList mHeaders;
		QList<DmPolygon> mPolygons;
		QList<DmLine> mLines;
		QList<DmCircle> mCircles;
		QList<DmArc> mArcs;
		QList<DmPoint> mPoints;
		QList<DmDirection> mDirections;
		QList<DmNote> mNotes;
		//QByteArrayList mAttributes;
		//QByteArrayList mGrids;
		//QByteArrayList mTins;

		QgsFields mFields;
		QgsFields mFieldsForDeirection;
		QgsFields mFieldsForNote;

		long mCurrentIndex = -1;
		bool mHoldCurrentRecord = false;

		static QRegExp mDataTypeRegexp;
};

#endif
