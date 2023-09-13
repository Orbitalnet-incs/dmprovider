/***************************************************************************
  qgsdmfile.cpp -  Data provider for DM
  -------------------
          begin                : March 2021
          copyright            : orbitalnet.imc
 ***************************************************************************/

#include "qgsdmfile.h"
#include "qgslogger.h"
#include <qgsfeature.h>

#include <QtGlobal>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDataStream>
#include <QTextStream>
#include <QFileSystemWatcher>
#include <QTextCodec>
#include <QStringList>
#include <QUrl>
#include <QtMath>
#include <QDebug>

QRegExp QgsDmFile::mDataTypeRegexp("^(|dm_(pg|pl|cir|arc|pt|dir|tx))$", Qt::CaseInsensitive);

// 3点を通る円の中心と半径を取得
void calculateCircleCenterAndRadius(const QList<Point2d>& points, Point2d& center, double& radius) {
	double x1 = points.at(0).x();
	double y1 = points.at(0).y();
	double x2 = points.at(1).x();
	double y2 = points.at(1).y();
	double x3 = points.at(2).x();
	double y3 = points.at(2).y();

	double	d = 2.0 * ((y1 - y3) * (x1 - x2) - (y1 - y2) * (x1 - x3));
	double	x = ((y1 - y3) * (qPow(y1, 2.0) - qPow(y2, 2.0) + qPow(x1, 2.0) - qPow(x2, 2.0)) - (y1 - y2) * (qPow(y1, 2.0) - qPow(y3, 2.0) + qPow(x1, 2.0) - qPow(x3, 2.0))) / d;
	double	y = ((x1 - x3) * (qPow(x1, 2.0) - qPow(x2, 2.0) + qPow(y1, 2.0) - qPow(y2, 2.0)) - (x1 - x2) * (qPow(x1, 2.0) - qPow(x3, 2.0) + qPow(y1, 2.0) - qPow(y3, 2.0))) / -d;
	center.setCoord(x, y);

	radius = qSqrt(qPow((x - x1), 2.0) + qPow((y - y1), 2.0));
}

QString extractField(const QByteArray & ba, bool trim, int start, int count)
{
	do
	{
		if (count == 0)
			break;

		if (start >= ba.length())
			break;

		int end = count < 0 ? ba.length() : start + count;
		QString result;
		for (int i = start; i < end; i++)
		{
			result.append(ba.at(i));
		}

		if (trim)
			return result.trimmed();
		else
			return result;

	} while (false);

	return QString();
}

QgsDmFile::QgsDmFile( const QString &url )
  : mDirPath( QString() )
{
	// 属性情報の作成
	mFields.append(QgsField("dmcode", QVariant::Int, QStringLiteral("integer")));
	mFields.append(QgsField("zukei", QVariant::Int, QStringLiteral("integer")));
	mFields.append(QgsField("kandan", QVariant::Int, QStringLiteral("integer")));
	mFields.append(QgsField("teni", QVariant::Int, QStringLiteral("integer")));

	mFieldsForDeirection.append(QgsField("dmcode", QVariant::Int, QStringLiteral("integer")));
	mFieldsForDeirection.append(QgsField("zukei", QVariant::Int, QStringLiteral("integer")));
	mFieldsForDeirection.append(QgsField("kandan", QVariant::Int, QStringLiteral("integer")));
	mFieldsForDeirection.append(QgsField("teni", QVariant::Int, QStringLiteral("integer")));
	mFieldsForDeirection.append(QgsField("vangle", QVariant::Double, QStringLiteral("double")));

	mFieldsForNote.append(QgsField("dmcode", QVariant::Int, QStringLiteral("integer")));
	mFieldsForNote.append(QgsField("teni", QVariant::Int, QStringLiteral("integer")));
	mFieldsForNote.append(QgsField("vangle", QVariant::Double, QStringLiteral("double")));
	mFieldsForNote.append(QgsField("tateyoko", QVariant::Int, QStringLiteral("integer")));
	mFieldsForNote.append(QgsField("size", QVariant::Int, QStringLiteral("integer")));
	mFieldsForNote.append(QgsField("vtext", QVariant::String, QStringLiteral("text")));

	if(!url.isNull()) setFromUrl(url);
}

QgsDmFile::QgsDmFile(const QgsDmFile * other)
{
	if (this == other)
	  return;

	this->mSrid = other->mSrid;
	this->mOverwritingTimes = other->mOverwritingTimes;
	this->mDataType = other->mDataType;

	this->mGeomType = other->mGeomType;

	this->mDefinitionValid = other->mDefinitionValid;
	//this->mIndexes = other->mIndexes;
	this->mMeshes = other->mMeshes;
	//this->mHeaders = other->mHeaders;
	this->mPolygons = other->mPolygons;
	this->mLines = other->mLines;
	this->mCircles = other->mCircles;
	this->mArcs = other->mArcs;
	this->mPoints = other->mPoints;
	this->mDirections = other->mDirections;
	this->mNotes = other->mNotes;
	//this->mAttributes = other->mAttributes;
	//this->mGrids = other->mGrids;
	//this->mTins = other->mTins;

	this->mFields = other->mFields;
	this->mFieldsForDeirection = other->mFieldsForDeirection;
	this->mFieldsForNote = other->mFieldsForNote;

	this->reset();
}

