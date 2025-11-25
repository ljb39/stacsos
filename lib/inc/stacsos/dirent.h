/* SPDX-License-Identifier: MIT */

/* StACSOS - Utility Library
 *
 * Copyright (c) University of St Andrews 2024
 * Loell Jean Barit <ljb39@st-andrews.ac.uk>
 */

#pragma once

/**
 * Maximum filename character length
 */
#define MAX_FILENAME_LEN 256

/**
 * Directory entry types.
 * 
 * 1 - Directory
 * 0 - File
 */
enum class dirent_type : u8 {
    DT_FILE = 0,
    DT_DIR = 1
};

/**
 * A single directory entry in the list.
 */
struct dirent {
    char name[MAX_FILENAME_LEN];
    dirent_type type;
    u64 size;  // File size (0 for directories)
};

/**
 * Request structure. 
 * 
 * path - path to the directory
 * buffer - where the results are written to
 * buffer_count - number of dirent entries are in the buffer 
 */
struct dirlist_request {
    const char* path;
    struct dirent* buffer;
    size_t buffer_count;
};

/**
 * Result/response structure.
 * 
 * entries_read - number of entries written
 * has_more - used if the number of entries exceed the buffer count.
 */
struct dirlist_result {
    size_t entries_read;        
    bool has_more;
};