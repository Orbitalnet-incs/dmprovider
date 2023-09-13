/***************************************************************************
  qgsdmprovider.cpp -  Data provider for DM
  -------------------
          begin                : March 2021
          copyright            : orbitalnet.imc
 ***************************************************************************/

#include "qgsdmprovider.h"

#include <QtGlobal>
#include <QFile>
#include <QFileInfo>
#include <QDataStream>
#include <QTextStream>
#include <QStringList>
#include <QSettings>
#include <QRegExp>
#include <QUrl>
#include <QUrlQuery>

#include "qgsapplication.h"
#include "qgsdataprovider.h"
#include "qgsexpression.h"
#include "qgsfeature.h"
#include "qgsfields.h"
#include "qgsgeometry.h"
#include "qgslogger.h"
#include "qgsmessagelog.h"
#include "qgsmessageoutput.h"
#include "qgsrectangle.h"
#include "qgsspatialindex.h"
#include "qgis.h"
#include "qgsexpressioncontextutils.h"
#include "qgsproviderregistry.h"

#include "qgsdmfeatureiterator.h"
#include "qgsdmfile.h"


const QString QgsDmProvider::TEXT_PROVIDER_KEY = QStringLiteral( "dm" );
const QString QgsDmProvider::TEXT_PROVIDER_DESCRIPTION = QStringLiteral( "DM data provider" );

// If more than this fraction of records are not in a subset then use an index to
// iterate over records rather than simple iterator and filter.

static const int SUBSET_ID_THRESHOLD_FACTOR = 10;

QgsDmProvider::QgsDmProvider( const QString &uri, const ProviderOptions &options )
  : QgsVectorDataProvider( uri, options )
{
	setEncoding("Shift-JIS");

  // Add supported types to enable creating expression fields in field calculator
  setNativeTypes( QList< NativeType >()
                  << QgsVectorDataProvider::NativeType( tr( "Whole number (integer)" ), QStringLiteral( "integer" ), QVariant::Int, 0, 10 )
                  << QgsVectorDataProvider::NativeType( tr( "Whole number (integer - 64 bit)" ), QStringLiteral( "int8" ), QVariant::LongLong )
                  << QgsVectorDataProvider::NativeType( tr( "Decimal number (double)" ), QStringLiteral( "double precision" ), QVariant::Double, -1, -1, -1, -1 )
                  << QgsVectorDataProvider::NativeType( tr( "Text, unlimited length (text)" ), QStringLiteral( "text" ), QVariant::String, -1, -1, -1, -1 )
                );

  QUrl url = QUrl::fromEncoded( uri.toLatin1() );
  mFile = qgis::make_unique< QgsDmFile >();
  mFile->setFromUrl( url );

  QString subset;

	if (url.hasQueryItem(QStringLiteral("dataType"))) {
		mDataType = url.queryItemValue(QStringLiteral("dataType")).toLower();

		if (mDataType == "dm_pg") {
			mWkbType = QgsWkbTypes::Polygon;
			mGeometryType = QgsWkbTypes::PolygonGeometry;
		}
		else if (mDataType == "dm_pl") {
			mWkbType = QgsWkbTypes::LineString;
			mGeometryType = QgsWkbTypes::LineGeometry;
		}
		else if (mDataType == "dm_cir") {
			mWkbType = QgsWkbTypes::LineString;
			mGeometryType = QgsWkbTypes::LineGeometry;
		}
		else if (mDataType == "dm_arc") {
			mWkbType = QgsWkbTypes::LineString;
			mGeometryType = QgsWkbTypes::LineGeometry;
		}
		else if (mDataType == "dm_pt") {
			mWkbType = QgsWkbTypes::Point;
			mGeometryType = QgsWkbTypes::PointGeometry;
		}
		else if (mDataType == "dm_dir") {
			mWkbType = QgsWkbTypes::Point;
			mGeometryType = QgsWkbTypes::PointGeometry;
		}
		else if (mDataType == "dm_tx") {
			mWkbType = QgsWkbTypes::Point;
			mGeometryType = QgsWkbTypes::PointGeometry;
		}
		else {
			mWkbType = QgsWkbTypes::NoGeometry;
			mGeometryType = QgsWkbTypes::NullGeometry;
		}
  }
	else {
		mWkbType = QgsWkbTypes::NoGeometry;
		mGeometryType = QgsWkbTypes::NullGeometry;
	}

	if (url.hasQueryItem(QStringLiteral("srid"))) {
		QString mSrid = QString("POSTGIS:%1").arg(url.queryItemValue(QStringLiteral("srid")));
		mCrs.createFromString(mSrid);
	}
	// プロバイダーがサブセットの効率を改善するためにインデックスを生成するかどうかを決定します。デフォルトはyesです
  if ( url.hasQueryItem( QStringLiteral( "subsetIndex" ) ) )
  {
    mBuildSubsetIndex = ! url.queryItemValue( QStringLiteral( "subsetIndex" ) ).toLower().startsWith( 'n' );
  }
	// プロバイダーが空間インデックスを生成するかどうかを決定します。デフォルトはnoです。
  if ( url.hasQueryItem( QStringLiteral( "spatialIndex" ) ) )
  {
    mBuildSpatialIndex = ! url.queryItemValue( QStringLiteral( "spatialIndex" ) ).toLower().startsWith( 'n' );
  }
	// 表示するレコードのサブセットを識別する式を定義します
  if ( url.hasQueryItem( QStringLiteral( "subset" ) ) )
  {
    // We need to specify FullyDecoded so that %25 is decoded as %
    subset = QUrlQuery( url ).queryItemValue( QStringLiteral( "subset" ), QUrl::FullyDecoded );
    QgsDebugMsg( "subset is: " + subset );
  }
	// クワイエットが含まれている場合、ファイルのロード中に発生したエラーはユーザーダイアログに報告されません（エラーは引き続き出力ログに表示されます）。
  if ( url.hasQueryItem( QStringLiteral( "quiet" ) ) ) mShowInvalidLines = false;

  // Do an initial scan of the file to determine field names, types,
  // geometry type (for Wkt), extents, etc.  Parameter value subset.isEmpty()
  // avoid redundant building indexes if we will be building a subset string,
  // in which case indexes will be rebuilt.

  scanFile( subset.isEmpty() );

  if ( ! subset.isEmpty() )
  {
    setSubsetString( subset );
  }
}

