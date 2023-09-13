/***************************************************************************
 *   Copyright orbitalnet.imc                                              *
 *                                                                         *
 *   This is a plugin generated from the QGIS plugin template              *
 ***************************************************************************/
#ifndef QGSDMSOURCESELECT_H
#define QGSDMSOURCESELECT_H

#include "ui_qgsdmsourceselectbase.h"

#include <QTextStream>
#include "qgshelp.h"
#include "qgsguiutils.h"
#include "qgsproviderregistry.h"
#include "qgsabstractdatasourcewidget.h"
#include "qgsdmfile.h"

class QButtonGroup;
class QgisInterface;

/**
 * \class QgsDmSourceSelect
 */
class QgsDmSourceSelect : public QgsAbstractDataSourceWidget, private Ui::QgsDmSourceSelectBase
{
    Q_OBJECT

  public:
    QgsDmSourceSelect( QWidget *parent = nullptr, Qt::WindowFlags fl = QgsGuiUtils::ModalDialogFlags, QgsProviderRegistry::WidgetMode widgetMode = QgsProviderRegistry::WidgetMode::None );

  private:
    bool loadDmFilesDefinition();

  private:
    std::unique_ptr<QgsDmFile> mFile;
    int mExampleRowCount = 20;
    int mBadRowCount = 0;
    QString mSettingsKey;
    QString mLastFileType;
    QButtonGroup *bgFileFormat = nullptr;
    QButtonGroup *bgGeomType = nullptr;
    void showHelp();
		void addDmLayer(const QUrl& baseUrl, const QString& dataType);

  public slots:
    void addButtonClicked() override;
    void updateDirectoryPath();
		void updateOverwriteingTimes(int value);
    void enableAccept();
    bool validate();

private slots:
  void enableOverwritingTimes(bool checked);
	
};

#endif // QGSDMSOURCESELECT_H
