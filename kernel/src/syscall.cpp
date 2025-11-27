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
#include <stacsos/kernel/fs/fs-node.h>
#include <stacsos/kernel/mem/address-space.h>
#include <stacsos/kernel/obj/object-manager.h>
#include <stacsos/kernel/obj/object.h>
#include <stacsos/kernel/sched/process-manager.h>
#include <stacsos/kernel/sched/process.h>
#include <stacsos/kernel/sched/sleeper.h>
#include <stacsos/kernel/sched/thread.h>
#include <stacsos/syscalls.h>
#include <stacsos/memops.h>
#include <stacsos/dirent.h>
#include <stacsos/kernel/debug.h>

using namespace stacsos;
using namespace stacsos::kernel;
using namespace stacsos::kernel::sched;
using namespace stacsos::kernel::obj;
using namespace stacsos::kernel::fs;
using namespace stacsos::kernel::mem;
using namespace stacsos::kernel::arch::x86;


static syscall_result do_get_dir_contents(process &owner, const char *path, char *buffer, size_t buffer_size)
{
	dprintf("\n do_get_dir_contents entered.");

    fat_node* dir_node = (fat_node*)vfs::get().lookup(path);

	dprintf("\n[get_dir_contents] lookup('%s') returned %p", path, dir_node);

	if (!dir_node || dir_node->kind() != fs_node_kind::directory) {
		dprintf("\ninvalid path");
		return syscall_result { syscall_result_code::not_found, 0 };  // If path is invalid
	}
	if (dir_node->kind() != fs_node_kind::directory) {
		dprintf("\npath does not lead to a directory");
    	return syscall_result { syscall_result_code::not_supported, 0 };  // Path is not a directory
	}


    // Load the children of the directory
    dir_node->load_directory();
	dprintf("\nDirectory children loaded.");

    // Copy the directory content names to the buffer using a structured format (dirent)
    size_t offset = 0;
    for (auto child : dir_node->children()) {
		if (child->name() == "." || child->name() == "..") continue;

        // Check if the buffer is large enough to hold the entry
		dprintf("\n[get_dir_contents] child name = '%s' \n", child->name().c_str());
        if (offset + sizeof(dirent) > buffer_size) {
            return syscall_result { syscall_result_code::buffer_overflow, 0 };  // Not enough space in the buffer
        }

        // Create a dirent structure for the child entry
        dirent entry;
        size_t name_len = child->name().length();
        if (name_len >= sizeof(entry.name)) {
            name_len = sizeof(entry.name) - 1;  // Ensure truncation if name is too long
        }
        memops::strncpy(entry.name, child->name().c_str(), name_len);
        entry.name[name_len] = '\0';  // Ensure null-termination
		dprintf("[KERNEL] entry.name = '%s'\n", entry.name);


        // Set type: 1 for directory, 0 for file
        entry.type = (child->kind() == fs_node_kind::directory) ? 1 : 0;

        // Set size for files only (directories have size 0)
        entry.size = (entry.type == 0) ? child->size() : 0;

        // Copy the entry into the buffer
        memops::memcpy(buffer + offset, &entry, sizeof(entry));
		dprintf("[KERNEL] wrote dirent of %s at user addr %p\n",
				entry.name,
				buffer + offset);
        offset += sizeof(entry);
    }

    return syscall_result { syscall_result_code::ok, offset };  // Return the number of bytes copied
}


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

static syscall_result operation_result_to_syscall_result(operation_result &&o)
{
	syscall_result_code rc = (syscall_result_code)o.code;
	return syscall_result { rc, o.data };
}

extern "C" syscall_result handle_syscall(syscall_numbers index, u64 arg0, u64 arg1, u64 arg2, u64 arg3)
{
	// dprintf("\nHandle syscall entered: %llu \n", static_cast<u64>(index));
	
	auto &current_thread = thread::current();

	auto &current_process = current_thread.owner();

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

	case syscall_numbers::get_dir_contents: {
		const char* path = (const char*)arg0;  // Directory path (from arg0)
		char* buffer = (char*)arg1;            // Buffer to store the directory contents
		size_t buffer_size = (size_t)arg2;     // Size of the buffer (arg2)

		return do_get_dir_contents(current_process, path, buffer, buffer_size);
    }

	default:
		dprintf("ERROR: unsupported syscall: %lx\n", index);
		return syscall_result { syscall_result_code::not_supported, 0 };
	}
}