QgsDmProvider::~QgsDmProvider() = default;

QgsAbstractFeatureSource *QgsDmProvider::featureSource() const
{
  return new QgsDmFeatureSource( this );
}

void QgsDmProvider::resetCachedSubset() const
{
  mCachedSubsetString = QString();
  mCachedUseSubsetIndex = false;
  mCachedUseSpatialIndex = false;
}

void QgsDmProvider::resetIndexes() const
{
  resetCachedSubset();
  mUseSubsetIndex = false;
  mUseSpatialIndex = false;

  mSubsetIndex.clear();
  if ( mBuildSpatialIndex)
    mSpatialIndex = qgis::make_unique< QgsSpatialIndex >();
}

// buildIndexes parameter of scanFile is set to false when we know we will be
// immediately rescanning (when the file is loaded and then the subset expression is
// set)

void QgsDmProvider::scanFile( bool buildIndexes )
{
  QStringList messages;

  // assume the layer is invalid until proven otherwise

  mLayerValid = false;
  mValid = false;
	attributeFields.clear();

  clearInvalidLines();

  // Initiallize indexes
  resetIndexes();
  bool buildSpatialIndex = buildIndexes && nullptr != mSpatialIndex;

  // No point building a subset index if there is no geometry, as all
  // records will be included.

  bool buildSubsetIndex = buildIndexes && mBuildSubsetIndex;

  if ( ! mFile->isValid() || mDataType.isEmpty() )
  {
    messages.append( tr( "DM Files cannot be read or parameters are not valid" ) );
    reportErrors( messages );
    QgsDebugMsg( QStringLiteral( "DM source invalid - directory or parameters" ) );
    return;
  }

	// ファイル全体をスキャンして決定します
	// 1）フィールドの数（これはQgsDmFile mFileによって処理されます）
	// 2）有効な地物の数。 有効な地物の選択は、QgsDmFeatureIteratorのコードと一致する必要があることに注意してください
	// 3）レイヤーの幾何学的範囲
	// 4）各フィールドのタイプ
	// 
	// また、サブセットと空間インデックスを作成します。

	if (mFile->read() == false) {
		messages.append(tr("DM Files cannot be read"));
		reportErrors(messages);
		QgsDebugMsg(QStringLiteral("DM source invalid - files failed"));
		return;
	}

	attributeFields = mFile->attributeFields();

	mNumberFeatures = 0;
	mExtent = QgsRectangle();
	bool foundFirstGeometry = false;


	int fid = 1;
	if (mDataType == "dm_pg") {
		const QList<DmPolygon>& dmpolygons = mFile->polygons();

		mNumberFeatures = dmpolygons.count();


		QListIterator<DmPolygon> itr(dmpolygons);
		while (itr.hasNext()) {
			DmPolygon dmpolygon = itr.next();		
			QgsGeometry geom;
			createGeometry(mGeometryType, dmpolygon.points(), geom);
			appendExtent(geom, foundFirstGeometry);

			if (buildSpatialIndex) {
				addFeaturemToSpatialIndex(fid++, geom);
			}
		}
	}
	else if (mDataType == "dm_pl") {
		const QList<DmLine>& dmlines = mFile->lines();

		mNumberFeatures = dmlines.count();

		QListIterator<DmLine> itr(dmlines);
		while (itr.hasNext()) {
			DmLine dmline = itr.next();
			QgsGeometry geom;
			createGeometry(mGeometryType, dmline.points(), geom);
			appendExtent(geom, foundFirstGeometry);

			if (buildSpatialIndex) {
				addFeaturemToSpatialIndex(fid++, geom);
			}
		}
	}
	else if (mDataType == "dm_cir") {
		const QList<DmCircle>& dmcircles = mFile->circles();

		mNumberFeatures = dmcircles.count();

		QListIterator<DmCircle> itr(dmcircles);
		while (itr.hasNext()) {
			DmCircle dmcircle = itr.next();
			QgsGeometry geom;
			createGeometry(mGeometryType, dmcircle.points(), geom);
			appendExtent(geom, foundFirstGeometry);

			if (buildSpatialIndex) {
				addFeaturemToSpatialIndex(fid++, geom);
			}
		}
	}
	else if (mDataType == "dm_arc") {
		const QList<DmArc>& dmarcs = mFile->arcs();

		mNumberFeatures = dmarcs.count();

		QListIterator<DmArc> itr(dmarcs);
		while (itr.hasNext()) {
			DmArc dmarc = itr.next();
			QgsGeometry geom;
			createGeometry(mGeometryType, dmarc.points(), geom);
			appendExtent(geom, foundFirstGeometry);

			if (buildSpatialIndex) {
				addFeaturemToSpatialIndex(fid++, geom);
			}
		}
	}
	else if (mDataType == "dm_pt") {
		const QList<DmPoint>& dmpoints = mFile->points();

		mNumberFeatures = dmpoints.count();

		QListIterator<DmPoint> itr(dmpoints);
		while (itr.hasNext()) {
			DmPoint dmpoint = itr.next();
			QgsGeometry geom;
			createGeometry(mGeometryType, dmpoint.points(), geom);
			appendExtent(geom, foundFirstGeometry);

			if (buildSpatialIndex) {
				addFeaturemToSpatialIndex(fid++, geom);
			}
		}
	}
	else if (mDataType == "dm_dir") {
		const QList<DmDirection>& dmdirs = mFile->directions();

		mNumberFeatures = dmdirs.count();

		QListIterator<DmDirection> itr(dmdirs);
		while (itr.hasNext()) {
			DmDirection dmdir = itr.next();
			QgsGeometry geom;
			createGeometry(mGeometryType, dmdir.points(), geom);
			appendExtent(geom, foundFirstGeometry);

			if (buildSpatialIndex) {
				addFeaturemToSpatialIndex(fid++, geom);
			}
		}
	}
	else if (mDataType == "dm_tx") {
		const QList<DmNote>& dmnotes = mFile->notes();

		mNumberFeatures = dmnotes.count();

		QListIterator<DmNote> itr(dmnotes);
		while (itr.hasNext()) {
			DmNote dmnote = itr.next();
			QgsGeometry geom;
			createGeometry(mGeometryType, dmnote.points(), geom);
			appendExtent(geom, foundFirstGeometry);

			if (buildSpatialIndex) {
				QgsFeature f;
				f.setId(fid++);
				f.setGeometry(geom);
				mSpatialIndex->addFeature(f);
			}
		}
	}

  // Decide whether to use subset ids to index records rather than simple iteration through all
  // If more than 10% of records are being skipped, then use index.  (Not based on any experimentation,
  // could do with some analysis?)

  if ( buildSubsetIndex )
  {
    long recordCount = mNumberFeatures;
    recordCount -= recordCount / SUBSET_ID_THRESHOLD_FACTOR;
    mUseSubsetIndex = mSubsetIndex.size() < recordCount;
    if ( ! mUseSubsetIndex )
      mSubsetIndex = QList<quintptr>();
  }

  mUseSpatialIndex = buildSpatialIndex;
  mLayerValid = true;
}

