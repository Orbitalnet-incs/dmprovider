/***************************************************************************
    qgsdmfeatureiterator.cpp
    ---------------------
    begin                : March 2021
    copyright            : orbitalnet.imc
 ***************************************************************************/
#include "qgsdmfeatureiterator.h"
#include "qgsdmprovider.h"
#include "qgsdmfile.h"

#include "qgsexpression.h"
#include "qgsgeometry.h"
#include "qgslogger.h"
#include "qgsmessagelog.h"
#include "qgsproject.h"
#include "qgsspatialindex.h"
#include "qgsexception.h"
#include "qgsexpressioncontextutils.h"

#include <QtAlgorithms>
#include <QTextStream>

QgsDmFeatureIterator::QgsDmFeatureIterator( QgsDmFeatureSource *source, bool ownSource, const QgsFeatureRequest &request )
  : QgsAbstractFeatureIteratorFromSource<QgsDmFeatureSource>( source, ownSource, request )
  , mTestSubset( mSource->mSubsetExpression )
{
  // requet引数に基づきフィルターモードを決定する
  QgsDebugMsg( QStringLiteral( "Setting up QgsDmIterator" ) );

  if ( mRequest.destinationCrs().isValid() && mRequest.destinationCrs() != mSource->mCrs )
  {
        // requestのCRSとソースのCRSが違う場合は変換オブジェクトを作成する
    mTransform = QgsCoordinateTransform( mSource->mCrs, mRequest.destinationCrs(), mRequest.transformContext() );
  }
  try
  {
        // requestの範囲をソースのCRSに合わせて範囲に変換
    mFilterRect = filterRectToSourceCrs( mTransform );
  }
  catch ( QgsCsException & )
  {
    // 投影変換失敗
    close();
    return;
  }

  if ( !mFilterRect.isNull() )
  {
        // 範囲の指定あり
    QgsDebugMsg( QStringLiteral( "Configuring for rectangle select" ) );
    mTestGeometry = true;
    mTestGeometryExact = mRequest.flags() & QgsFeatureRequest::ExactIntersect;

    if ( ! mFilterRect.intersects( mSource->mExtent ) && !mTestSubset )
    {
            // サブセットの指定がなく、範囲がレイヤーの範囲に交差しない場合は地物IDリストモードに設定
      QgsDebugMsg( QStringLiteral( "Rectangle outside layer extents - no features to return" ) );
      mMode = FeatureIds;
    }
    else if ( mFilterRect.contains( mSource->mExtent ) && !mTestSubset )
    {
            // 要求範囲にレイヤー全体の範囲が含まれる場合はファイルスキャンモードのままとする
            // ジオメトリのテストも不要
      QgsDebugMsg( QStringLiteral( "Rectangle contains layer extents - bypass spatial filter" ) );
      mTestGeometry = false;
    }

        // 空間インデックスがある場合は、それを使用します。 
        // 空間インデックスはすでにサブセットを説明しています。 
        // また、正確な交差を行わない限り、ジオメトリをテストする必要がないことも意味します
    else if ( mSource->mUseSpatialIndex )
    {
      mFeatureIds = mSource->mSpatialIndex->intersects( mFilterRect );
      // 効率的に順次検索するためにソート
      std::sort( mFeatureIds.begin(), mFeatureIds.end() );
      QgsDebugMsg( QStringLiteral( "Layer has spatial index - selected %1 features from index" ).arg( mFeatureIds.size() ) );
      mMode = FeatureIds;
      mTestSubset = false;
      mTestGeometry = mTestGeometryExact;
    }
  }

  if ( request.filterType() == QgsFeatureRequest::FilterFid )
  {
    QgsDebugMsg( QStringLiteral( "Configuring for returning single id" ) );
    if ( mFilterRect.isNull() || mFeatureIds.contains( request.filterFid() ) )
    {
      mFeatureIds = QList<QgsFeatureId>() << request.filterFid();
    }
    mMode = FeatureIds;
    mTestSubset = false;
  }

  else
        // ジオメトリとテストジオメトリがある場合は、オプションを評価する。
        // ジオメトリがない場合、すべてのレコードがジオメトリフィルタを通過する。
        // CC：2013-05-09ジオメトリのフィルタリングとジオメトリの要求なしの意図された関係についてよくわかりませんか？ 
        // ジオメトリを要求しない場合、空間フィルタを無視するという現在のロジックを保持しています。

    if ( mMode == FileScan && mSource->mUseSubsetIndex )
    {
            // サブセットインデックスがあれば利用します
      QgsDebugMsg( QStringLiteral( "Layer has subset index - use %1 items from subset index" ).arg( mSource->mSubsetIndex.size() ) );
      mTestSubset = false;
      mMode = SubsetIndex;
    }

  if ( mMode == FileScan )
  {
    QgsDebugMsg( QStringLiteral( "File will be scanned for desired features" ) );
  }

    // レイヤーにジオメトリがある場合、本当にそれをロードする必要があるか？
    // リクエストで明示的に要求された場合、ジオメトリ（つまり空間フィルター）をテストしている場合、
    // またはサブセット式をテストしている場合は必要
  if ( !( mRequest.flags() & QgsFeatureRequest::NoGeometry )
         || mTestGeometry
         || ( mTestSubset && mSource->mSubsetExpression->needsGeometry() )
         || ( request.filterType() == QgsFeatureRequest::FilterExpression && request.filterExpression()->needsGeometry() )
     )
  {
    mLoadGeometry = true;
  }
  else
  {
    QgsDebugMsgLevel( QStringLiteral( "Feature geometries not required" ), 4 );
    mLoadGeometry = false;
  }

  // 式フィルターに必要なすべての属性がフェッチされていることを確認する
  if ( mRequest.flags() & QgsFeatureRequest::SubsetOfAttributes && request.filterType() == QgsFeatureRequest::FilterExpression )
  {
    QgsAttributeList attrs = request.subsetOfAttributes();
    // フィルタ式に必要なすべてのフィールドが準備されていることを確認
    QSet<int> attributeIndexes = request.filterExpression()->referencedAttributeIndexes( mSource->mFields );
    attributeIndexes += attrs.toSet();
    mRequest.setSubsetOfAttributes( attributeIndexes.toList() );
  }
  // order byで使用されている属性も必要
  if ( mRequest.flags() & QgsFeatureRequest::SubsetOfAttributes && !mRequest.orderBy().isEmpty() )
  {
    QgsAttributeList attrs = request.subsetOfAttributes();
    const auto usedAttributeIndices = mRequest.orderBy().usedAttributeIndices( mSource->mFields );
    for ( int attrIndex : usedAttributeIndices )
    {
      if ( !attrs.contains( attrIndex ) )
        attrs << attrIndex;
    }
    mRequest.setSubsetOfAttributes( attrs );
  }

  QgsDebugMsg( QStringLiteral( "Iterator is scanning file: " ) + ( mMode == FileScan ? "Yes" : "No" ) );
  QgsDebugMsg( QStringLiteral( "Iterator is loading geometries: " ) + ( mLoadGeometry ? "Yes" : "No" ) );
  QgsDebugMsg( QStringLiteral( "Iterator is testing geometries: " ) + ( mTestGeometry ? "Yes" : "No" ) );
  QgsDebugMsg( QStringLiteral( "Iterator is testing subset: " ) + ( mTestSubset ? "Yes" : "No" ) );

  rewind();
}

