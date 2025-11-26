/* SPDX-License-Identifier: MIT */

/* StACSOS - Kernel
 *
 * Copyright (c) University of St Andrews 2024
 * Tom Spink <tcs6@st-andrews.ac.uk>
 */
#include <stacsos/kernel/arch/x86/cregs.h>
#include <stacsos/kernel/arch/x86/pio.h>
#include <stacsos/kernel/debug.h>
#include <stacsos/kernel/fs/vfs.h>
#include <stacsos/kernel/fs/fat.h>
#include <stacsos/kernel/mem/address-space.h>
#include <stacsos/kernel/obj/object-manager.h>
#include <stacsos/kernel/obj/object.h>
#include <stacsos/kernel/sched/process-manager.h>
#include <stacsos/kernel/sched/process.h>
#include <stacsos/kernel/sched/sleeper.h>
#include <stacsos/kernel/sched/thread.h>
#include <stacsos/syscalls.h>
#include <stacsos/dirent.h> 

using namespace stacsos;
using namespace stacsos::kernel;
using namespace stacsos::kernel::sched;
using namespace stacsos::kernel::obj;
using namespace stacsos::kernel::fs;
using namespace stacsos::kernel::mem;
using namespace stacsos::kernel::arch::x86;

static syscall_result do_open(process &owner, const char *path)
{
	auto node = vfs::get().lookup(path);
	if (node == nullptr) {
		return syscall_result { syscall_result_code::not_found, 0 };
	}

	auto file = node->open();
	if (!file) {
		return syscall_result { syscall_result_code::not_supported, 0 };
	}

	auto file_object = object_manager::get().create_file_object(owner, file);
	return syscall_result { syscall_result_code::ok, file_object->id() };
}

/**
 * 
 * @param request Pointer to a dirlist_request object
 * @param result Pointer to the dirlist_result object where the results are written.
 */
static syscall_result do_readdir(dirlist_request* request, dirlist_result* result)
{
    // Validate pointers
    if (!request || !result || !request->path || !request->buffer) {
        return syscall_result { syscall_result_code::not_found, 0 };
    }
    
    // Lookup the path through VFS
    auto node = vfs::get().lookup(request->path);
    
    if (!node) {
        dprintf("do_readdir: path not found: %s\n", request->path);
        return syscall_result { syscall_result_code::not_found, 0 };
    }
    
    // Check if it's a directory
    if (node->kind() != fs_node_kind::directory) {
        dprintf("do_readdir: not a directory: %s\n", request->path);
        return syscall_result { syscall_result_code::not_supported, 0 };
    }
    
    // Cast to fat_node to access children
    fat_node* dir = static_cast<fat_node*>(node);
    
    if (!dir) {
        dprintf("do_readdir: failed to cast to fat_node\n");
        return syscall_result { syscall_result_code::not_supported, 0 };
    }
    
    // Initialize result
    result->entries_read = 0;
    result->has_more = false;
    
    // Iterate through children
    size_t index = 0;
    const auto& children = dir->children();
    
    for (auto child : children) {
        if (index >= request->buffer_count) {
            result->has_more = true;
            break;
        }
        
        // Build directory entry
        dirent entry;
        
        // Copy name safely
        const string& child_name = child->name();
        size_t name_len = child_name.length();
        if (name_len >= MAX_FILENAME_LEN) {
            name_len = MAX_FILENAME_LEN - 1;
        }
        
        for (size_t i = 0; i < name_len; i++) {
            entry.name[i] = child_name[i];
        }
        entry.name[name_len] = '\0';

        // Fill remaining characters with null for safety
        for (size_t i = name_len + 1; i < MAX_FILENAME_LEN; i++) {
            entry.name[i] = '\0';
        }
        
        // Set type
        entry.type = (child->kind() == fs_node_kind::file) ? 
                     dirent_type::DT_FILE : 
                     dirent_type::DT_DIR;
        
        // Set size (only for files)
        entry.size = (entry.type == dirent_type::DT_FILE) ?
                     child->size() : 0;

        dirent* dest = &request->buffer[index];

        // Copy name
        for (size_t i = 0; i < MAX_FILENAME_LEN; i++) {
            dest->name[i] = entry.name[i];
        }

        // Copy simple fields
        dest->type = entry.type;
        dest->size = entry.size;

        index++;
    }
    
    result->entries_read = index;
    
    return syscall_result { syscall_result_code::ok, index };
}

static syscall_result operation_result_to_syscall_result(operation_result &&o)
{
	syscall_result_code rc = (syscall_result_code)o.code;
	return syscall_result { rc, o.data };
}

