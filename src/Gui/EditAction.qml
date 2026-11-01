/* SPDX-FileCopyrightText: 2025 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick.Templates as T
import foton.private

T.Action {
    icon.name: "edit-image"
    text: i18nc("@action edit screenshot", "Edit…")
    checkable: true
    checked: contextWindow.annotating
    onToggled: contextWindow.annotating = checked
}