// rescanFile.  Called if something has changed file definition, such as
// selecting a subset, the file has been changed by another program, etc

void QgsDmProvider::rescanFile() const
{
  resetIndexes();

  bool buildSpatialIndex = nullptr != mSpatialIndex;
  bool buildSubsetIndex = mBuildSubsetIndex && ( mSubsetExpression || !mDataType.isEmpty() );

  // In case file has been rewritten check that it is still valid

  mValid = mLayerValid && mFile->isValid();
  if ( ! mValid )
    return;

  // Open the file and get number of rows, etc. We assume that the
  // file has a header row and process accordingly. Caller should make
  // sure that the delimited file is properly formed.

  QStringList messages;


  // Scan through the features in the file

  mSubsetIndex.clear();
  mUseSubsetIndex = false;
  QgsFeatureIterator fi = getFeatures( QgsFeatureRequest() );
  mNumberFeatures = 0;
  mExtent = QgsRectangle();
  QgsFeature f;
  bool foundFirstGeometry = false;
  while ( fi.nextFeature( f ) )
  {
    if ( mGeometryType != QgsWkbTypes::NullGeometry && f.hasGeometry() )
    {
      if ( !foundFirstGeometry )
      {
        mExtent = f.geometry().boundingBox();
        foundFirstGeometry = true;
      }
      else
      {
        QgsRectangle bbox( f.geometry().boundingBox() );
        mExtent.combineExtentWith( bbox );
      }
      if ( buildSpatialIndex )
        mSpatialIndex->addFeature( f );
    }
    if ( buildSubsetIndex )
      mSubsetIndex.append( ( quintptr ) f.id() );
    mNumberFeatures++;
  }
  if ( buildSubsetIndex )
  {
    long recordCount = mFile->recordCount();
    recordCount -= recordCount / SUBSET_ID_THRESHOLD_FACTOR;
    mUseSubsetIndex = recordCount < mSubsetIndex.size();
    if ( ! mUseSubsetIndex )
      mSubsetIndex.clear();
  }

  mUseSpatialIndex = buildSpatialIndex;
}

