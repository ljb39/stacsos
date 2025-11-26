
/** StACSOS - ls utility
 * 
 * Loell Jean Barit <ljb39@st-andrews.ac.uk>
 */

#include <stacsos/console.h>
#include <stacsos/memops.h>
#include <stacsos/user-syscall.h>
#include <stacsos/dirent.h>

using namespace stacsos;

int main(const char *cmdline) 
{
    auto& con = console::get();

    if (!cmdline || memops::strlen(cmdline) == 0) {
		console::get().write("error: usage: ls [-l] <path>\n");
		return 1;
	}

    /**
     * Parsing command line arguments.
     * Adapted from the implementation found in user/cat/src
     */
    bool long_format = false;
    const char* path = nullptr;
    
    //Parsing flags
    while(*cmdline) {
        //Parse flags
        if(*cmdline == '-') {
            cmdline++;

            if (*cmdline++ == 'l') long_format = true;
            else{
                console::get().write("error: usage: ls [-l] <path>\n");
                return 1;
            }
        } else break;
    }

    while (*cmdline == ' ') {
		cmdline++;
	};

    //Parsing Path

    if (*cmdline != '\0') {
        path = cmdline;  // Point to the path string
    } else {
        // No path provided - show error or use default
        console::get().write("error: usage: ls [-l] <path>\n");
        return 1;
    }
    
    // console::get().writef("ls: Directory: %s\n", path);
    // console::get().writef("ls: Long format: %s\n", long_format ? "yes" : "no");
    
    // // TODO: Call readdir syscall here
    // console::get().write("ls: Syscall not implemented yet\n");

    const size_t BUFFER_SIZE = 64;
    dirent entries[BUFFER_SIZE];

    // Prepare request structure
    dirlist_request request;
    request.path = path;
    request.buffer = entries;
    request.buffer_count = BUFFER_SIZE;
    
    // Prepare result structure
    dirlist_result result;
    
    // Make the syscall!
    auto syscall_result = syscalls::readdir(&request, &result);
    
    // Check for errors
    if (syscall_result.code != syscall_result_code::ok) {
        con.writef("error: cannot read directory '%s'\n", path);
        return 1;
    }
    
    // Check if no entries found
    if (result.entries_read == 0) {
        con.writef("(empty directory)\n");
        return 0;
    }
    
    // Display results
    for (size_t i = 0; i < result.entries_read; i++) {
        if (long_format) {
            // Long format: [F] filename    12345
            char type = (entries[i].type == dirent_type::DT_FILE) ? 'F' : 'D';
            con.writef("[%c] %-30s", type, entries[i].name);
            
            if (entries[i].type == dirent_type::DT_FILE) {
                con.writef(" %lu", entries[i].size);
            }
            con.writef("\n");
        } else {
            // Short format: just filename
            con.writef("%s\n", entries[i].name);
        }
    }
    
    // Optionally warn if directory had more entries than buffer could hold
    if (result.has_more) {
        con.writef("... (more entries not shown)\n");
    }
    
    return 0;
}