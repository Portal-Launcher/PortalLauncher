// SPDX-License-Identifier: GPL-3.0-only
#include "ImportLaunchersPage.h"
#include "ui_ImportLaunchersPage.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QRegularExpression>

#include "Application.h"
#include "QObjectPtr.h"
#include "InstanceList.h"
#include "icons/IconList.h"
#include "modplatform/import_launchers/LauncherImportTask.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/NewInstanceDialog.h"
#include "ui/dialogs/ProgressDialog.h"
#include "ui/widgets/ProjectItem.h"

namespace LauncherImport {

ImportLaunchersPage::ImportLaunchersPage(NewInstanceDialog* dialog, QWidget* parent)
    : QWidget(parent), m_dialog(dialog), ui(new Ui::ImportLaunchersPage)
{
    ui->setupUi(this);

    m_model = new ListModel(this);
    m_filter = new FilterModel(this);
    m_filter->setSourceModel(m_model);

    ui->instanceList->setModel(m_filter);
    ui->instanceList->setSortingEnabled(true);
    ui->instanceList->header()->hide();
    ui->instanceList->setIndentation(0);
    ui->instanceList->setIconSize(QSize(42, 42));
    ui->instanceList->setItemDelegate(new ProjectItemDelegate(this));

    const auto& sortings = m_filter->availableSortings();
    for (auto it = sortings.constBegin(); it != sortings.constEnd(); ++it)
        ui->sortByBox->addItem(it.key());
    ui->sortByBox->setCurrentText(m_filter->translateCurrentSorting());

    connect(ui->instanceList->selectionModel(), &QItemSelectionModel::currentChanged, this, &ImportLaunchersPage::onSelectionChanged);
    connect(ui->sortByBox, &QComboBox::currentTextChanged, this,
            [this](const QString& text) { m_filter->setSorting(m_filter->availableSortings().value(text)); });
    connect(ui->searchEdit, &QLineEdit::textChanged, this, [this](const QString& text) { m_filter->setSearchTerm(text); });
    connect(m_model, &ListModel::scanFinished, this, &ImportLaunchersPage::onScanFinished);
    connect(ui->importAllButton, &QPushButton::clicked, this, &ImportLaunchersPage::importAll);

    connect(ui->rescanButton, &QPushButton::clicked, this, [this] {
        ui->statusLabel->setText(tr("Looking for instances…"));
        m_model->refresh();
    });
    connect(ui->browseButton, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Pick a launcher folder or an instances folder"), QDir::homePath(),
                                                              QFileDialog::ShowDirsOnly);
        if (dir.isEmpty())
            return;
        m_model->addExtraPath(dir);
        ui->statusLabel->setText(tr("Looking for instances…"));
        m_model->refresh();
    });
}

ImportLaunchersPage::~ImportLaunchersPage()
{
    delete ui;
}

void ImportLaunchersPage::openedImpl()
{
    if (!m_initialized) {
        m_initialized = true;
        ui->statusLabel->setText(tr("Looking for instances…"));
        m_model->refresh();
    }
    suggestCurrent();
}

void ImportLaunchersPage::retranslate()
{
    ui->retranslateUi(this);
}

void ImportLaunchersPage::onScanFinished(int count)
{
    if (count == 0)
        ui->statusLabel->setText(tr("No instances from other launchers were found. If yours live somewhere unusual, point at the folder."));
    else
        ui->statusLabel->setText(tr("%n instance(s) found. Pick one to import it; the original is left untouched.", "", count));
    ui->instanceList->sortByColumn(0, Qt::AscendingOrder);
    ui->importAllButton->setEnabled(count > 0);
}

void ImportLaunchersPage::onSelectionChanged(const QModelIndex& now, const QModelIndex&)
{
    if (!now.isValid()) {
        m_selected = {};
        suggestCurrent();
        return;
    }
    m_selected = m_filter->data(now, Qt::UserRole).value<FoundInstance>();
    suggestCurrent();
}

void ImportLaunchersPage::suggestCurrent()
{
    if (!isOpened)
        return;
    if (!m_selected.isValid()) {
        m_dialog->setSuggestedPack();
        return;
    }
    m_dialog->setSuggestedPack(m_selected.name, new LauncherImportTask(m_selected));
    if (!m_selected.iconPath.isEmpty() && QFileInfo::exists(m_selected.iconPath))
        m_dialog->setSuggestedIconFromFile(m_selected.iconPath, iconKeyFor(m_selected));
}

QString ImportLaunchersPage::iconKeyFor(const FoundInstance& inst)
{
    // a stable, filesystem-safe key of the instance's own
    QString key = QStringLiteral("import_%1").arg(inst.name.toLower());
    key.replace(QRegularExpression("[^a-z0-9]+"), "_");
    return key;
}

void ImportLaunchersPage::importAll()
{
    // everything currently listed, in the order shown
    QList<FoundInstance> batch;
    for (int row = 0; row < m_filter->rowCount(); row++)
        batch.append(m_filter->data(m_filter->index(row, 0), Qt::UserRole).value<FoundInstance>());
    if (batch.isEmpty())
        return;

    auto response = CustomMessageBox::selectable(this, tr("Import All"),
                                                 tr("Import all %n listed instance(s)? Each one is copied over in turn and the other "
                                                    "launchers keep their copies.",
                                                    "", batch.size()),
                                                 QMessageBox::Question, QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok)
                        ->exec();
    if (response != QMessageBox::Ok)
        return;

    const QString group = m_dialog->instGroup();
    QStringList failed;
    for (const auto& inst : batch) {
        auto* import = new LauncherImportTask(inst);
        InstanceName instName(inst.name, inst.mcVersion);
        import->setName(instName);
        import->setGroup(group);
        QString icon = "default";
        if (!inst.iconPath.isEmpty() && QFileInfo::exists(inst.iconPath)) {
            const QString key = iconKeyFor(inst);
            APPLICATION->icons()->installIcon(inst.iconPath, key + "." + QFileInfo(inst.iconPath).suffix());
            icon = key;
        }
        import->setIcon(icon);

        unique_qobject_ptr<Task> task(APPLICATION->instances()->wrapInstanceTask(import));
        ProgressDialog progress(this);
        progress.setSkipButton(true, tr("Skip"));
        const int result = progress.execWithTask(task.get());
        if (result != QDialog::Accepted)
            failed.append(QStringLiteral("%1 (%2)").arg(inst.name, inst.launcherName()));
    }

    if (!failed.isEmpty())
        CustomMessageBox::selectable(this, tr("Import All"), tr("These could not be imported:\n%1").arg(failed.join('\n')), QMessageBox::Warning)
            ->show();
    // the instances already exist; nothing is left for the dialog's own OK to do
    m_dialog->reject();
}

void ImportLaunchersPage::setSearchTerm(QString term)
{
    ui->searchEdit->setText(term);
}

QString ImportLaunchersPage::getSerachTerm() const
{
    return ui->searchEdit->text();
}

}  // namespace LauncherImport
