/***************************************************************************
      qgsdmprovider.h  -  Data provider for DM
                             -------------------
    begin                : March 2021
    copyright            : orbitalnet.imc
 ***************************************************************************/

#ifndef QGSDMPROVIDER_H
#define QGSDMPROVIDER_H

#include <QStringList>

#include "qgsvectordataprovider.h"
#include "qgscoordinatereferencesystem.h"
#include "qgsdmfile.h"
#include "qgsfields.h"

#include "qgsprovidermetadata.h"

class QgsFeature;
class QgsField;
class QgsGeometry;
class QgsPointXY;
class QFile;
class QTextStream;

class QgsDmFeatureIterator;
class QgsExpression;
class QgsSpatialIndex;

/**
 * \class QgsDmProvider
 * \brief Data provider for DM files.
 *
 * The provider needs to know both the path to the text file and
 * the delimiter to use. Since the means to add a layer is fairly
 * rigid, we must provide this information encoded in a form that
 * the provider can decipher and use.
 *
 * The uri must defines the file path and the parameters used to
 * interpret the contents of the file.
 *
 * Example uri = "/home/foo/delim.txt?delimiter=|"*
 *
 * For detailed information on the uri format see the QGSVectorLayer
 * documentation.  Note that the interpretation of the URI is split
 * between QgsDmFile and QgsDmProvider.
 *
 */
class QgsDmProvider : public QgsVectorDataProvider
{
    Q_OBJECT

  public:

    static const QString TEXT_PROVIDER_KEY;
    static const QString TEXT_PROVIDER_DESCRIPTION;

    explicit QgsDmProvider( const QString &uri, const QgsDataProvider::ProviderOptions &providerOptions );
    ~QgsDmProvider() override;

    /* Implementation of functions from QgsVectorDataProvider */
		QgsAbstractFeatureSource *featureSource() const override;
		QString storageType() const override;
		QgsFeatureIterator getFeatures(const QgsFeatureRequest &request) const override;
		QgsWkbTypes::Type wkbType() const override;
		long featureCount() const override;
		QgsFields fields() const override;
		QgsVectorDataProvider::Capabilities capabilities() const override;
		bool createSpatialIndex() override;
		QgsFeatureSource::SpatialIndexPresence hasSpatialIndex() const override;
		QString name() const override;
		QString description() const override;
		QgsRectangle extent() const override;
		bool isValid() const override;
		QgsCoordinateReferenceSystem crs() const override;
		bool setSubsetString(const QString &subset, bool updateFeatureCount = true) override;
		bool supportsSubsetString() const override { return true; }
		QString subsetString() const override
		{
			return mSubsetString;
		}


  private:

    void scanFile( bool buildIndexes );

    //some of these methods const, as they need to be called from const methods such as extent()
    void rescanFile() const;
    void resetCachedSubset() const;
    void resetIndexes() const;
    void clearInvalidLines() const;
    void recordInvalidLine( const QString &message );
    void reportErrors( const QStringList &messages = QStringList(), bool showDialog = false ) const;
    static bool recordIsEmpty( QStringList &record );
    void setUriParameter( const QString &parameter, const QString &value );

		void appendExtent(const QgsGeometry& geom, bool& foundFirstGeometry);
		void addFeaturemToSpatialIndex(int fid, const QgsGeometry& geom);

    // mLayerValid defines whether the layer has been loaded as a valid layer
    bool mLayerValid = false;
    // mValid defines whether the layer is currently valid (may differ from
    // mLayerValid if the file has been rewritten)
    mutable bool mValid = false;

		static bool createGeometry(QgsWkbTypes::GeometryType type, const QList<Point2d>& points, QgsGeometry& geom);
		static QgsPolylineXY createPolyline(const QList<Point2d>& vertexes, bool forPolygon = false);

    //! Text file
    std::unique_ptr< QgsDmFile > mFile;

		QString mDataType;

    QgsFields attributeFields;

    //! Layer extent
    mutable QgsRectangle mExtent;
		mutable long mNumberFeatures;

    QString mSubsetString;
    mutable QString mCachedSubsetString;
    std::unique_ptr< QgsExpression > mSubsetExpression;
    bool mBuildSubsetIndex = false;
    mutable QList<quintptr> mSubsetIndex;
    mutable bool mUseSubsetIndex = false;
    mutable bool mCachedUseSubsetIndex;

		//! Storage for any lines in the file that couldn't be loaded
		int mMaxInvalidLines = 50;
		mutable int mNExtraInvalidLines;
		mutable QStringList mInvalidLines;
		//! Only want to show the invalid lines once to the user
		bool mShowInvalidLines = true;

    // Coordinate reference system
    QString mSrid;
		QgsCoordinateReferenceSystem mCrs;

    QgsWkbTypes::Type mWkbType = QgsWkbTypes::NoGeometry;
    QgsWkbTypes::GeometryType mGeometryType = QgsWkbTypes::UnknownGeometry;

    // Spatial index
    bool mBuildSpatialIndex = false;
    mutable bool mUseSpatialIndex;
    mutable bool mCachedUseSpatialIndex;
    mutable std::unique_ptr< QgsSpatialIndex > mSpatialIndex;

    friend class QgsDmFeatureIterator;
    friend class QgsDmFeatureSource;
};

class QgsDmProviderMetadata: public QgsProviderMetadata
{
  public:
    QgsDmProviderMetadata();
    QgsDataProvider *createProvider( const QString &uri, const QgsDataProvider::ProviderOptions &options ) override;
    QVariantMap decodeUri( const QString &uri ) override;
};

#endif
