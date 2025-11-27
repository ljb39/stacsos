
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
    const char* path = "";

    // ----- Parse Arguments -----

    if (!cmdline) cmdline = "/";

    while (*cmdline == ' ') cmdline++;

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

    if (*cmdline != '\0') {
        path = cmdline;
    }

    // ----- Allocate Entry Buffer in HEAP -----

    const size_t ENTRY_COUNT = 64;
    const size_t BUFFER_BYTES = ENTRY_COUNT * sizeof(dirent);

    auto mem = syscalls::alloc_mem(BUFFER_BYTES);
    if (mem.code != syscall_result_code::ok) {
    con.write("ls: alloc_mem failed\n");
    return 1;
    }

    dirent* entries = (dirent*)mem.ptr;

    // ----- Perform Directory Read -----
    rw_result result = syscalls::get_dir_contents(path, (char*)entries, BUFFER_BYTES);

    if (result.code != syscall_result_code::ok) {
        switch (result.code) {
            case syscall_result_code::not_found:
                con.writef("ls: directory not found: %s\n", path);
                break;

            case syscall_result_code::not_supported:
                con.writef("ls: not a directory: %s\n", path);
                break;

            default:
                con.writef("ls: error reading directory: %s\n", path);
                break;
        }
        return 1;
    }

    // ----- Handle Zero Entries -----

    size_t count = result.length / sizeof(dirent);

    if (count == 0) {
        con.write("(empty directory)\n");
        return 0;
    }

    // ----- Sort -----

    sort_entries(entries, count);

    // ----- Display Results -----

    // Find longest name to align columns
    size_t max_name_len = 0;
    for (size_t i = 0; i < count; i++) {
        size_t len = memops::strlen(entries[i].name);
        if (len > max_name_len) {
            max_name_len = len;
        }
    }

    // Optional: clamp to avoid very wide filenames
    if (max_name_len > 40) {
        max_name_len = 40;
    }

    for (size_t i = 0; i < count; i++) {
    const bool is_dir = (entries[i].type == 1);

    // Tag
    if (is_dir) {
        con.write("[D] ");
    } else {
        con.write("[F] ");
    }

    // Name (optionally truncated if absurdly long)
    const char* name = entries[i].name;
    size_t name_len = memops::strlen(name);
    if (name_len > max_name_len) {
        name_len = max_name_len;
    }

    // Print the name itself
    for (size_t j = 0; j < name_len; j++) {
        char ch[2] = { name[j], 0 };
        con.write(ch);
    }

    // If it's a directory, show a trailing '/'
    if (is_dir) {
        con.write("/");
    }

    // Pad spaces up to column
    size_t printed_len = name_len + (is_dir ? 1 : 0);
    const size_t col_width = max_name_len + 2;  // +2 spacing
    if (printed_len < col_width) {
        for (size_t s = printed_len; s < col_width; s++) {
            con.write(" ");
        }
    }

    // File size (for files only)
    if (!is_dir) {
        con.write(" ");
        con.writef("%u bytes", (unsigned)entries[i].size);
    }

    con.write("\n");
}


    return 0;
}
