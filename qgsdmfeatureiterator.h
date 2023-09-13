/***************************************************************************
    qgsdmfeatureiterator.h
    ---------------------
    begin                : March 2021
    copyright            : orbitalnet.imc
 ***************************************************************************/
#ifndef QGSDMFEATUREITERATOR_H
#define QGSDMFEATUREITERATOR_H

#include <QList>
#include "qgsfeatureiterator.h"
#include "qgsfeature.h"
#include "qgsexpressioncontext.h"

#include "qgsdmprovider.h"

class QgsDmFeatureSource : public QgsAbstractFeatureSource
{
  public:
    explicit QgsDmFeatureSource( const QgsDmProvider *p );

    QgsFeatureIterator getFeatures( const QgsFeatureRequest &request ) override;

  private:

		bool createGeometryFromSrouce(const QList<Point2d>& points, QgsGeometry& geom);

    std::unique_ptr< QgsExpression > mSubsetExpression;
    QgsExpressionContext mExpressionContext;
    QgsRectangle mExtent;
    bool mUseSpatialIndex;
    std::unique_ptr< QgsSpatialIndex > mSpatialIndex;
    bool mUseSubsetIndex;
    QList<quintptr> mSubsetIndex;
    std::unique_ptr< QgsDmFile > mFile;
    QgsFields mFields;
    int mFieldCount;  // Note: this includes field count for wkt field
    QgsWkbTypes::GeometryType mGeometryType;
    QList<int> attributeColumns;
    QgsCoordinateReferenceSystem mCrs;
		
    friend class QgsDmFeatureIterator;
};


class QgsDmFeatureIterator : public QgsAbstractFeatureIteratorFromSource<QgsDmFeatureSource>
{
    enum IteratorMode
    {
      FileScan,
      SubsetIndex,
      FeatureIds
    };
  public:
    QgsDmFeatureIterator( QgsDmFeatureSource *source, bool ownSource, const QgsFeatureRequest &request );

    ~QgsDmFeatureIterator() override;

    bool rewind() override;
    bool close() override;

    // Tests whether the geometry is required, given that testGeometry is true.
    bool wantGeometry( const QgsPointXY &point ) const;
    bool wantGeometry( const QgsGeometry &geom ) const;

  protected:
    bool fetchFeature( QgsFeature &feature ) override;

  private:

    bool setNextFeatureId( qint64 fid );

    bool nextFeatureInternal( QgsFeature &feature );

    QList<QgsFeatureId> mFeatureIds;
    IteratorMode mMode = FileScan;
    long mNextId = 0;
    bool mTestSubset = false;
    bool mTestGeometry = false;
    bool mTestGeometryExact = false;
    bool mLoadGeometry = false;
    QgsRectangle mFilterRect;
    QgsCoordinateTransform mTransform;
};


#endif // QGSDMFEATUREITERATOR_H