QgsDmFile::~QgsDmFile()
{
}

bool QgsDmFile::read()
{
	// ディレクトリパスの指定がない場合はfalseを返却して終了する
	if (mDirPath.isEmpty()) {
		return false;
	}

	QDir dmDir(mDirPath);
	QStringList dmFiles = dmDir.entryList(QStringList() << "*.dm", QDir::Files);
	if (dmFiles.isEmpty()) {
		return false;
	}

	// データをクリアする
	clear();

	bool success = true;
	QStringListIterator fileItr(dmFiles);
	while (fileItr.hasNext())
	{
		// DMファイルを読み込みDMデータを収集する
		if (readDmFilie(dmDir.filePath(fileItr.next())) == false) {
			success = false;
			break;
		}
	}

	if (success) {
		reset();
	}

	return success;
}

const QgsFields & QgsDmFile::attributeFields() const
{
	if (mDataType == "dm_dir") {
		return mFieldsForDeirection;
	}
	else if (mDataType == "dm_tx") {
		return mFieldsForNote;
	}

	return mFields;
}

// リセットして最初から読み直す（イテレーターで使用）
void QgsDmFile::reset()
{
	mCurrentIndex = -2;
	if (recordCount() > 0) mCurrentIndex = -1;
}

// 次の地物を取得
bool QgsDmFile::nextElement(DmElement& element)
{	
	if (mCurrentIndex < -1) {
		if (read() == false) {
			return false;
		}
	}

	if (mHoldCurrentRecord)
		mHoldCurrentRecord = false;
	else
		mCurrentIndex++;

	if (mDataType == "dm_pg") {
		if (mCurrentIndex >= mPolygons.count())
			return false;
		element = mPolygons.at(mCurrentIndex);
	}
	else if (mDataType == "dm_pl") {
		if (mCurrentIndex >= mLines.count())
			return false;
		element = mLines.at(mCurrentIndex);
	}
	else if (mDataType == "dm_cir") {
		if (mCurrentIndex >= mCircles.count())
			return false;
		element = mCircles.at(mCurrentIndex);
	}
	else if (mDataType == "dm_arc") {
		if (mCurrentIndex >= mArcs.count())
			return false;
		element = mArcs.at(mCurrentIndex);
	}
	else if (mDataType == "dm_pt") {
		if (mCurrentIndex >= mPoints.count())
			return false;
		element = mPoints.at(mCurrentIndex);
	}
	else if (mDataType == "dm_dir") {
		if (mCurrentIndex >= mDirections.count())
			return false;
		element = mDirections.at(mCurrentIndex);
	}
	else if (mDataType == "dm_tx") {
		if (mCurrentIndex >= mNotes.count())
			return false;
		element = mNotes.at(mCurrentIndex);
	}
	else
		return false;

	return true;
}

QVariant QgsDmFile::fetchAttribute(const QString & fieldName, long recordid)
{
	do
	{
		long index = recordid - 1;
		if (index < 0)
			break;

		if (mDataType == "dm_pg") {
			if (index >= mPolygons.count())
				break;
			return mPolygons.at(index).fieldValue(fieldName);
		}
		else if (mDataType == "dm_pl") {
			if (index >= mLines.count())
				break;
			return mLines.at(index).fieldValue(fieldName);
		}
		else if (mDataType == "dm_cir") {
			if (index >= mCircles.count())
				break;
			return mCircles.at(index).fieldValue(fieldName);
		}
		else if (mDataType == "dm_arc") {
			if (index >= mArcs.count())
				break;
			return mArcs.at(index).fieldValue(fieldName);
		}
		else if (mDataType == "dm_pt") {
			if (index >= mPoints.count())
				break;
			return mPoints.at(index).fieldValue(fieldName);
		}
		else if (mDataType == "dm_dir") {
			if (index >= mDirections.count())
				break;
			return mDirections.at(index).fieldValue(fieldName);
		}
		else if (mDataType == "dm_tx") {
			if (index >= mNotes.count())
				break;
			return mNotes.at(index).fieldValue(fieldName);
		}
		
	} while (false);

	return QVariant();
}

bool QgsDmFile::setNextRecordId(long nextRecordId)
{
	if (nextRecordId < 1 || nextRecordId >= recordCount())
		return false;

	mHoldCurrentRecord = true;
	// レコードIDは1～、mCurrentIndexは0～
	mCurrentIndex = (nextRecordId - 1);
	return true;
}