extern "C" syscall_result handle_syscall(syscall_numbers index, u64 arg0, u64 arg1, u64 arg2, u64 arg3)
{
	auto &current_thread = thread::current();
	auto &current_process = current_thread.owner();

	// dprintf("SYSCALL: %u %x %x %x %x\n", index, arg0, arg1, arg2, arg3);

	switch (index) {
	case syscall_numbers::exit:
		current_process.stop();
		return syscall_result { syscall_result_code::ok, 0 };

	case syscall_numbers::set_fs:
		stacsos::kernel::arch::x86::fsbase::write(arg0);
		return syscall_result { syscall_result_code::ok, 0 };

	case syscall_numbers::set_gs:
		stacsos::kernel::arch::x86::gsbase::write(arg0);
		return syscall_result { syscall_result_code::ok, 0 };

	case syscall_numbers::open:
		return do_open(current_process, (const char *)arg0);

	case syscall_numbers::close:
		object_manager::get().free_object(current_process, arg0);
		return syscall_result { syscall_result_code::ok, 0 };

	case syscall_numbers::write: {
		auto o = object_manager::get().get_object(current_process, arg0);
		if (!o) {
			return syscall_result { syscall_result_code::not_found, 0 };
		}

		return operation_result_to_syscall_result(o->write((const void *)arg1, arg2));
	}

	case syscall_numbers::pwrite: {
		auto o = object_manager::get().get_object(current_process, arg0);
		if (!o) {
			return syscall_result { syscall_result_code::not_found, 0 };
		}

		return operation_result_to_syscall_result(o->pwrite((const void *)arg1, arg2, arg3));
	}

	case syscall_numbers::read: {
		auto o = object_manager::get().get_object(current_process, arg0);
		if (!o) {
			return syscall_result { syscall_result_code::not_found, 0 };
		}

		return operation_result_to_syscall_result(o->read((void *)arg1, arg2));
	}

	case syscall_numbers::pread: {
		auto o = object_manager::get().get_object(current_process, arg0);
		if (!o) {
			return syscall_result { syscall_result_code::not_found, 0 };
		}

		return operation_result_to_syscall_result(o->pread((void *)arg1, arg2, arg3));
	}

	case syscall_numbers::ioctl: {
		auto o = object_manager::get().get_object(current_process, arg0);
		if (!o) {
			return syscall_result { syscall_result_code::not_found, 0 };
		}

		return operation_result_to_syscall_result(o->ioctl(arg1, (void *)arg2, arg3));
	}

	case syscall_numbers::alloc_mem: {
		auto rgn = current_thread.owner().addrspace().alloc_region(PAGE_ALIGN_UP(arg0), region_flags::readwrite, true);

		return syscall_result { syscall_result_code::ok, rgn->base };
	}

	case syscall_numbers::start_process: {
		dprintf("start process: %s %s\n", arg0, arg1);

		auto new_proc = process_manager::get().create_process((const char *)arg0, (const char *)arg1);
		if (!new_proc) {
			return syscall_result { syscall_result_code::not_found, 0 };
		}

		new_proc->start();
		return syscall_result { syscall_result_code::ok, object_manager::get().create_process_object(current_process, new_proc)->id() };
	}

	case syscall_numbers::wait_for_process: {
		// dprintf("wait process: %lu\n", arg0);

		auto process_object = object_manager::get().get_object(current_process, arg0);
		if (!process_object) {
			return syscall_result { syscall_result_code::not_found, 0 };
		}

		return operation_result_to_syscall_result(process_object->wait_for_status_change());
	}

	case syscall_numbers::start_thread: {
		auto new_thread = current_thread.owner().create_thread((u64)arg0, (void *)arg1);
		new_thread->start();

		return syscall_result { syscall_result_code::ok, object_manager::get().create_thread_object(current_process, new_thread)->id() };
	}

	case syscall_numbers::stop_current_thread: {
		current_thread.stop();
		asm volatile("int $0xff");

		return syscall_result { syscall_result_code::ok, 0 };
	}

	case syscall_numbers::join_thread: {
		auto thread_object = object_manager::get().get_object(current_process, arg0);
		if (!thread_object) {
			return syscall_result { syscall_result_code::not_found, 0 };
		}

		return operation_result_to_syscall_result(thread_object->join());
	}

	case syscall_numbers::sleep: {
		sleeper::get().sleep_ms(arg0);
		return syscall_result { syscall_result_code::ok, 0 };
	}

	case syscall_numbers::poweroff: {
		pio::outw(0x604, 0x2000);
		return syscall_result { syscall_result_code::ok, 0 };
	}

	case syscall_numbers::readdir: {
		return do_readdir((dirlist_request*)arg0, (dirlist_result*)arg1);
	}

	default:
		dprintf("ERROR: unsupported syscall: %lx\n", index);
		return syscall_result { syscall_result_code::not_supported, 0 };
	}
}
