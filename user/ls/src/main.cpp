
/** StACSOS - ls utility
 * 
 * Loell Jean Barit <ljb39@st-andrews.ac.uk>
 */

#include <stacsos/dirent.h>
#include <stacsos/user-syscall.h>
#include <stacsos/console.h>
#include <stacsos/memops.h>

using namespace stacsos;

struct LsOptions {
    bool long_format = false;
    bool recursive = false;
    const char* path = "/";
};

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

static void parse_arguments(const char* cmdline, LsOptions& opts)
{
    if (!cmdline) cmdline = "";
    while (*cmdline == ' ') cmdline++;

    while (*cmdline == '-') {
        cmdline++;
        while (*cmdline && *cmdline != ' ') {
            switch (*cmdline) {
                case 'l': opts.long_format = true; break;
                case 'r': opts.recursive   = true; break;

                default:
                    console::get().writef("ls: unknown option '%c'\n", *cmdline);
                    break;
            }
            cmdline++;
        }

        while (*cmdline == ' ') cmdline++;
    }

    if (*cmdline != '\0')
        opts.path = cmdline;
}

static size_t read_directory(const char* path, dirent* entries, size_t max_entries)
{
    auto& con = console::get();

    rw_result r = syscalls::get_dir_contents(path, (char*)entries,
                                             max_entries * sizeof(dirent));
    
    switch(r.code){

        case syscall_result_code::invalid_argument:
            con.write("ls: invalid path.");
            return 0;

        case syscall_result_code::not_found:
            con.write("ls : path does not exist.");
            return 0;

        case syscall_result_code::not_supported:
            con.write("ls: not a directory");
            return 0;

        default: 
            size_t num_entries = r.length / sizeof(dirent);
            
            if (num_entries == 0){
                con.write("empty directory");
                return 0;
            } else return num_entries;
    }
}

static void build_path(char* out, const char* parent, const char* child, size_t max_len)
{
    size_t plen = memops::strlen(parent);
    size_t clen = memops::strlen(child);

    // Copy parent safely
    if (plen >= max_len - 1) plen = max_len - 1;
    memops::memcpy(out, parent, plen);
    out[plen] = '\0';

    size_t pos = plen;

    // Add slash if needed
    if (pos < max_len - 1 && parent[plen - 1] != '/') {
        out[pos++] = '/';
    }

    // Append child
    size_t space = max_len - pos - 1;
    if (clen > space) clen = space;

    memops::memcpy(out + pos, child, clen);
    out[pos + clen] = '\0';
}



static void ls_recursive(const char* path, const LsOptions& opts)
{
    console::get().writef("\n%s:\n", path);

    const size_t BUFSZ = 64 * sizeof(dirent);
    auto mem = syscalls::alloc_mem(BUFSZ);
    dirent* entries = (dirent*)mem.ptr;

    rw_result result = syscalls::get_dir_contents(path, (char*)entries, BUFSZ);
    size_t count = result.length / sizeof(dirent);

    sort_entries(entries, count);

    if (opts.long_format)
        print_long(entries, count);
    else
        print_short(entries, count);

    // Recurse into subdirectories
    for (size_t i = 0; i < count; i++) {
        if (entries[i].type == 1) {  // directory
            char newpath[128];
            build_path(newpath, path, entries[i].name, 128);

            ls_recursive(newpath, opts);
        }
    }
}




int main(const char* cmdline)
{
    console::get().write("\e\x0e");
    auto& con = console::get();

    LsOptions opts;
    parse_arguments(cmdline, opts);

    if (opts.recursive) {
        ls_recursive(opts.path, opts);
        return 0;
    }

    const size_t MAX = 64;
    const size_t BUF = MAX * sizeof(dirent);

    auto mem = syscalls::alloc_mem(BUF);
    if (mem.code != syscall_result_code::ok) {
        con.write("ls: alloc_mem failed\n");
        return 1;
    }

    dirent* entries = (dirent*)mem.ptr;

    size_t count = read_directory(opts.path, entries, MAX);
    
    if (count == 0) return 0;

    sort_entries(entries, count);

    if (opts.long_format)
        print_long(entries, count);
    else
        print_short(entries, count);

    return 0;
}


