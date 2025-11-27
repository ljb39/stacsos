
/** StACSOS - ls utility
 * 
 * Loell Jean Barit <ljb39@st-andrews.ac.uk>
 */

#include <stacsos/dirent.h>
#include <stacsos/user-syscall.h>
#include <stacsos/console.h>
#include <stacsos/memops.h>

using namespace stacsos;

static void sort_entries(dirent* entries, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        for (size_t j = i + 1; j < count; j++) {
            if (memops::strcmp(entries[i].name, entries[j].name) > 0) {
                dirent tmp;
                memops::memcpy(&tmp, &entries[i], sizeof(dirent));
                memops::memcpy(&entries[i], &entries[j], sizeof(dirent));
                memops::memcpy(&entries[j], &tmp, sizeof(dirent));
            }
        }
    }
}

static void print_short(dirent* entries, size_t count)
{
    auto& con = console::get();
    for (size_t i = 0; i < count; i++) {
        if (entries[i].type == 1)
            con.writef("%s/\n", entries[i].name);
        else
            con.writef("%s\n", entries[i].name);
    }
}

static void print_long(dirent* entries, size_t count)
{
    auto& con = console::get();

    size_t max_name = 0;
    for (size_t i = 0; i < count; i++) {
        size_t len = memops::strlen(entries[i].name);
        if (len > max_name) max_name = len;
    }
    if (max_name > 40) max_name = 40;

    for (size_t i = 0; i < count; i++) {
        bool is_dir = (entries[i].type == 1);

        con.write(is_dir ? "[D] " : "[F] ");

        size_t name_len = memops::strlen(entries[i].name);
        if (name_len > max_name) name_len = max_name;

        for (size_t j = 0; j < name_len; j++) {
            char buf[2] = { entries[i].name[j], 0 };
            con.write(buf);
        }

        if (is_dir) con.write("/");

        size_t printed = name_len + (is_dir ? 1 : 0);
        size_t col_width = max_name + 2;
        while (printed < col_width) { con.write(" "); printed++; }

        if (!is_dir)
            con.writef("%u bytes", (unsigned)entries[i].size);

        con.write("\n");
    }
}

static const char* parse_args(const char* cmdline, bool& long_format)
{
    if (!cmdline) return "/";

    while (*cmdline == ' ') cmdline++;

    if (*cmdline == '-') {
        cmdline++;
        if (*cmdline == 'l') {
            long_format = true;
            cmdline++;
        }
        while (*cmdline == ' ') cmdline++;
    }

    return (*cmdline == '\0') ? "/" : cmdline;
}

int main(const char* cmdline)
{
    auto& con = console::get();
    con.write("\e\x0e");

    bool long_format = false;
    const char* path = parse_args(cmdline, long_format);

    const size_t ENTRY_COUNT = 64;
    const size_t BUFFER_SIZE = ENTRY_COUNT * sizeof(dirent);

    auto mem = syscalls::alloc_mem(BUFFER_SIZE);
    if (mem.code != syscall_result_code::ok) {
        con.write("ls: alloc_mem failed\n");
        return 1;
    }

    dirent* entries = (dirent*)mem.ptr;
    rw_result r = syscalls::get_dir_contents(path, (char*)entries, BUFFER_SIZE);

    if (r.code != syscall_result_code::ok) {
        con.writef("ls: cannot read: %s\n", path);
        return 1;
    }

    size_t count = r.length / sizeof(dirent);
    if (count == 0) {
        con.write("(empty directory)\n");
        return 0;
    }

    sort_entries(entries, count);

    if (long_format)
        print_long(entries, count);
    else
        print_short(entries, count);

    return 0;
}