QgsDmFeatureIterator::~QgsDmFeatureIterator()
{
  close();
}

bool QgsDmFeatureIterator::fetchFeature( QgsFeature &feature )
{
  // before we do anything else, assume that there's something wrong with
  // the feature
  feature.setValid( false );

  if ( mClosed )
    return false;

  bool gotFeature = false;
  if ( mMode == FileScan )
  {
    gotFeature = nextFeatureInternal( feature );
  }
  else
  {
    while ( ! gotFeature )
    {
      qint64 fid = -1;
      if ( mMode == FeatureIds )
      {
        if ( mNextId < mFeatureIds.size() )
        {
          fid = mFeatureIds.at( mNextId );
        }
      }
      else if ( mNextId < mSource->mSubsetIndex.size() )
      {
        fid = mSource->mSubsetIndex.at( mNextId );
      }
      if ( fid < 0 ) break;
      mNextId++;
      gotFeature = ( setNextFeatureId( fid ) && nextFeatureInternal( feature ) );
    }
  }

  // CC: 2013-05-08:  What is the intent of rewind/close.  The following
  // line from previous implementation means that we cannot rewind the iterator
  // after reading last record? Is this correct?  This line can be removed if
  // not.

  if ( ! gotFeature ) close();

  geometryToDestinationCrs( feature, mTransform );

  return gotFeature;
}

bool QgsDmFeatureIterator::rewind()
{
  if ( mClosed )
    return false;

  // Skip to first data record
  if ( mMode == FileScan )
  {
    mSource->mFile->reset();
  }
  else
  {
    mNextId = 0;
  }
  return true;
}

bool QgsDmFeatureIterator::close()
{
  if ( mClosed )
    return false;

  iteratorClosed();

  mFeatureIds = QList<QgsFeatureId>();
  mClosed = true;
  return true;
}

/**
 * Check to see if the point is within the selection rectangle
 */