QString QgsDmProvider::storageType() const
{
  return QStringLiteral( "DM files" );
}

QgsFeatureIterator QgsDmProvider::getFeatures( const QgsFeatureRequest &request ) const
{
  return QgsFeatureIterator( new QgsDmFeatureIterator( new QgsDmFeatureSource( this ), true, request ) );
}

void QgsDmProvider::clearInvalidLines() const
{
  mInvalidLines.clear();
  mNExtraInvalidLines = 0;
}

bool QgsDmProvider::recordIsEmpty( QStringList &record )
{
  const auto constRecord = record;
  for ( const QString &s : constRecord )
  {
    if ( ! s.isEmpty() )
      return false;
  }
  return true;
}

void QgsDmProvider::recordInvalidLine( const QString &message )
{
  if ( mInvalidLines.size() < mMaxInvalidLines )
  {
    mInvalidLines.append( message.arg( mFile->recordId() ) );
  }
  else
  {
    mNExtraInvalidLines++;
  }
}

void QgsDmProvider::reportErrors( const QStringList &messages, bool showDialog ) const
{
  if ( !mInvalidLines.isEmpty() || ! messages.isEmpty() )
  {
    QString tag( QStringLiteral( "Dm" ) );
    QgsMessageLog::logMessage( tr( "Errors in DM Directory %1" ).arg( mFile->dirPath() ), tag );
    const auto constMessages = messages;
    for ( const QString &message : constMessages )
    {
      QgsMessageLog::logMessage( message, tag );
    }
    if ( ! mInvalidLines.isEmpty() )
    {
      QgsMessageLog::logMessage( tr( "The following lines were not loaded into QGIS due to errors:" ), tag );
      for ( int i = 0; i < mInvalidLines.size(); ++i )
        QgsMessageLog::logMessage( mInvalidLines.at( i ), tag );
      if ( mNExtraInvalidLines > 0 )
        QgsMessageLog::logMessage( tr( "There are %1 additional errors in the file" ).arg( mNExtraInvalidLines ), tag );
    }

    // Display errors in a dialog...
    if ( mShowInvalidLines && showDialog )
    {
      QgsMessageOutput *output = QgsMessageOutput::createMessageOutput();
      output->setTitle( tr( "DM file errors" ) );
      output->setMessage( tr( "Errors in directory %1" ).arg( mFile->dirPath() ), QgsMessageOutput::MessageText );
      const auto constMessages = messages;
      for ( const QString &message : constMessages )
      {
        output->appendMessage( message );
      }
      if ( ! mInvalidLines.isEmpty() )
      {
        output->appendMessage( tr( "The following lines were not loaded into QGIS due to errors:" ) );
        for ( int i = 0; i < mInvalidLines.size(); ++i )
          output->appendMessage( mInvalidLines.at( i ) );
        if ( mNExtraInvalidLines > 0 )
          output->appendMessage( tr( "There are %1 additional errors in the file" ).arg( mNExtraInvalidLines ) );
      }
      output->showMessage();
    }

    // We no longer need these lines.
    clearInvalidLines();
  }
}

