/* SPDX-FileCopyrightText: 2024 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include "foton_debug.h"

/**
 * Convenience functions for categorizing output
 */
namespace Log
{
using CategoryFunction = QMessageLogger::CategoryFunction;

static inline auto debug(CategoryFunction category = FOTON_LOG)
{
    return QMessageLogger(nullptr, 0, nullptr).debug(category).noquote();
}

static inline auto info(CategoryFunction category = FOTON_LOG)
{
    return QMessageLogger(nullptr, 0, nullptr).info(category).noquote();
}

static inline auto warning(CategoryFunction category = FOTON_LOG)
{
    return QMessageLogger(nullptr, 0, nullptr).warning(category).noquote();
}

static inline auto critical(CategoryFunction category = FOTON_LOG)
{
    return QMessageLogger(nullptr, 0, nullptr).critical(category).noquote();
}

static inline auto fatal(CategoryFunction category = FOTON_LOG)
{
    return QMessageLogger(nullptr, 0, nullptr).fatal(category).noquote();
}

};
