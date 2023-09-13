/***************************************************************************
 *   Copyright orbitalnet.imc                                              *
 *                                                                         *
 *   GUI for loading a DM file as a layer in QGIS                          *
 *   This plugin works in conjunction with the DM data                     *
 *   provider plugin                                                       *
 ***************************************************************************/
#include "qgsdmsourceselect.h"

#include "layertree/qgslayertreeview.h"
#include "layertree/qgslayertreemodel.h"
#include "qgisinterface.h"
#include "qgslogger.h"
#include "qgsvectordataprovider.h"
#include "qgsdmprovider.h"
#include "qgsdmfile.h"
#include "qgssettings.h"
#include "qgsproviderregistry.h"
#include "qgsgui.h"
#include "qgsmapcanvas.h"
#include "qgsproject.h"
#include "qgslayertree.h"
#include "qgslayertreegroup.h"

#include <QButtonGroup>
#include <QFile>
#include <QFileDialog>
#include <QDir>
#include <QMessageBox>
#include <QRegExp>
#include <QTextStream>
#include <QTextCodec>
#include <QUrl>
#include <QSpinBox>
#include <QStandardPaths>

const int MAX_SAMPLE_LENGTH = 200;

QgsDmSourceSelect::QgsDmSourceSelect( QWidget *parent, Qt::WindowFlags fl, QgsProviderRegistry::WidgetMode theWidgetMode )
  : QgsAbstractDataSourceWidget( parent, fl, theWidgetMode )
  , mFile( qgis::make_unique<QgsDmFile>() )
  , mSettingsKey( QStringLiteral( "/Plugin-Dm" ) )
{

  setupUi( this );
  QgsGui::instance()->enableAutoGeometryRestore( this );
  setupButtons( buttonBox );
  connect( buttonBox, &QDialogButtonBox::helpRequested, this, &QgsDmSourceSelect::showHelp );

  QgsSettings settings;
	txtSrid->setText(settings.value(mSettingsKey + QStringLiteral("/srid"), QString::number(2449)).toString());

  connect(txtGroupName, &QLineEdit::textChanged, this, &QgsDmSourceSelect::enableAccept);
  connect(txtSrid, &QLineEdit::textChanged, this, &QgsDmSourceSelect::enableAccept);
  connect(chkForceOverwriting, &QCheckBox::stateChanged, this, &QgsDmSourceSelect::enableOverwritingTimes);
  connect(mOverwritingTimes, QOverload<int>::of(&QSpinBox::valueChanged), this, &QgsDmSourceSelect::updateOverwriteingTimes);

  mDirectoryWidget->setDialogTitle( tr( "Choose a DM Folder that has DM files" ) );
  //mDirectoryWidget->setDefaultRoot( settings.value( mSettingsKey + QStringLiteral( "/lastDMDirectory" ), QDir::fromNativeSeparators(QStandardPaths::displayName(QStandardPaths::DesktopLocation))).toString() );
	mDirectoryWidget->setDefaultRoot(settings.value(mSettingsKey + QStringLiteral("/lastDMDirectory"), QString()).toString());
  connect( mDirectoryWidget, &QgsFileWidget::fileChanged, this, &QgsDmSourceSelect::updateDirectoryPath );

	lblStatus->clear();
}

