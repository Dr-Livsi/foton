/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include <QAbstractItemView>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QWidget>

/**
 * Helpers for a consistent look of the settings pages.
 */
namespace SettingsPageUtils
{
// Labels on the left like in the dialogs of Windows.
// By default, fields are only as wide as their content instead of reaching to the edge of the window.
inline void setUpFormLayouts(QWidget *page, QFormLayout::FieldGrowthPolicy growthPolicy = QFormLayout::FieldsStayAtSizeHint)
{
    const auto layouts = page->findChildren<QFormLayout *>();
    for (auto layout : layouts) {
        layout->setFieldGrowthPolicy(growthPolicy);
        layout->setRowWrapPolicy(QFormLayout::DontWrapRows);
        layout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        layout->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    }

    // The width of a combo box is only calculated once by default, which can be before the font of the style
    // is used. Then the text doesn't fit. As wide as the longest item, the list that opens fits too.
    const auto comboBoxes = page->findChildren<QComboBox *>();
    for (auto comboBox : comboBoxes) {
        comboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        comboBox->view()->setTextElideMode(Qt::ElideNone);
    }

    // The same width for the labels in all groups, so that the fields form one column.
    int labelWidth = 0;
    QList<QWidget *> labels;
    for (auto layout : layouts) {
        for (int row = 0; row < layout->rowCount(); ++row) {
            auto item = layout->itemAt(row, QFormLayout::LabelRole);
            if (item && item->widget()) {
                labels.append(item->widget());
                labelWidth = std::max(labelWidth, item->widget()->sizeHint().width());
            }
        }
    }
    for (auto label : std::as_const(labels)) {
        label->setMinimumWidth(labelWidth);
    }
}

// A less prominent explanation below a setting.
inline void makeHint(QLabel *label)
{
    auto palette = label->palette();
    palette.setColor(QPalette::WindowText, palette.color(QPalette::PlaceholderText));
    label->setPalette(palette);
}
}