long QgsDmFile::recordCount() const
{
	if (mDataType == "dm_pg")
		return mPolygons.count();
	else if (mDataType == "dm_pl")
		return mLines.count();
	else if (mDataType == "dm_cir")
		return mCircles.count();
	else if (mDataType == "dm_arc")
		return mArcs.count();
	else if (mDataType == "dm_pt")
		return mPoints.count();
	else if (mDataType == "dm_dir")
		return mDirections.count();
	else if (mDataType == "dm_tx")
		return mNotes.count();
	
	return 0;
}

void QgsDmFile::clear()
{
	//mIndexes.clear();
	mMeshes.clear();
	//mHeaders.clear();
	mPolygons.clear();
	mLines.clear();
	mCircles.clear();
	mArcs.clear();
	mPoints.clear();
	mDirections.clear();
	mNotes.clear();
	//mAttributes.clear();
	//mGrids.clear();
	//mTins.clear();

	mCurrentIndex = -1;
}

void QgsDmFile::resetDefinition()
{
	mDirPath.clear();
	mDataType.clear();
	mSrid.clear();
	mOverwritingTimes = -1;
}

bool QgsDmFile::readDmFilie(const QString & filePath)
{
	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		return false;

	QRegExp rxElement("E\\d");

	while (!file.atEnd()) {
		QByteArray line = file.readLine();

		// レコードタイプを確認
		QString recordType = extractField(line, false, 0, 2);

		QByteArrayList rows;
		rows.append(line);

		DmMesh mesh;

		if (recordType == "I ") {
			// インデックス
			// 図郭識別番号レコード数
			int recordCount = extractField(line, true, 37, 2).toInt();
			int index = 0;
			while (!file.atEnd() && index < recordCount)
			{
				rows.append(file.readLine());
				index++;
			}
			//mIndexes.append(rows);

		}
		else if (recordType == "M ") {
			// 図郭
			// 図郭の修正回数を決定する
			// 新規の場合0
			int recModCount = extractField(line, true, 65, 2).toInt();
			int modCount = mOverwritingTimes < 0 ? recModCount : qMin(recModCount, mOverwritingTimes);
			// 図郭レコード(b)を読み込む
			if (!file.atEnd()) rows.append(file.readLine());
			// 図郭レコード(c)を読み込む
			if (!file.atEnd()) rows.append(file.readLine());

			// 図郭レコード(d)(e)(f)を新規+修正回数分読み込む
			int modIndex = 0;
			QList<int> fcountList;
			while (!file.atEnd() && (modIndex < (recModCount + 1)))
			{
				// 図郭レコード(d)を読み込む
				QByteArray temp_row = file.readLine();
				rows.append(temp_row);
				// 撮影コースレコード(図郭レコード(f))数を算出する
				int courseRecordCount = extractField(temp_row, true, 9, 1).toInt();
				if (file.atEnd())
					break;
				fcountList.append(courseRecordCount);
				// 図郭レコード(e)を読み込む
				rows.append(file.readLine());
				// 図郭レコード(f)を読み込む
				int courseRecordIndex = 0;
				while (!file.atEnd() && (courseRecordIndex < courseRecordCount)) {
					rows.append(file.readLine());
					courseRecordIndex++;
				}

				modIndex++;
			}
			mesh = DmMesh(rows, modCount, fcountList);
			mMeshes.append(mesh);
		}
		else if (recordType == "H ") {
			// グループヘッダレコード（レイヤヘッダレコード及び要素グループヘッダレコード）
			//mHeaders.append(line);
		}
		else if (rxElement.exactMatch(recordType)) {
			// 要素レコード

			int recordCount = extractField(line, true, 31, 4).toInt();
			for (int i = 0; (i < recordCount && !file.atEnd()); i++)
			{
				rows.append(file.readLine());
			}

			if (recordType == "E1") {
				// 面
				if (mDataType.isEmpty() || mDataType == "dm_pg") {
					mPolygons.append(DmPolygon(rows, mMeshes.last()));
				}
			}
			else if (recordType == "E2") {
				// 線
				if (mDataType.isEmpty() || mDataType == "dm_pl") {
					mLines.append(DmLine(rows, mMeshes.last()));
				}
			}
			else if (recordType == "E3") {
				// 円
				if (mDataType.isEmpty() || mDataType == "dm_cir") {
					mCircles.append(DmCircle(rows, mMeshes.last()));
				}
			}
			else if (recordType == "E4") {
				// 円弧
				if (mDataType.isEmpty() || mDataType == "dm_arc") {
					mArcs.append(DmArc(rows, mMeshes.last()));
				}
			}
			else if (recordType == "E5") {
				// 点
				if (mDataType.isEmpty() || mDataType == "dm_pt") {
					mPoints.append(DmPoint(rows, mMeshes.last()));
				}
			}
			else if (recordType == "E6") {
				// 方向
				if(mDataType.isEmpty() || mDataType == "dm_dir") {
					mDirections.append(DmDirection(rows, mMeshes.last()));
				}
			}
			else if (recordType == "E7") {
				// 注記
				if(mDataType.isEmpty() || mDataType == "dm_tx") {
					mNotes.append(DmNote(rows, mMeshes.last()));
				}
				
			}
			else if (recordType == "E8") {
				// 属性
				//mAttributes.append(rows);
			}
		}
		else if (recordType == "G ") {
			// グリッド
			int recordCount = extractField(line, true, 26, 4).toInt();
			for (int i = 0; i < recordCount; i++)
			{
				if (file.atEnd()) break;
				rows.append(file.readLine());
			}
			//mGrids.append(rows);
		}
		else if (recordType == "T ") {
			// 不整三角網
			int recordCount = extractField(line, true, 26, 6).toInt();
			for (int i = 0; i < recordCount; i++)
			{
				if (file.atEnd()) break;
				rows.append(file.readLine());
			}
			//mTins.append(rows);
		}
	}
	file.close();

	return true;
}

