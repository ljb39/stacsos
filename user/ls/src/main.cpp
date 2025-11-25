
/** StACSOS - ls utility
 * 
 * Loell Jean Barit <ljb39@st-andrews.ac.uk>
 */

#include <stacsos/console.h>
#include <stacsos/memops.h>
#include <stacsos/objects.h>

using namespace stacsos;

int main(const char *cmdline) 
{

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
    
    console::get().writef("ls: Directory: %s\n", path);
    console::get().writef("ls: Long format: %s\n", long_format ? "yes" : "no");
    
    // TODO: Call readdir syscall here
    console::get().write("ls: Syscall not implemented yet\n");
    
    return 0;
}