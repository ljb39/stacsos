
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

    // No command line: provide friendly usage
    if (!cmdline || memops::strlen(cmdline) == 0) {
        con.write("usage: ls [-l] <path>\n");
        return 1;
    }

    // Parsing command-line arguments
    bool long_format = false;
    const char* path = nullptr;

    // Flags (only -l supported)
    while (*cmdline) {
        if (*cmdline == '-') {
            cmdline++;

            if (*cmdline == 'l') {
                long_format = true;
                cmdline++;
            } else {
                con.write("usage: ls [-l] <path>\n");
                return 1;
            }
        } else {
            break;
        }
    }

    // Skip spaces
    while (*cmdline == ' ') {
        cmdline++;
    }

    // Path must be provided
    if (*cmdline != '\0') {
        path = cmdline;
    } else {
        con.write("usage: ls [-l] <path>\n");
        return 1;
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

    // Check syscall result
    if (r.code != syscall_result_code::ok) {
        con.writef("ls: cannot read directory '%s'\n", path);
        return 1;
    }

    // No entries
    if (result.entries_read == 0) {
        con.write("(empty directory)\n");
        return 0;
    }

    //Sort entries before displaying
    sort_entries(entries, result.entries_read);

    //Print directory contents
    for (size_t i = 0; i < result.entries_read; i++) {
        if (long_format) {
            //Long format: show type and size
            char type = (entries[i].type == dirent_type::DT_FILE) ? 'F' : 'D';
            con.writef("[%c] %-30s", type, entries[i].name);

            if (entries[i].type == dirent_type::DT_FILE) {
                con.writef(" %lu", entries[i].size);
            }

            con.write("\n");
        } else {
            con.writef("%s\n", entries[i].name);
        }
    }

    //Warn if buffer overflow
    if (result.has_more) {
        con.write("... (more entries not shown)\n");
    }

    return 0;
}