// Extract the provider definition from the url
bool QgsDmFile::setFromUrl( const QString &url )
{
  QUrl qurl = QUrl::fromEncoded( url.toLatin1() );
  return setFromUrl( qurl );
}

// Extract the provider definition from the url
bool QgsDmFile::setFromUrl( const QUrl &url )
{
  // Close any existing definition
	resetDefinition();

	if (url.hasQueryItem(QStringLiteral("dataType"))) {
		setDataType(url.queryItemValue(QStringLiteral("dataType")).toLower());
	}
	if (url.hasQueryItem(QStringLiteral("srid"))) {
		setSrid(url.queryItemValue(QStringLiteral("srid")).toLower());
	}	
	if (url.hasQueryItem(QStringLiteral("overwritingTimes"))) {
		setOverwritingTimes(url.queryItemValue(QStringLiteral("overwritingTimes")).toInt());
	}
  setDirPath( url.toLocalFile() );

	return true;
}

QUrl QgsDmFile::url()
{
  QUrl url = QUrl::fromLocalFile( mDirPath );

	if (!mSrid.isEmpty()) {
		url.addQueryItem(QStringLiteral("srid"), mSrid);
	}
	if (!mDataType.isEmpty()) {
		url.addQueryItem(QStringLiteral("dataType"), mDataType);
	}

	// 修正回数強制上書き
	if (mOverwritingTimes >= 0) {
		url.addQueryItem(QStringLiteral("overwritingCount"), QString::number(mOverwritingTimes));
	}
  return url;
}

void QgsDmFile::setDirPath( const QString &text)
{
  mDirPath = text;
	mDefinitionValid = (!mDirPath.isEmpty() && mDataTypeRegexp.exactMatch(mDataType));
}

void QgsDmFile::setDataType(const QString & text)
{
	mDataType = text;
	mDefinitionValid = (!mDirPath.isEmpty() && mDataTypeRegexp.exactMatch(mDataType));
}

bool QgsDmFile::isValid()
{
	if (!mDefinitionValid) return false;

	QDir dir(mDirPath);
	if (dir.exists() == false)
		return false;

	return dir.entryList(QStringList() << "*.dm", QDir::Files).count() > 0;
}

QStringList QgsDmFile::fieldNames() const
{
	if (mDataType == "dm_pg" || mDataType == "dm_pl" || mDataType == "dm_cir" || mDataType == "dm_arc" || mDataType == "dm_pt") {
		return mFields.names();
	}
	else if (mDataType == "dm_dir") {
		return mFieldsForDeirection.names();
	}
	else if (mDataType == "dm_tx") {
		return mFieldsForNote.names();
	}

	return QStringList();
}

int QgsDmFile::fieldIndex(const QString & name)
{
	if (mDataType == "dm_pg" || mDataType == "dm_pl" || mDataType == "dm_cir" || mDataType == "dm_arc" || mDataType == "dm_pt") {
		return mFields.indexFromName(name);
	}
	else if (mDataType == "dm_dir") {
		return mFieldsForDeirection.indexFromName(name);
	}
	else if (mDataType == "dm_tx") {
		return mFieldsForNote.indexFromName(name);
	}

	return -1;
}

