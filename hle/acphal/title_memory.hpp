// SPDX-FileCopyrightText: 2026 gookie and g6shim contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <cstddef>

namespace g6shim::acphal {

bool readable(const void* address, std::size_t bytes);
bool copy_from_title(const void* source, void* destination, std::size_t bytes);

}  // namespace g6shim::acphal
