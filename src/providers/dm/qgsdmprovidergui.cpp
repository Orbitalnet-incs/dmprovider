/***************************************************************************
  qgsdmprovidergui.cpp
  --------------------------------------
  Date                 : March 2021
  Copyright            : orbitalnet.imc
 ***************************************************************************/

#include "qgsapplication.h"
#include "qgsproviderguimetadata.h"
#include "qgssourceselectprovider.h"

#include "qgsdmprovider.h"
#include "qgsdmsourceselect.h"

//! Provider for DM source select
class QgsDmSourceSelectProvider : public QgsSourceSelectProvider
{
  public:

    QString providerKey() const override { return QStringLiteral( "dm" ); }
    QString text() const override { return QObject::tr( "DM" ); }
    int ordering() const override { return QgsSourceSelectProvider::OrderLocalProvider + 40; }
    QIcon icon() const override { return QgsApplication::getThemeIcon( QStringLiteral( "/mActionAddDmLayer.svg" ) ); }
    QgsAbstractDataSourceWidget *createDataSourceWidget( QWidget *parent = nullptr, Qt::WindowFlags fl = Qt::Widget, QgsProviderRegistry::WidgetMode widgetMode = QgsProviderRegistry::WidgetMode::Embedded ) const override
    {
      return new QgsDmSourceSelect( parent, fl, widgetMode );
    }
};


class QgsDmProviderGuiMetadata: public QgsProviderGuiMetadata
{
  public:
    QgsDmProviderGuiMetadata()
      : QgsProviderGuiMetadata( QgsDmProvider::TEXT_PROVIDER_KEY )
    {
    }

    QList<QgsSourceSelectProvider *> sourceSelectProviders() override
    {
      QList<QgsSourceSelectProvider *> providers;
      providers << new QgsDmSourceSelectProvider;
      return providers;
    }
};


QGISEXTERN QgsProviderGuiMetadata *providerGuiMetadataFactory()
{
  return new QgsDmProviderGuiMetadata();
}