bool QgsDmFile::test(QMap<QString, bool>& hasMap)
{
	// ディレクトリパスの指定がない場合はfalseを返却して終了する
	if (mDirPath.isEmpty()) {
		return false;
	}

	QDir dmDir(mDirPath);
	QStringList dmFiles = dmDir.entryList(QStringList() << "*.dm", QDir::Files);
	if (dmFiles.isEmpty()) {
		return false;
	}

	int pgCount = 0;
	int plCount = 0;
	int cirCount = 0;
	int arcCount = 0;
	int ptCount = 0;
	int dirCount = 0;
	int txCount = 0;

	bool success = true;
	QStringListIterator fileItr(dmFiles);
	QRegularExpression rxHeaderLine("^H [ \\d]{4}[ \\d]{2}[ \\d]{4}[ \\d]{4}(?<level>[ \\d]{2})(?<all>[ \\d]{5})(?<group>[ \\d]{5})(?<pg>[ \\d]{5})(?<pl>[ \\d]{5})(?<cir>[ \\d]{5})(?<arc>[ \\d]{5})(?<pt>[ \\d]{5})(?<dir>[ \\d]{5})(?<tx>[ \\d]{5})(?<attr>[ \\d]{5})(?<tin>\\d).{14} $");

	while (fileItr.hasNext())
	{
		QFile file(dmDir.filePath(fileItr.next()));
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
			success = false;
			break;
		}

		QTextStream stream(&file);
		stream.setCodec("Shift-JIS");
		QString line;
		while (stream.readLineInto(&line)) {
			QRegularExpressionMatch match = rxHeaderLine.match(line);
			if (match.hasMatch()) {
				pgCount += match.captured("pg").trimmed().toInt();
				plCount += match.captured("pl").trimmed().toInt();
				cirCount += match.captured("cir").trimmed().toInt();
				arcCount += match.captured("arc").trimmed().toInt();
				ptCount += match.captured("pt").trimmed().toInt();
				dirCount += match.captured("dir").trimmed().toInt();
				txCount += match.captured("tx").trimmed().toInt();

				if (pgCount > 0 && plCount > 0 && cirCount > 0 && arcCount > 0 && ptCount > 0 && dirCount > 0 && txCount > 0) {
					break;
				}
			}
		}
		file.close();
	}

	hasMap.insert("dm_pg", pgCount > 0);
	hasMap.insert("dm_pl", plCount > 0);
	hasMap.insert("dm_cir", cirCount > 0);
	hasMap.insert("dm_arc", arcCount > 0);
	hasMap.insert("dm_pt", ptCount > 0);
	hasMap.insert("dm_dir", dirCount > 0);
	hasMap.insert("dm_tx", txCount > 0);

	return (pgCount > 0 || plCount > 0 || cirCount > 0 || arcCount > 0 || ptCount > 0 || dirCount > 0 || txCount > 0);
}


DmMesh::DmMesh(const QByteArrayList & rows, int modifiedCount, const QList<int>& fCountList)
{
	// 地図情報レベル
	mLevel = extractField(rows.at(0), true, 30, 5).toInt();
	// 座標値の単位
	setTani(extractField(rows.at(1), true, 44, 3).toInt());
	
	// 端数単位
	double fractionUnit = (mLevel < 2500) ? 0.001 : 0.01;
	
	// 図郭レコード(d)(e)(f)は新規+修正回数分繰り返しているので最終修正回の図郭レコード(e)レコードから抽出する
	int lastEIndex = 4;
	for (int modifiedIndex = 0; modifiedIndex < modifiedCount; modifiedIndex++)
	{
		lastEIndex += (2 + fCountList.at(modifiedIndex));
	}

	// 左下図郭の端数座標
	double fractionX = extractField(rows.at(lastEIndex), true, 40, 4).toDouble() * fractionUnit;
	double fractionY = extractField(rows.at(lastEIndex), true, 44, 4).toDouble() * fractionUnit;
	// 左下図郭の座標を完成する
	double x = extractField(rows.at(1), true, 7, 7).toDouble() + fractionY;
	double y = extractField(rows.at(1), true, 0, 7).toDouble() + fractionX;
	// 原点を算出
	mOriginPoint.setCoord(x, y);
}

double DmMesh::xCoord(const QString & coordText) const
{
	return mOriginPoint.x() + coordText.toDouble() * mTani;
}

double DmMesh::yCoord(const QString & coordText) const
{
	return mOriginPoint.y() + coordText.toDouble() * mTani;

}

void DmMesh::setTani(int value)
{
	if (value == 1) {
		mTani = 0.001;	// mm単位
	}
	else if (value == 10) {
		mTani = 0.01;		// cm単位
	}
	else {
		// 本来は999のみ
		mTani = 1;			// m単位
	}
}


DmElement::DmElement()
{
}

DmElement::~DmElement()
{
}

