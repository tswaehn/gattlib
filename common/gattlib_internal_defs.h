/*
 * SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-or-later
 *
 * Copyright (c) 2021-2024, Olivier Martin <olivier@labapart.org>
 */

#ifndef __GATTLIB_INTERNAL_DEFS_H__
#define __GATTLIB_INTERNAL_DEFS_H__

#include <stdbool.h>
#include <glib.h>

#if defined(WITH_PYTHON)
	#include <Python.h>
#endif

#include "gattlib.h"

#if defined(WITH_PYTHON)
struct gattlib_python_args {
	PyObject* callback;
	PyObject* args;
};
#endif

struct gattlib_handler {
	union {
		gattlib_discovered_device_t discovered_device;
		gatt_connect_cb_t connection_handler;
		gattlib_event_handler_t notification_handler;
		gattlib_disconnection_handler_t disconnection_handler;
		void (*callback)(void);
	} callback;

	void* user_data;
	// We create a thread to ensure the callback is not blocking the mainloop
	GThread *thread;
	// The mutex ensures the callbacks is not being freed while being called
	// We use a recursive mutex to be able to disable BLE scan from 'on_discovered_device'
	// when we want to connect to the discovered device.
	// Note: The risk is that we are actually realising the handle from the one we are executing
	GRecMutex mutex;
	// Thread pool
	GThreadPool *thread_pool;
#if defined(WITH_PYTHON)
	// In case of Python callback and argument, we keep track to free it when we stopped to discover BLE devices
	void* python_args;
#endif
};

struct _gattlib_device {
	// Context specific to the backend implementation (eg: dbus backend)
	void* context;

	GMutex connection_mutex;

	struct {
		// Used by gattlib_disconnection when we want to wait for the disconnection to be effective
		GCond condition;
		// Mutex used for disconnection_condition synchronization
		GMutex lock;
		// Used to avoid spurious or stolen wakeup
		bool value;
	} disconnection_wait;

	struct gattlib_handler on_connection;
	struct gattlib_handler notification;
	struct gattlib_handler indication;
	struct gattlib_handler on_disconnection;
};

void gattlib_handler_dispatch_to_thread(struct gattlib_handler* handler, void (*python_callback)(),
		GThreadFunc thread_func, const char* thread_name, void* (*thread_args_allocator)(va_list args), ...);
void gattlib_handler_free(struct gattlib_handler* handler);
bool gattlib_has_valid_handler(struct gattlib_handler* handler);

void gattlib_notification_device_thread(gpointer data, gpointer user_data);

/**
 * Clean GATTLIB connection on disconnection
 *
 * This function is called by the disconnection callback to always be called on explicit
 * and implicit disconnection.
 */
void gattlib_connection_free(gatt_connection_t* connection);

#if defined(WITH_PYTHON)
// Callback used by Python to create arguments used by native callback
void* gattlib_python_callback_args(PyObject* python_callback, PyObject* python_args);

/**
 * These functions are called by Python wrapper
 */
void gattlib_discovered_device_python_callback(void *adapter, const char* addr, const char* name, void *user_data);
void gattlib_connected_device_python_callback(void *adapter, const char *dst, gatt_connection_t* connection, int error, void* user_data);
void gattlib_disconnected_device_python_callback(gatt_connection_t* connection, void *user_data);
void gattlib_notification_device_python_callback(const uuid_t* uuid, const uint8_t* data, size_t data_length, void* user_data);
#endif

#endif
