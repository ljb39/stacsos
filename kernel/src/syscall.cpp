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

/**
 * Rettrieves directory entries from a filesystem path.
 * 
 * @param path			Path string
 * @param buffer		Userspace memory pointer where the kernel will write an 
 * 						array of `dirent` structures
 * @param buffer_size	Size of the output buffer (bytes)
 */
static syscall_result do_get_dir_contents(const char *path,char *buffer,size_t buffer_size)
{
    //ensure pointers are valid and buffer is large enough to hold one entry.
    if (!path || !buffer || buffer_size < sizeof(dirent)) {
        return { syscall_result_code::invalid_argument, 0 };
    }

    //validate path string length. Cannot exceed 256 chars (including \0)
    const size_t MAX_PATH = 256;
    size_t n = 0;
    while (n < MAX_PATH && path[n] != '\0') n++;
    if (n == MAX_PATH) {
        return { syscall_result_code::invalid_argument, 0 };
    }

    // Resolve directory

	//lookup fat_node
    auto *node = (fat_node*) vfs::get().lookup(path);

	//path not found
    if (!node) return { syscall_result_code::not_found, 0 };

	//path exists, but is not a directory
    if (node->kind() != fs_node_kind::directory) return { syscall_result_code::not_supported, 0 };

	//load directory content
    node->load_directory();

	//num bytes written into the user buffer
    size_t offset = 0;

    for (auto child : node->children()) {

        if (child->name() == "." || child->name() == "..") continue;

		//ensure there is enough space for one more dirent 
        if (offset + sizeof(dirent) > buffer_size) return { syscall_result_code::buffer_overflow, offset };

        dirent e {};

		//copy file name, type and size into dirent
        size_t len = child->name().length();
        if (len >= MAX_FILENAME_LEN) len = MAX_FILENAME_LEN - 1;
        memops::memcpy(e.name, child->name().c_str(), len);
        e.name[len] = '\0';
        e.type = child->kind() == fs_node_kind::directory ? 1 : 0;
        e.size = e.type ? 0 : child->size();

		//copy struct into buffer and update offset
        memops::memcpy(buffer + offset, &e, sizeof(e));
        offset += sizeof(e);
    }

    return { syscall_result_code::ok, offset };
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

		return do_get_dir_contents(path, buffer, buffer_size);
    }

	default:
		dprintf("ERROR: unsupported syscall: %lx\n", index);
		return syscall_result { syscall_result_code::not_supported, 0 };
	}
}