QVariant DmElement::fieldValue(const QString & fieldName) const
{
	if (fieldName.compare("dmcode", Qt::CaseInsensitive) == 0)
		return mDmcode;
	if (fieldName.compare("zukei", Qt::CaseInsensitive) == 0)
		return mZukeiKubun;
	if (fieldName.compare("kandan", Qt::CaseInsensitive) == 0)
		return mKandan;
	if (fieldName.compare("teni", Qt::CaseInsensitive) == 0)
		return mTeni; 
	return QVariant();
}

bool DmElement::extractCommonProperty(const QByteArray & header)
{
	bool ok = false;
	do
	{
		mDmcode = extractField(header, true, 2, 4).toInt(&ok);
		if (!ok) break;

		mZukeiKubun = extractField(header, true, 18, 2).toInt(&ok);
		if (!ok) break;

		mKandan = extractField(header, true, 26, 1).toInt(&ok);
		if (!ok) break;

		mTeni = extractField(header, true, 24, 2).toInt(&ok);
		if (!ok) break;

		mDataKubun = extractField(header, true, 20, 1).toInt(&ok);
		if (!ok) break;

		return true;

	} while (false);

	return false;
}

int DmElement::coordDataCount(const QByteArray & header)
{
	return extractField(header, true, 27, 4).toInt();
}

int DmElement::coordRecordCount(const QByteArray & header)
{
	return extractField(header, true, 31, 4).toInt();
}

bool DmElement::read(const QByteArrayList & rows, const DmMesh & mesh, int limitData)
{
	if (extractCommonProperty(rows[0]) == false) {
		QgsDebugMsg(QStringLiteral(u"ヘッダー取込エラー"));
		return false;
	}
	
	int recordCount = coordRecordCount(rows[0]);
	int dataCount = coordDataCount(rows[0]);

	if (dataCount == 0) {
		QgsDebugMsg(QStringLiteral(u"データ数 0"));
		return false;
	}

	if (limitData > 0 && dataCount != limitData) {
		QgsDebugMsg(QString("想定データ数(%1) != 実データ数(%2)").arg(limitData).arg(dataCount));
		return false;
	}

	// 座標データ取込
	return extractCoords(rows, mesh, recordCount, dataCount);
}

bool DmElement::extractCoords(const QByteArrayList & rows, const DmMesh & mesh, int recordCount, int dataCount)
{
	bool success = false;
	if (is2D()) {
		success = extract2dCoords(rows, mesh, recordCount, dataCount);
		if (!success) {
			QgsDebugMsg(QStringLiteral(u"2D座標データの取込に失敗"));
		}
	}
	else if (is3D()) {
		success = extract3dCoords(rows, mesh, recordCount, dataCount);
		if (!success) {
			QgsDebugMsg(QStringLiteral(u"3D座標データの取込に失敗"));
		}
	}
	else {
		QgsDebugMsg(QString("想定外のデータ区分を検出 : %1").arg(mDataKubun));
	}

	return success;
}

bool DmElement::extract2dCoords(const QByteArrayList & rows, const DmMesh & mesh, int recordCount, int dataCount)
{
	int recordIndex = 0;
	int xPos = 0, yPos = 0;
	double x = 0.0, y = 0.0;

	for (int dataIndex = 0; dataIndex < dataCount; dataIndex++)
	{
		if (dataIndex % 6 == 0) {
			recordIndex++;
			if (recordIndex > recordCount)
				break;
		}

		xPos = 0 + ((dataIndex % 6) * 14);
		yPos = 7 + ((dataIndex % 6) * 14);

		x = extractField(rows[recordIndex], true, yPos, 7).toDouble() * mesh.tani();
		y = extractField(rows[recordIndex], true, xPos, 7).toDouble() * mesh.tani();
		mPoints.append(Point2d(mesh.originPoint().x() + x, mesh.originPoint().y() + y));
	}

	return !(mPoints.isEmpty());
}

bool DmElement::extract3dCoords(const QByteArrayList & rows, const DmMesh & mesh, int recordCount, int dataCount)
{
	int recordIndex = 0;
	int xPos = 0, yPos = 0, zPos = 0;
	double x = 0.0, y = 0.0;
	int z = 0;

	for (int dataIndex = 0; dataIndex < dataCount; dataIndex++)
	{
		if (dataIndex % 6 == 0) {
			recordIndex++;
			if (recordIndex >= recordCount)
				break;
		}

		xPos = 0 + ((dataIndex % 4) * 21);
		yPos = 7 + ((dataIndex % 4) * 21);
		zPos = 14 + ((dataIndex % 4) * 21);

		x = extractField(rows[recordIndex], true, yPos, 7).toDouble() * mesh.tani();
		y = extractField(rows[recordIndex], true, xPos, 7).toDouble() * mesh.tani();
		z = extractField(rows[recordIndex], true, zPos, 7).toInt();

		mPoints.append(Point3d(mesh.originPoint().x() + x, mesh.originPoint().y() + y, z));
	}

	return !(mPoints.isEmpty());

}