bool QgsDmProvider::setSubsetString( const QString &subset, bool updateFeatureCount )
{
  QString nonNullSubset = subset.isNull() ? QString() : subset;

  // If not changing string, then all OK, nothing to do
  if ( nonNullSubset == mSubsetString )
    return true;

  bool valid = true;

  // If there is a new subset string then encode it..

  std::unique_ptr< QgsExpression > expression;
  if ( ! nonNullSubset.isEmpty() )
  {

    expression = qgis::make_unique< QgsExpression >( nonNullSubset );
    QString error;
    if ( expression->hasParserError() )
    {
      error = expression->parserErrorString();
    }
    else
    {
      QgsExpressionContext context = QgsExpressionContextUtils::createFeatureBasedContext( QgsFeature(), fields() );
      expression->prepare( &context );
      if ( expression->hasEvalError() )
      {
        error = expression->evalErrorString();
      }
    }
    if ( ! error.isEmpty() )
    {
      valid = false;
      expression.reset();
      QString tag( QStringLiteral( "Dm" ) );
      QgsMessageLog::logMessage( tr( "Invalid subset string %1 for %2" ).arg( nonNullSubset, mFile->dirPath() ), tag );
    }
  }

  // if the expression is valid, then reset the subset string and data source Uri

  if ( valid )
  {
    QString previousSubset = mSubsetString;
    mSubsetString = nonNullSubset;
    mSubsetExpression = std::move( expression );

    // Update the feature count and extents if requested

    // Usage of updateFeatureCount is a bit painful, basically expect that it
    // will only be false for a temporary subset, and the original subset
    // will be replaced before an update is requeired.
    //
    // It appears to be false for a temporary subset string, which is used to
    // get some data, and then immediately reset.  No point scanning file and
    // resetting subset index for this.  On the other hand, we don't want to
    // lose indexes in this instance, or have to rescan file.  So we cache
    // the settings until a real subset is required.

    if ( updateFeatureCount )
    {
      if ( ! mCachedSubsetString.isNull() && mSubsetString == mCachedSubsetString )
      {
        QgsDebugMsg( QStringLiteral( "Dm: Resetting cached subset string %1" ).arg( mSubsetString ) );
        mUseSpatialIndex = mCachedUseSpatialIndex;
        mUseSubsetIndex = mCachedUseSubsetIndex;
        resetCachedSubset();
      }
      else
      {
        QgsDebugMsg( QStringLiteral( "Dm: Setting new subset string %1" ).arg( mSubsetString ) );
        // Reset the subset index
        rescanFile();
        // Encode the subset string into the data source URI.
        setUriParameter( QStringLiteral( "subset" ), nonNullSubset );
      }
    }
    else
    {
      // If not already using temporary subset, then cache the current subset
      QgsDebugMsg( QStringLiteral( "Dm: Setting temporary subset string %1" ).arg( mSubsetString ) );
      if ( mCachedSubsetString.isNull() )
      {
        QgsDebugMsg( QStringLiteral( "Dm: Caching previous subset %1" ).arg( previousSubset ) );
        mCachedSubsetString = previousSubset;
        mCachedUseSpatialIndex = mUseSpatialIndex;
        mCachedUseSubsetIndex = mUseSubsetIndex;
      }
      mUseSubsetIndex = false;
      mUseSpatialIndex = false;
    }
  }

  clearMinMaxCache();
  emit dataChanged();
  return valid;
}

