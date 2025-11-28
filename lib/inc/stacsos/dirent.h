/* SPDX-License-Identifier: MIT */

/* StACSOS - Utility Library
 *
 * Copyright (c) University of St Andrews 2024
 * Loell Jean Barit <ljb39@st-andrews.ac.uk>
 */

#pragma once

#define MAX_FILENAME_LEN 256

/**
 * Represents a single directory entry.
 */
struct dirent {
    char name[MAX_FILENAME_LEN];
    u8 type;     // 0=file, 1=dir
    u64 size;    // file size (0 for dir)
};