bool DmElement::is2D()
{
	return mDataKubun == 2;
}

bool DmElement::is3D()
{
	return (mDataKubun == 3 || mDataKubun == 6);
}

DmPolygon::DmPolygon(const QByteArrayList &rows, const DmMesh &mesh)
	: DmElement()
{
	if (read(rows, mesh)) {
		//QgsDebugMsg(QStringLiteral(u"DmPolygon取込成功"));
	}
	else {
		//QgsDebugMsg(QStringLiteral(u"DmPolygon取込失敗"));
	}
}

DmLine::DmLine(const QByteArrayList & rows, const DmMesh & mesh)
	: DmElement()
{
	if (read(rows, mesh)) {
		//QgsDebugMsg(QStringLiteral(u"DmLine取込成功"));
	}
	else {
		//QgsDebugMsg(QStringLiteral(u"DmLine取込失敗"));
	}
}

DmCircle::DmCircle(const QByteArrayList & rows, const DmMesh & mesh)
	: DmElement()
{
	if (read(rows, mesh, 3)) {

		// 取得した3点から中心座標と半径を算出する
		Point2d center;
		double radius = 0.0;
		calculateCircleCenterAndRadius(mPoints, center, radius);

		// ここで一旦mPointsをクリアする
		mPoints.clear();

		// 円周上の10度刻みのポイントを作成する(37点目は始点と同一点)
		for (double deg = 0.0; deg < 361.0; deg=deg+10.0)
		{
			double x = center.x() + radius * qCos(qDegreesToRadians(deg));
			double y = center.y() + radius * qSin(qDegreesToRadians(deg));
			mPoints.append(Point2d(x, y));
		}

		//QgsDebugMsg(QStringLiteral(u"DmCircle取込成功"));
	}
	else {
		//QgsDebugMsg(QStringLiteral(u"DmCircle取込失敗"));
	}
}

DmArc::DmArc(const QByteArrayList & rows, const DmMesh & mesh)
	: DmElement()
{
	if (read(rows, mesh, 3)) {

		// 取得した3点から中心座標と半径を算出する
		Point2d center;
		double radius = 0.0;
		calculateCircleCenterAndRadius(mPoints, center, radius);

		// ここで一旦mPointsを別変数へ移してクリアする
		QList<Point2d> vertexes(mPoints);
		mPoints.clear();

		// 中心点からの各点角度を算出
		double deg1 = qRadiansToDegrees(qAtan2(vertexes[0].y() - center.y(), vertexes[0].x() - center.x()));
		double deg2 = qRadiansToDegrees(qAtan2(vertexes[1].y() - center.y(), vertexes[1].x() - center.x()));
		double deg3 = qRadiansToDegrees(qAtan2(vertexes[2].y() - center.y(), vertexes[2].x() - center.x()));

		QList<double> angles = calculateArcAngles(deg1, deg2, deg3);

		for (double deg : angles) {
			double x = radius * qCos(qDegreesToRadians(deg)) + center.x();
			double y = radius * qSin(qDegreesToRadians(deg)) + center.y();
			mPoints.append(Point2d(x, y));
		}

		//QgsDebugMsg(QStringLiteral(u"DmArc取込成功"));
	}
	else {
		//QgsDebugMsg(QStringLiteral(u"DmArc取込失敗"));
	}
}

QList<double> DmArc::calculateArcAngles(double deg1, double deg2, double deg3)
{
	// deg1 始点角度
	// deg2 経由角度
	// deg3 終点角度

	// deg1を0度に設定
	double t_deg2 = deg2 - deg1;
	double t_deg3 = deg3 - deg1;

	// 0-360表記に整形
	if (t_deg2 < 0)
		t_deg2 += 360;
	else if (t_deg2 > 360)
		t_deg2 -= 360;


	if (t_deg3 < 0)
		t_deg3 += 360;
	else if (t_deg3 > 360)
		t_deg3 -= 360;

	double angle = 0.0;
	int step = 0;
	if (t_deg3 > t_deg2) {
		// clock wise
		angle = t_deg3;
		step = 1;
	}
	else {
		// reverse clock wise
		angle = 360 - t_deg3;
		step = -1;
	}

	QList<double> angles;
	int maxAngle = qCeil(angle);
	for (int i = 0; i < maxAngle; i++)
	{
		// 1度づつ移動
		angles.append(deg1 + double(i * step));
	}
	angles.append(deg3);

	return angles;
}

