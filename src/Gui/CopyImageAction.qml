/* SPDX-FileCopyrightText: 2025 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick.Templates as T
import foton.private

T.Action {
    icon.name: "edit-copy"
    text: i18nc("@action", "Copy")
    onTriggered: contextWindow.copyImage()
}
