/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick.Templates as T
import foton.private

T.Action {
    enabled: FotonCore.textRecognitionSupported
    icon.name: "document-scan"
    text: i18nc("@action", "Recognize Text")
    onTriggered: FotonCore.recognizeText()
}
