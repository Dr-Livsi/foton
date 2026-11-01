/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-FileCopyrightText: 2019 Boudhayan Gupta <bgupta@kde.org>

 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include "ImagePlatform.h"
#include <memory>

using ImagePlatformPtr = std::unique_ptr<ImagePlatform>;
ImagePlatformPtr loadImagePlatform();
