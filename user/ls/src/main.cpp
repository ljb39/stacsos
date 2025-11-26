
/** StACSOS - ls utility
 * 
 * Loell Jean Barit <ljb39@st-andrews.ac.uk>
 */

#include <stacsos/console.h>
#include <stacsos/memops.h>
#include <stacsos/user-syscall.h>
#include <stacsos/dirent.h>

using namespace stacsos;

/**
 * Sort directory entries alphabetically (ascending)
 */
static void sort_entries(dirent* entries, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        for (size_t j = i + 1; j < count; j++) {

            // Case-insensitive alphabetical compare via memops::strcmp
            if (memops::strcmp(entries[i].name, entries[j].name) > 0) {
                // Swap
                dirent temp = entries[i];
                entries[i] = entries[j];
                entries[j] = temp;
            }
        }
    }
}

int main(const char *cmdline)
{
    auto& con = console::get();

    bool long_format = false;

    // Default path
    const char* path = ".";

    // Handle null cmdline pointer
    if (!cmdline) cmdline = "";

    // Consume leading spaces
    while (*cmdline == ' ') cmdline++;

    // Parse flags
    if (*cmdline == '-') {
        cmdline++;

        if (*cmdline == 'l') {
            long_format = true;
            cmdline++;
        } else {
            con.write("usage: ls [-l] <path>\n");
            return 1;
        }

        // Skip spaces after flag
        while (*cmdline == ' ') cmdline++;
    }

    // If a path remains, override default '.'
    if (*cmdline != '\0') {
        path = cmdline;
    }

    // Buffer for directory entries
    const size_t BUFFER_SIZE = 64;
    dirent entries[BUFFER_SIZE];

    // Build request
    dirlist_request request;
    request.path = path;
    request.buffer = entries;
    request.buffer_count = BUFFER_SIZE;

    // Build result struct
    dirlist_result result;

    // Make syscall
    auto r = syscalls::readdir(&request, &result);

    // Improved error messages
    if (r.code != syscall_result_code::ok) {
        if (r.code == syscall_result_code::not_found) {
            con.writef("ls: directory not found: %s\n", path);
        } else if (r.code == syscall_result_code::not_supported) {
            con.writef("ls: not a directory: %s\n", path);
        } else {
            con.writef("ls: error reading directory: %s\n", path);
        }
        return 1;
    }

    // No entries
    if (result.entries_read == 0) {
        con.write("(empty directory)\n");
        return 0;
    }

    // Sort entries before displaying
    sort_entries(entries, result.entries_read);

    // Print directory contents
    for (size_t i = 0; i < result.entries_read; i++) {
        if (long_format) {
            // Improved long format
            if (entries[i].type == dirent_type::DT_DIR) {
                con.writef("D  %-30s/\n", entries[i].name);
            } else {
                con.writef("F  %-30s %8lu bytes\n",
                           entries[i].name, entries[i].size);
            }
        } else {
            con.writef("%s\n", entries[i].name);
        }
    }

    // Warn if buffer overflow (not all entries shown)
    if (result.has_more) {
        con.write("... (more entries not shown)\n");
    }

    return 0;
}