bool QgsDmFeatureIterator::wantGeometry( const QgsPointXY &pt ) const
{
  if ( ! mTestGeometry ) return true;
  return mFilterRect.contains( pt );
}

/**
 * Check to see if the geometry is within the selection rectangle
 */
bool QgsDmFeatureIterator::wantGeometry( const QgsGeometry &geom ) const
{
  if ( ! mTestGeometry ) return true;

  if ( mTestGeometryExact )
    return geom.intersects( mFilterRect );
  else
    return geom.boundingBox().intersects( mFilterRect );
}

bool QgsDmFeatureIterator::nextFeatureInternal( QgsFeature &feature )
{
  QgsDmFile *file = mSource->mFile.get();

  // If the iterator is not scanning the file, then it will have requested a specific
  // record, so only need to load that one.

  bool first = true;
  bool scanning = mMode == FileScan;

  while ( scanning || first )
  {
    first = false;

    // before we do anything else, assume that there's something wrong with
    feature.setValid( false );

    DmElement element;
    if(file->nextElement(element) == false) break;

    QgsFeatureId fid = file->recordId();

    QgsGeometry geom;

    if (mSource->createGeometryFromSrouce(element.points(), geom) == false) {
        continue;
    }

		if (mTestGeometry) {

			if (mTestGeometryExact) {
				if (!geom.intersects(mFilterRect))
					continue;
			}
			else {
				if (!geom.boundingBox().intersects(mFilterRect))
					continue;
			}
		}

    // At this point the current feature values are valid

    feature.setValid( true );
    feature.setFields( mSource->mFields ); // allow name-based attribute lookups
    feature.setId( fid );
    feature.initAttributes( mSource->mFields.count() );
    feature.setGeometry( geom );

    // サブセット式をテストする場合は、万が一に備えてすべての属性が必要です。

    if ( ! mTestSubset && ( mRequest.flags() & QgsFeatureRequest::SubsetOfAttributes ) )
    {
      QgsAttributeList attrs = mRequest.subsetOfAttributes();
      for ( QgsAttributeList::const_iterator i = attrs.constBegin(); i != attrs.constEnd(); ++i )
      {
        int fieldIdx = *i;
        feature.setAttribute(fieldIdx, file->fetchAttribute(mSource->mFields.at(fieldIdx).name(), fid));
      }
    }
    else
    {
      for ( int idx = 0; idx < mSource->mFields.count(); ++idx )
        feature.setAttribute(idx, file->fetchAttribute(mSource->mFields.at(idx).name(), fid));
    }

    // If the iterator hasn't already filtered out the subset, then do it now

    if ( mTestSubset )
    {
      mSource->mExpressionContext.setFeature( feature );
      QVariant isOk = mSource->mSubsetExpression->evaluate( &mSource->mExpressionContext );
      if ( mSource->mSubsetExpression->hasEvalError() ) continue;
      if ( ! isOk.toBool() ) continue;
    }

    // We have a good record, so return
    return true;

  }

  return false;
}

bool QgsDmFeatureIterator::setNextFeatureId( qint64 fid )
{
  return mSource->mFile->setNextRecordId( ( long ) fid );
}

// ------------

QgsDmFeatureSource::QgsDmFeatureSource( const QgsDmProvider *p )
    : mSubsetExpression( p->mSubsetExpression ? new QgsExpression( *p->mSubsetExpression ) : nullptr )
  , mExtent( p->mExtent )
  , mUseSpatialIndex( p->mUseSpatialIndex )
  , mSpatialIndex( p->mSpatialIndex ? new QgsSpatialIndex( *p->mSpatialIndex ) : nullptr )
  , mUseSubsetIndex( p->mUseSubsetIndex )
  , mSubsetIndex( p->mSubsetIndex )
  , mFile( nullptr )
  , mFields( p->attributeFields )
  , mFieldCount( p->attributeFields.count())
  , mGeometryType( p->mGeometryType )
  , mCrs( p->mSrid )
{
  mFile.reset(new QgsDmFile(p->mFile.get()));

  mExpressionContext << QgsExpressionContextUtils::globalScope()
                     << QgsExpressionContextUtils::projectScope( QgsProject::instance() );
  mExpressionContext.setFields( mFields );
}

QgsFeatureIterator QgsDmFeatureSource::getFeatures( const QgsFeatureRequest &request )
{
  return QgsFeatureIterator( new QgsDmFeatureIterator( this, false, request ) );
}

bool QgsDmFeatureSource::createGeometryFromSrouce(const QList<Point2d>& points, QgsGeometry & geom)
{
    return QgsDmProvider::createGeometry(mGeometryType, points, geom);
}
