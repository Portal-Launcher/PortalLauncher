// SPDX-License-Identifier: GPL-3.0-only
#include "ImportLaunchersPage.h"
#include "ui_ImportLaunchersPage.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QRegularExpression>

#include "modplatform/import_launchers/LauncherImportTask.h"
#include "ui/dialogs/NewInstanceDialog.h"
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
    if (!m_selected.iconPath.isEmpty() && QFileInfo::exists(m_selected.iconPath)) {
        // give the icon a stable, filesystem-safe key of its own
        QString key = QStringLiteral("import_%1").arg(m_selected.name.toLower());
        key.replace(QRegularExpression("[^a-z0-9]+"), "_");
        m_dialog->setSuggestedIconFromFile(m_selected.iconPath, key);
    }
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