void QgsDmProvider::setUriParameter( const QString &parameter, const QString &value )
{
  QUrl url = QUrl::fromEncoded( dataSourceUri().toLatin1() );
  if ( url.hasQueryItem( parameter ) )
    url.removeAllQueryItems( parameter );
  if ( ! value.isEmpty() )
    url.addQueryItem( parameter, value );
  setDataSourceUri( QString::fromLatin1( url.toEncoded() ) );
}

QgsPolylineXY QgsDmProvider::createPolyline(const QList<Point2d>& vertexes, bool forPolygon)
{
	QgsPolylineXY polyline;
	QListIterator<Point2d> itrPoint(vertexes);
	while (itrPoint.hasNext())
	{
		Point2d vertex = itrPoint.next();
		polyline.append(QgsPointXY(vertex.x(), vertex.y()));
	}

	if (forPolygon) {
		polyline.append(QgsPointXY(vertexes.first().x(), vertexes.first().y()));
	}

	return polyline;
}

void QgsDmProvider::appendExtent(const QgsGeometry & geom, bool& foundFirstGeometry)
{
	if (!foundFirstGeometry) {
		mExtent = geom.boundingBox();
		foundFirstGeometry = true;
	}
	else {
		mExtent.combineExtentWith(geom.boundingBox());
	}
}

void QgsDmProvider::addFeaturemToSpatialIndex(int fid, const QgsGeometry& geom)
{
	QgsFeature f;
	f.setId(fid);
	f.setGeometry(geom);
	mSpatialIndex->addFeature(f);
}

bool QgsDmProvider::createGeometry(QgsWkbTypes::GeometryType type, const QList<Point2d>& points, QgsGeometry & geom)
{
	if (type == QgsWkbTypes::PointGeometry) {
		QgsPointXY point(points.first().x(), points.first().y());
		geom = QgsGeometry::fromPointXY(point);
	}
	else if (type == QgsWkbTypes::LineGeometry) {
		QgsPolylineXY polyline = createPolyline(points);
		geom = QgsGeometry::fromPolylineXY(polyline);
	}
	else if (type == QgsWkbTypes::PolygonGeometry) {
		QgsPolylineXY polyline = createPolyline(points, true);
		QgsPolygonXY polygon;
		polygon.append(polyline);
		geom = QgsGeometry::fromPolygonXY(polygon);
	}
	else
		return false;

	return geom.isGeosValid();
}

QgsRectangle QgsDmProvider::extent() const
{
  return mExtent;
}

QgsWkbTypes::Type QgsDmProvider::wkbType() const
{
  return mWkbType;
}

long QgsDmProvider::featureCount() const
{
  return mNumberFeatures;
}

QgsFields QgsDmProvider::fields() const
{
  return attributeFields;
}

bool QgsDmProvider::isValid() const
{
  return mLayerValid;
}

QgsVectorDataProvider::Capabilities QgsDmProvider::capabilities() const
{
  return SelectAtId | CreateSpatialIndex | CircularGeometries;
}

bool QgsDmProvider::createSpatialIndex()
{
	if (mBuildSpatialIndex)
		return true; // Already built

	mBuildSpatialIndex = true;
	setUriParameter(QStringLiteral("spatialIndex"), QStringLiteral("yes"));
	rescanFile();
	return true;
}

QgsFeatureSource::SpatialIndexPresence QgsDmProvider::hasSpatialIndex() const
{
	return mSpatialIndex ? QgsFeatureSource::SpatialIndexPresent : QgsFeatureSource::SpatialIndexNotPresent;
}

QgsCoordinateReferenceSystem QgsDmProvider::crs() const
{
  return mCrs;
}

QString  QgsDmProvider::name() const
{
  return TEXT_PROVIDER_KEY;
}

QString  QgsDmProvider::description() const
{
  return TEXT_PROVIDER_DESCRIPTION;
}

QVariantMap QgsDmProviderMetadata::decodeUri( const QString &uri )
{
  QVariantMap components;
  components.insert( QStringLiteral( "path" ), QUrl( uri ).toLocalFile() );
  return components;
}

QgsDataProvider *QgsDmProviderMetadata::createProvider( const QString &uri, const QgsDataProvider::ProviderOptions &options )
{
  return new QgsDmProvider( uri, options );
}


QgsDmProviderMetadata::QgsDmProviderMetadata():
  QgsProviderMetadata( QgsDmProvider::TEXT_PROVIDER_KEY, QgsDmProvider::TEXT_PROVIDER_DESCRIPTION )
{
}

QGISEXTERN QgsProviderMetadata *providerMetadataFactory()
{
  return new QgsDmProviderMetadata();
}