void QgsDmSourceSelect::addButtonClicked()
{
	// 追加ボタンが押せる時点で入力チェック済み

	// 既にupdateDirectory()でQgsDmFileにDMフォルダパスを渡しているので
	// isValid()でフォルダが存在することとdmファイルがあることが確認できる
  if ( ! mFile->isValid() )
  {
    QMessageBox::warning( this, tr( "Invalid DM file" ), tr( "有効なDMフォルダパスを入力してください。" ) );
    return;
  }

	// この時点で一旦QgsDmFileでディレクトリ内の全DMファイルを読み込んでレイヤー作成対象のデータがあるか確認する
	QMap<QString, bool> hasMap;
	if (!mFile->test(hasMap)) {
		QMessageBox::warning(this, tr("Invalid DM file"), tr("有効なDMデータが見つかりません。"));
		return;
	}

	qApp->setOverrideCursor(Qt::WaitCursor);

  // レイヤープロバイダーに渡すURIを作成する
	QUrl url = QUrl::fromLocalFile(mDirectoryWidget->filePath());
	url.addQueryItem( QStringLiteral( "srid" ), txtSrid->text().trimmed());
	if (chkForceOverwriting->isChecked() )
  {
    url.addQueryItem( QStringLiteral( "overwritingTimes" ), QString::number(mOverwritingTimes->value()));
  }
	url.addQueryItem(QStringLiteral("spatialIndex"), cbxSpatialIndex->isChecked() ? QStringLiteral("yes") : QStringLiteral("no"));

	// グループを作成する
	QgsLayerTree *root = QgsProject::instance()->layerTreeRoot();
	QgsLayerTreeGroup *group = root->addGroup(txtGroupName->text());
	
	QgsLayerTreeView *treeView = NULL;
	const QList< QWidget * > widgets = qApp->allWidgets();
	for (const QWidget *widget : widgets)
	{
		if (!treeView)
		{
			treeView = widget->findChild<QgsLayerTreeView *>(QStringLiteral("theLayerTreeView"));
		}
	}
	// これ以降作成するレイヤーをグループの下に配置するためカレントにする
	if (treeView) {
		treeView->setCurrentIndex(treeView->layerTreeModel()->node2index(group));
	}

  // add the layer to the map
	QMapIterator<QString, bool> itr(hasMap);
	while (itr.hasNext())
	{
		itr.next();
		if (itr.value()) {
			addDmLayer(url, itr.key());
		}
	}

	QgsSettings settings;
	settings.setValue(mSettingsKey + "/lastDMDirectory", QFileInfo(mDirectoryWidget->filePath()).dir().path());
	settings.setValue(mSettingsKey + "/srid", txtSrid->text());

  // clear the directory and group name show something has happened, ready for another file
  mDirectoryWidget->setFilePath( QString() );
  txtGroupName->setText( QString() );
	chkForceOverwriting->setChecked(false);
	mOverwritingTimes->setValue(0);

	qApp->restoreOverrideCursor();

  if ( widgetMode() == QgsProviderRegistry::WidgetMode::None )
  {
    accept();
  }
}

bool QgsDmSourceSelect::loadDmFilesDefinition()
{
  mFile->setDirPath( mDirectoryWidget->filePath());
	if (chkForceOverwriting->isChecked()) {
		mFile->setOverwritingTimes(mOverwritingTimes->value());
	}
	else {
		mFile->setOverwritingTimes(-1);
	}
  return mFile->isValid();
}


void QgsDmSourceSelect::updateDirectoryPath()
{
	QString dirPath = mDirectoryWidget->filePath();
	if (!dirPath.isEmpty()) {
		QFileInfo fi(dirPath);
		if (fi.exists()) {
			if (txtGroupName->text().isEmpty()) {
				txtGroupName->setText(fi.baseName());
			}
		}
	}

	loadDmFilesDefinition();
	enableAccept();
}

void QgsDmSourceSelect::updateOverwriteingTimes(int value)
{
	loadDmFilesDefinition();
	enableAccept();
}


bool QgsDmSourceSelect::validate()
{
  // Check that input data is valid - provide a status message if not..

  QString message;

	do
	{
		QString dirpath = mDirectoryWidget->filePath().trimmed();
		if (dirpath.isEmpty())
		{
			message = tr("DMフォルダが未入力です");
			return false;
		}

		QDir dir(dirpath);
		if (dir.exists() == false)
		{
			message = tr("DMフォルダが存在しません");
			return false;
		}

		if (dir.entryList(QStringList() << "*.dm", QDir::Files).count() == 0) {
			message = tr("DMファイルが存在しません");
			return false;
		}

		// グループ名
		if (txtGroupName->text().isEmpty()) {
			message = tr("グループ名が未入力です");
			break;
		}

		// SRID
		QString srid = txtSrid->text().trimmed();
		if (srid.isEmpty()) {
			message = tr("SRIDが未入力です");
			break;
		}
		QRegExp rxNumber("^\\d+$");
		if (rxNumber.exactMatch(srid) == false) {
			message = tr("SRIDが不正です");
			break;

		}
		// 強制上書き
		if (chkForceOverwriting->isChecked()) {
			if (mOverwritingTimes->value() == 0) {
				message = tr("修正回数が未入力です");
				break;
			}
		}

		lblStatus->clear();
		return true;

	} while (false);
 
  lblStatus->setText( message );
  return false;
}

void QgsDmSourceSelect::enableAccept()
{
  emit enableButtons( validate() );
}

void QgsDmSourceSelect::showHelp()
{
	// 何もしない
  //QgsHelp::openHelp( QStringLiteral( "managing_data_source/opening_data.html#importing-a-dm-file" ) );
}

void QgsDmSourceSelect::addDmLayer(const QUrl & baseUrl, const QString & dataType)
{
	QUrl url(baseUrl);

	url.addQueryItem(QStringLiteral("dataType"), dataType);

	emit addVectorLayer(QString::fromLatin1(url.toEncoded()), dataType);
}

void QgsDmSourceSelect::enableOverwritingTimes(bool checked)
{
	mOverwritingTimes->setEnabled(checked);
}