DmPoint::DmPoint(const QByteArrayList & rows, const DmMesh & mesh)
{
	do
	{
		if (extractCommonProperty(rows[0]) == false) {
			QgsDebugMsg(QStringLiteral(u"ヘッダー取込エラー"));
			break;
		}

		int dataCount = coordDataCount(rows[0]);
		if (dataCount == 0) {
			// 記号
			double x = mesh.xCoord(extractField(rows[0], true, 42, 7));
			double y = mesh.yCoord(extractField(rows[0], true, 35, 7));
			mPoints.append(Point2d(x, y));
		}
		else if (dataCount > 0) {
			QgsDebugMsg(QStringLiteral(u"ポイントデータのデータ数に0以外が設定されている : 標高点群は対象外"));
			break;
		}

		//QgsDebugMsg(QStringLiteral(u"DmPoint取込成功"));
		return;

	} while (false);

	//QgsDebugMsg(QStringLiteral(u"DmPoint取込失敗"));
}

DmDirection::DmDirection(const QByteArrayList & rows, const DmMesh & mesh)
	: DmElement()
{
	if (read(rows, mesh)) {
		mAngle = qRadiansToDegrees(qAtan2(mPoints[1].y() - mPoints[0].y(), mPoints[1].x() - mPoints[0].x()));
		// 方向はPointとするので始点のみにする
		mPoints.removeLast();
		//QgsDebugMsg(QStringLiteral(u"DmDirection取込成功"));
	}
	else {
		//QgsDebugMsg(QStringLiteral(u"DmDirection取込失敗"));
	}
}

QVariant DmDirection::fieldValue(const QString & fieldName) const
{
	if (fieldName.compare("vangle", Qt::CaseInsensitive) == 0)
		return mAngle;

	return DmElement::fieldValue(fieldName);
}

bool DmDirection::is2D()
{
	return (mDataKubun == 0 || mDataKubun == 2);
}

DmNote::DmNote(const QByteArrayList & rows, const DmMesh & mesh)
	: DmElement()
{
	do
	{
		bool ok = false;
		QByteArray header = rows[0];
		mDmcode = extractField(header, true, 2, 4).toInt(&ok);
		if (!ok) break;
	
		mTeni = extractField(header, true, 24, 2).toInt(&ok);
		if (!ok) break;
		
		// 注記区分(漢字か英数字かの区分)
		int noteKubun = extractField(header, true, 23, 1).toInt(&ok);
		if (!ok) break;

		// 文字数
		int dataCount = coordDataCount(header);

		// 座標
		double x = mesh.xCoord(extractField(header, true, 42, 7));
		double y = mesh.yCoord(extractField(header, true, 35, 7));
		mPoints.append(Point2d(x, y));

		// 縦横区分(0:横書き／1:縦書き)
		mTateyoko = extractField(rows[1], true, 0, 1).toInt(&ok);
		if (!ok) break;
		// 傾き
		mAngle = extractField(rows[1], true, 1, 7).toInt(&ok);
		if (!ok) break;
		// 字の大きさ(0.1mm)
		mSize = extractField(rows[1], true, 8, 5).toInt(&ok);
		if (!ok) break;

		int all = 0;
		int mod = 0;
		// 注記区分からレコード数と最終レコードからの文字数を算出する
		switch (noteKubun)
		{
			case 1:	// 全角
				dataCount *= 2;
			case 2:	// 半角
				all = dataCount / 64;
				mod = dataCount % 64;
				break;
		default:
			// 区分しない
			QgsDebugMsg(QString("注記区分に1,2以外が設定されている : %1").arg(noteKubun));
			break;
		}

		// 注記データ

		QTextCodec *codec = QTextCodec::codecForName("Shift-JIS");
		QTextDecoder *decoder = codec->makeDecoder();

		QString note;
		if (dataCount < 64) {
			note.append(decoder->toUnicode(rows[all + 1].mid(20, dataCount)));
		}
		else {
			for (int i = 0; i < all; i++)
			{
				note.append(decoder->toUnicode(rows[i + 1].mid(20, 64)));
			}

			if (mod > 0) {
				note.append(decoder->toUnicode(rows[all + 1].mid(20, mod)));
			}
		}

		mText = note;

		//QgsDebugMsg(QStringLiteral(u"DmNote取込成功"));

		return;

	} while (false);

	//QgsDebugMsg(QStringLiteral(u"DmDirection取込失敗"));

}

QVariant DmNote::fieldValue(const QString & fieldName) const 
{
	if (fieldName.compare("dmcode", Qt::CaseInsensitive) == 0)
		return mDmcode;
	if (fieldName.compare("teni", Qt::CaseInsensitive) == 0)
		return mTeni;
	if (fieldName.compare("vangle", Qt::CaseInsensitive) == 0)
		return mAngle;
	if (fieldName.compare("tateyoko", Qt::CaseInsensitive) == 0)
		return mTateyoko;
	if (fieldName.compare("size", Qt::CaseInsensitive) == 0)
		return mSize;
	if (fieldName.compare("vtext", Qt::CaseInsensitive) == 0)
		return mText;

	return QVariant();
}
