
/** StACSOS - ls utility
 * 
 * Loell Jean Barit <ljb39@st-andrews.ac.uk>
 */

#include <stacsos/dirent.h>
#include <stacsos/user-syscall.h>
#include <stacsos/console.h>
#include <stacsos/memops.h>

using namespace stacsos;

/**
 * Sort directory entries alphabetically (ascending)
 */
static void sort_entries(dirent* entries, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        for (size_t j = i + 1; j < count; j++) {
            if (memops::strcmp(entries[i].name, entries[j].name) > 0) {
                // Swap
                dirent temp;
                memops::memcpy(&temp, &entries[i], sizeof(dirent));
                memops::memcpy(&entries[i], &entries[j], sizeof(dirent));
                memops::memcpy(&entries[j], &temp, sizeof(dirent));
            }
        }
    }
}

int main(const char *cmdline)
{
    console::get().write("\e\x0e"); // Clear screen

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

    con.write("\nParsing Finished.");

    // // Buffer for directory entries
    const size_t BUFFER_SIZE = 64;
    dirent entries[BUFFER_SIZE];

    con.write("\nEntries and buffer initialised");


    // // Make syscall to fetch the directory contents
    // // You might need to adjust sys_get_dir_contents to be able to return the number of entries read
    rw_result result = syscalls::get_dir_contents(path, (char*)entries, BUFFER_SIZE);

    con.write("\nResults retrieved");


    // Improved error handling
    if (result.code != syscall_result_code::ok) {
        if (result.code == syscall_result_code::not_found) {
            con.writef("ls: directory not found: %s\n", path);
        } else if (result.code == syscall_result_code::not_supported) {
            con.writef("ls: not a directory: %s\n", path);
        } else {
            con.writef("ls: error reading directory: %s\n", path);
        }
        return 1;
    }

    // No entries
    if (result.length == 0) {
        con.write("(empty directory)\n");
        return 0;
    }

    // Sort entries before displaying
    sort_entries(entries, result.length);

    // Print directory contents
    for (size_t i = 0; i < result.length; i++) {
        if (long_format) {
            // Long format: show file type and size
            if (entries[i].type == 1) {  // Directory
                con.writef("D  %-30s/\n", entries[i].name);
            } else {  // File
                con.writef("F  %-30s %8lu bytes\n", entries[i].name, entries[i].size);
            }
        } else {
            // Short format: just show the file/directory name
            con.writef("%s\n", entries[i].name);
        }
    }

    return 0;
}
