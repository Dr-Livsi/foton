/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "FolderRequester.h"

#include <KLocalizedString>

#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QToolButton>

using namespace Qt::StringLiterals;

FolderRequester::FolderRequester(QWidget *parent)
    : QWidget(parent)
    , m_lineEdit(new QLineEdit(this))
    , m_button(new QToolButton(this))
{
    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins({});
    layout->addWidget(m_lineEdit);
    layout->addWidget(m_button);
    setFocusProxy(m_lineEdit);

    m_button->setIcon(QIcon::fromTheme(u"document-open-folder"_s));
    m_button->setToolTip(i18nc("@info:tooltip", "Choose a folder"));
    m_button->setAccessibleName(m_button->toolTip());

    connect(m_lineEdit, &QLineEdit::textEdited, this, [this] {
        Q_EMIT urlChanged(url());
    });
    connect(m_button, &QToolButton::clicked, this, &FolderRequester::chooseFolder);
}

QUrl FolderRequester::url() const
{
    const QString path = m_lineEdit->text().trimmed();
    if (path.isEmpty()) {
        return {};
    }
    // Keep the trailing separator like the default value of the setting.
    auto folder = QDir::fromNativeSeparators(path);
    if (!folder.endsWith(u'/')) {
        folder += u'/';
    }
    return QUrl::fromLocalFile(folder);
}

void FolderRequester::setUrl(const QUrl &url)
{
    if (this->url() == url) {
        return;
    }
    m_lineEdit->setText(QDir::toNativeSeparators(url.isLocalFile() ? url.toLocalFile() : url.toString()));
    Q_EMIT urlChanged(this->url());
}

void FolderRequester::chooseFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, i18nc("@title:window", "Choose a Folder"), url().toLocalFile());
    if (!folder.isEmpty()) {
        setUrl(QUrl::fromLocalFile(QDir(folder).absolutePath() + u'/'));
    }
}

#include "moc_FolderRequester.cpp"
