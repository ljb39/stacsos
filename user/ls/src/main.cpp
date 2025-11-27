
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
    auto& con = console::get();
    console::get().write("\e\x0e"); // Clear screen

    bool long_format = false;
    const char* path = "/";      // Default = root directory

    // -------------------------
    // SAFE CMDLINE HANDLING
    // -------------------------
    if (!cmdline) cmdline = "";

    while (*cmdline == ' ') cmdline++;

    // ----- Parse -l flag -----
    if (*cmdline == '-') {
        cmdline++;

        if (*cmdline == 'l') {
            long_format = true;
            cmdline++;
        } else {
            con.write("usage: ls [-l] <path>\n");
            return 1;
        }

        while (*cmdline == ' ') cmdline++;
    }

    // If a path is supplied, override default "/"
    if (*cmdline != '\0') {
        path = cmdline;
    }

    // -------------------------
    // ALLOCATE ENTRY BUFFER
    // -------------------------

    const size_t ENTRY_COUNT = 64;
    const size_t BUFFER_BYTES = ENTRY_COUNT * sizeof(dirent);

    auto mem = syscalls::alloc_mem(BUFFER_BYTES);
    if (mem.code != syscall_result_code::ok) {
        con.write("ls: alloc_mem failed\n");
        return 1;
    }

    dirent* entries = (dirent*) mem.ptr;

    // -------------------------
    // PERFORM DIRECTORY READ
    // -------------------------
    rw_result result = syscalls::get_dir_contents(path, (char*)entries, BUFFER_BYTES);

    if (result.code != syscall_result_code::ok) {
        if (result.code == syscall_result_code::not_found)
            con.writef("ls: directory not found: %s\n", path);
        else if (result.code == syscall_result_code::not_supported)
            con.writef("ls: not a directory: %s\n", path);
        else
            con.writef("ls: error reading directory: %s\n", path);

        return 1;
    }

    size_t count = result.length / sizeof(dirent);

    if (count == 0) {
        con.write("(empty directory)\n");
        return 0;
    }

    // -------------------------
    // SORT RESULTS
    // -------------------------
    sort_entries(entries, count);

    // -------------------------
    // SHORT FORMAT
    // -------------------------
    if (!long_format) {
        for (size_t i = 0; i < count; i++) {
            if (entries[i].type == 1)
                con.writef("%s/\n", entries[i].name);
            else
                con.writef("%s\n", entries[i].name);
        }
        return 0;
    }

    // -------------------------
    // LONG FORMAT
    // -------------------------

    // Find longest filename for alignment
    size_t max_name_len = 0;
    for (size_t i = 0; i < count; i++) {
        size_t len = memops::strlen(entries[i].name);
        if (len > max_name_len) max_name_len = len;
    }
    if (max_name_len > 40) max_name_len = 40;

    // Output long format
    for (size_t i = 0; i < count; i++) {
        const bool is_dir = (entries[i].type == 1);

        con.write(is_dir ? "[D] " : "[F] ");

        size_t name_len = memops::strlen(entries[i].name);
        if (name_len > max_name_len) name_len = max_name_len;

        // Print filename char-by-char
        for (size_t j = 0; j < name_len; j++) {
            char ch[2] = { entries[i].name[j], 0 };
            con.write(ch);
        }

        if (is_dir) con.write("/");

        // Padding
        size_t printed_len = name_len + (is_dir ? 1 : 0);
        size_t col_width = max_name_len + 2;

        while (printed_len < col_width) {
            con.write(" ");
            printed_len++;
        }

        // File size (only for files)
        if (!is_dir)
            con.writef("%u bytes", (unsigned)entries[i].size);

        con.write("\n");
    }

    return 0;
}

