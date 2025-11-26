/* SPDX-License-Identifier: MIT */

/* StACSOS - Utility Library
 *
 * Copyright (c) University of St Andrews 2024
 * Loell Jean Barit <ljb39@st-andrews.ac.uk>
 */

#pragma once

struct dirent {
    char name[256];  // File or directory name (adjust length as needed)
    u8 type;    // File type: 0 = file, 1 = directory
    u64 size;   // Size for files (0 for directories)
};