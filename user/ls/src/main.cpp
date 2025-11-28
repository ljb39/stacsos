
/** StACSOS - ls utility
 * 
 * Loell Jean Barit <ljb39@st-andrews.ac.uk>
 */

#include <stacsos/dirent.h>
#include <stacsos/user-syscall.h>
#include <stacsos/console.h>
#include <stacsos/memops.h>

using namespace stacsos;

struct ls_options {
    bool long_format = false;
    bool recursive = false;
    const char* path = "/";
};

struct readdir_result {
    size_t count = 0;
    syscall_result_code code;
    const char* error_msg = "";
};

/**
 * Parses command-line flags and a path from a raw command-line string.
 * Does not support conjoined flags e.g. -lr 
 * 
 * Supported options: 
 *      -l long listing format
 *      -r recursive directory listing
 * 
 * @param cmdline   Raw command-line string after the command name
 * @param opts      Structure holding all the detected options
 */
static void parse_arguments(const char* cmdline, ls_options& opts)
{
    if (!cmdline) cmdline = "";

    //skips leading spaces
    while (*cmdline == ' ') cmdline++;

    //parse flags beginning with '-'
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

        //skip spaces until the next arg is detected  
        while (*cmdline == ' ') cmdline++;
    }

    //if the next arg is not a flag, it is assumed to be the flag
    if (*cmdline != '\0')
        opts.path = cmdline;
}

/**
 * 
 */
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


static readdir_result read_directory(const char* path, dirent* entries, size_t max_entries)
{
    auto& con = console::get();

    readdir_result result; 

    rw_result r = syscalls::get_dir_contents(path, (char*)entries,
                                             max_entries * sizeof(dirent));
    
    result.code = r.code;                                       

    switch(r.code){

        case syscall_result_code::invalid_argument:
            result.error_msg = "\nls: invalid path.";
            break;
        case syscall_result_code::not_found:
            result.error_msg = "\nls : path does not exist.";
            break;
        case syscall_result_code::not_supported:
            result.error_msg = "\nls: not a directory";
            break;
        default: 
            result.count = r.length / sizeof(dirent);
            break;
    }

    return result;
}

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

    for (size_t i = 0; i < count; i++) {
        bool is_dir = (entries[i].type == 1);

        con.write(is_dir ? "[D] " : "[F] ");

        size_t name_len = memops::strlen(entries[i].name);

        for (size_t j = 0; j < name_len; j++) {
            char buf[2] = { entries[i].name[j], 0 };
            con.write(buf);
        }

        if (is_dir) con.write("/");

        if (!is_dir)
            con.writef("%u bytes", (unsigned)entries[i].size);

        con.write("\n");
    }
}

static void ls_recursive(const char* path, const ls_options& opts)
{
    console::get().writef("\n%s:\n", path);

    const size_t BUFSZ = 64 * sizeof(dirent);
    auto mem = syscalls::alloc_mem(BUFSZ);
    dirent* entries = (dirent*)mem.ptr;

    rw_result result = syscalls::get_dir_contents(path, (char*)entries, BUFSZ);
    size_t count = result.length / sizeof(dirent);

    sort_entries(entries, count);

    if (opts.long_format) print_long(entries, count);
    else print_short(entries, count);

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

    ls_options opts;

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

    readdir_result result = read_directory(opts.path, entries, MAX);
    
    if (result.code != syscall_result_code::ok) {
        con.write(result.error_msg);
        return 1;
    }

    sort_entries(entries, result.count);

    if (opts.long_format) print_long(entries, result.count);
    else print_short(entries, result.count);

    return 0;
}


