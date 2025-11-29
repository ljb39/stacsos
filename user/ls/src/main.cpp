
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
 * Options parsed from command line.
 */
struct ls_options {
    bool long_format = false;
    bool recursive = false;
    const char* path = "/";
};

/**
 * Contains the results for reading the given directory.
 * Handles both normal results and errors.
 */
struct readdir_result {
    size_t count = 0; // Default value
    syscall_result_code code; //Result code of the system call
    const char* error_msg = ""; //Remains empty for normal results
};

/**
 * Parse ls command-line flags and extract a directory path.
 *
 * Supports:
 *      -l    long listing format
 *      -r    recursive listing
 *
 * Notes:
 *      - Flags must not be combined (i.e., "-lr" not supported).
 *      - The first non-flag argument is taken as the path.
 * 
 * @param cmdline   Raw argument string passed by the shell
 * @param opts      Struct to populate with parsed options
 */
static void parse_arguments(const char* cmdline, ls_options& opts)
{
    if (!cmdline) cmdline = "";

    //skips leading spaces
    while (*cmdline == ' ') cmdline++;

    //parse flags beginning with '-'
    while (*cmdline == '-') {
        cmdline++;

        //consume flag chars until space or end of line
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

    //if any non-flag argument remains, use it as the path
    if (*cmdline != '\0')
        opts.path = cmdline;
}

/**
 * Calls the get_dir_contents syscall and converts its result into a
 * user-friendly structure.
 *
 * @param path         Directory path to read
 * @param entries      Caller-allocated buffer of dirent objects
 * @param max_entries  Maximum number of entries buffer can hold
 * @return             readdir_result containing count or an error
 */
static readdir_result read_directory(const char* path, dirent* entries, size_t max_entries)
{
    auto& con = console::get();

    readdir_result result; 

    rw_result r = syscalls::get_dir_contents(path, (char*)entries,
                                             max_entries * sizeof(dirent));
    
    result.code = r.code;                                       

    switch(r.code){

        case syscall_result_code::invalid_argument:
            result.error_msg = "\nls: invalid path.\n";
            break;
        case syscall_result_code::not_found:
            result.error_msg = "\nls : path does not exist.\n";
            break;
        case syscall_result_code::not_supported:
            result.error_msg = "\nls: not a directory. \n";
            break;
        default: 
            //successful read
            result.count = r.length / sizeof(dirent);
            break;
    }

    return result;
}

/**
 * Sorts direcotry entries alphabetically by name (ascending)
 * 
 * @param entries   List of entries
 * @param count     Number of entries
 */
static void sort_entries(dirent* entries, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        for (size_t j = i + 1; j < count; j++) {
            if (memops::strcmp(entries[i].name, entries[j].name) > 0) {
                //full struct swap
                dirent tmp;
                memops::memcpy(&tmp, &entries[i], sizeof(dirent));
                memops::memcpy(&entries[i], &entries[j], sizeof(dirent));
                memops::memcpy(&entries[j], &tmp, sizeof(dirent));
            }
        }
    }
}

/**
 * Prints directory entries in the default short format.
 * 
 * @param entries   List of entries
 * @param count     Number of entries
 */
static void print_short(dirent* entries, size_t count)
{
    auto& con = console::get();
    for (size_t i = 0; i < count; i++) {
        //Directory
        if (entries[i].type == 1) con.writef("%s/\n", entries[i].name);
        //File
        else con.writef("%s\n", entries[i].name);
    }
}

/**
 * Prints directory entries in the default short format.
 * 
 * Example:
 *     [D] folder/
 *     [F] file.txt  123 bytes
 * 
 * @param entries   List of entries
 * @param count     Number of entries
 */
static void print_long(dirent* entries, size_t count)
{
    auto& con = console::get();

    for (size_t i = 0; i < count; i++) {
        bool is_dir = (entries[i].type == 1);

        const char* type = is_dir ? "[D] " : "[F] ";

        con.writef("%s  %s%s %u bytes\n", type, 
                                        entries[i].name, 
                                        is_dir ? "/" : "", 
                                        (unsigned)entries[i].size);
    }
}

/**
 * Builds a new filesystem path from parent + child. 
 * Used during recursive traveral. 
 * 
 * @param out       Output buffer where the final path is stored
 * @param parent    Existing directory path
 * @param child     Name of the subdirectory/file inside parent
 * @param max_len   Total size of output buffer. Prevents overflow in case
 *                  parent or child name is too long.
 */
static void build_path(char* out, const char* parent, const char* child, size_t max_len)
{
    //get lengths for parent and child
    size_t plen = memops::strlen(parent);
    size_t clen = memops::strlen(child);

    //truncate if it is too long
    if (plen >= max_len - 1) plen = max_len - 1;
    //copy parent into buffer
    memops::memcpy(out, parent, plen);
    out[plen] = '\0'; 

    //current position in the buffer after writing the parent
    size_t pos = plen;

    //add slash if needed
    if (pos < max_len - 1 && parent[plen - 1] != '/') {
        out[pos++] = '/';
    }

    //append child
    size_t space = max_len - pos - 1; //space remaining in the buffer
    if (clen > space) clen = space; //truncate if necessary

    //copy into buffer
    memops::memcpy(out + pos, child, clen);
    out[pos + clen] = '\0';
}

/**
 * Recursively lists a directory and all its children. 
 * 
 * @param path  Starting directory
 * @param opts  Command-line flags
 */
static int ls_recursive(const char* path, const ls_options& opts)
{
    auto& con = console::get();

    const size_t MAX = 64;
    const size_t BUF = MAX * sizeof(dirent);

    auto mem = syscalls::alloc_mem(BUF);
    if (mem.code != syscall_result_code::ok) {
        con.write("ls: alloc_mem failed\n");
        return 1;
    }

    dirent* entries = (dirent*)mem.ptr;

    readdir_result r = read_directory(path, entries, MAX);

    if (r.code != syscall_result_code::ok) {
            con.write(r.error_msg);
            return 1;
    }

    //sort directory alphabetically before printing
    sort_entries(entries, r.count);

    if (opts.long_format) print_long(entries, r.count);
    else print_short(entries, r.count);

    // Recurse into subdirectories
    for (size_t i = 0; i < r.count; i++) {
        if (entries[i].type == 1) {
            /**
             * if it is a directory, construct a new path that appends child to
             * current path
             */
            char newpath[128];
            build_path(newpath, path, entries[i].name, 128);
            //print section header for the directory
            console::get().writef("\n%s:\n", path);
            ls_recursive(newpath, opts);
        }
    }

    return 0;
}

int main(const char* cmdline)
{
    auto& con = console::get();

    ls_options opts;

    parse_arguments(cmdline, opts);

    if (opts.recursive) {
        return ls_recursive(opts.path, opts);
    } else {
        //allocate buffer for up to 64 directory entries
        const size_t MAX = 64;
        const size_t BUF = MAX * sizeof(dirent);

        auto mem = syscalls::alloc_mem(BUF);
        if (mem.code != syscall_result_code::ok) {
            con.write("ls: alloc_mcem failed\n");
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

}


