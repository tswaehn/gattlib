/*
 *
 *  GattLib - GATT Library
 *
 *  Copyright (C) 2021  Olivier Martin <olivier@labapart.org>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <sys/resource.h>

#ifdef GATTLIB_LOG_BACKEND_SYSLOG
#include <syslog.h>
#include <unistd.h>
#include <iostream>

#endif

#include "gattlib.h"

#define BLE_SCAN_TIMEOUT   4

typedef void (*ble_discovered_device_t)(const char* addr, const char* name);

// We use a mutex to make the BLE connections synchronous
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

LIST_HEAD(listhead, connection_t) g_ble_connections;
struct connection_t {
	pthread_t thread;
	char* addr;
    void * adapter;
	LIST_ENTRY(connection_t) entries;
};

void printMemoryUsage() {
    #define LOG_PREFIX "StatusService>"
    const uint32_t maxAllowedHeapKb= 50000;

    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        static uint32_t lastHeapSize = 0;
        uint32_t currentHeapSize = usage.ru_maxrss;

        std::string msg = LOG_PREFIX "<stats> Memory usage: " + std::to_string(currentHeapSize) + " kb; stack " + std::to_string(usage.ru_isrss) + "kb";
        if (currentHeapSize != lastHeapSize){
            // print updates
            auto delta = (int32_t)(currentHeapSize - lastHeapSize);
            msg += " +" + std::to_string(delta) + "kb";
            std::cout << msg << std::endl;
        } else {
            // nothing changed
        }
        lastHeapSize = currentHeapSize;

        //
        if (currentHeapSize >= maxAllowedHeapKb){
            std::cout << LOG_PREFIX"reached maximum of allowed heap size -- fire exit of application" << std::endl;
            std::cerr << LOG_PREFIX << "reached maximum of allowed heap size -- fire exit of application" << std::endl;
            exit(EXIT_FAILURE);
        }
    } else {
        std::cout  << LOG_PREFIX "Failed to get memory usage" << std::endl;
    }
}

static void *ble_connect_device(void *arg) {
	struct connection_t *connection = (struct connection_t*) arg;
	char* addr = connection->addr;
    void * adapter = connection->adapter;
	gatt_connection_t* gatt_connection;


        printf("------------START %s ---------------\n", addr);

        gatt_connection = gattlib_connect(adapter, addr, GATTLIB_CONNECTION_OPTIONS_LEGACY_DEFAULT, 0);
        if (gatt_connection == NULL) {
            GATTLIB_LOG(GATTLIB_ERROR, "Fail to connect to the bluetooth device.");
            sleep(1);
            return NULL;
        } else {
            puts("Succeeded to connect to the bluetooth device.");
        }

        gattlib_disconnect(gatt_connection);
        puts("disconnected from device");

	printf("------------DONE %s ---------------\n", addr);
	return NULL;
}

static void ble_discovered_device(void *adapter, const char* addr, const char* name, void *user_data) {
	struct connection_t *connection;
	int ret;

	if (name) {
		printf("Discovered %s - '%s'\n", addr, name);
	} else {
		printf("Discovered %s\n", addr);
	}

	connection = (struct connection_t*) malloc(sizeof(struct connection_t));
	if (connection == NULL) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failt to allocate connection.");
		return;
	}
	connection->addr = strdup(addr);

	ret = pthread_create(&connection->thread, NULL,	ble_connect_device, connection);
	if (ret != 0) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failt to create BLE connection thread.");
		free(connection);
		return;
	}
	LIST_INSERT_HEAD(&g_ble_connections, connection, entries);
}

int main(int argc, const char *argv[]) {
	const char* adapter_name;
	void* adapter;
	int ret;

	if (argc == 1) {
		adapter_name = NULL;
	} else if (argc == 2) {
		adapter_name = argv[1];
	} else {
		printf("%s [<bluetooth-adapter>]\n", argv[0]);
		return 1;
	}

#ifdef GATTLIB_LOG_BACKEND_SYSLOG
	openlog("gattlib_ble_scan", LOG_CONS | LOG_NDELAY | LOG_PERROR, LOG_USER);
	setlogmask(LOG_UPTO(LOG_INFO));
#endif

	LIST_INIT(&g_ble_connections);

    pthread_mutex_lock(&g_mutex);

    for (uint32_t x=0;x<10000;x++) {
        printf("iteration %d\n", x);
        printMemoryUsage();


        puts("open adapter");
        ret = gattlib_adapter_open(adapter_name, &adapter);
        if (ret) {
            GATTLIB_LOG(GATTLIB_ERROR, "Failed to open adapter.");
            return 1;
        }

        char * mac = "D8:48:DD:70:24:8F";
        //char * mac = "EB:01:B5:48:2D:EE";

        int paired = gattlib_is_paired(adapter, mac);
        printf("adapter is paired %d\n", paired);
        struct connection_t connection = {
                .addr = mac,
                .adapter = adapter,
        };

        ble_connect_device(&connection);


        puts("close adapter");
        gattlib_adapter_close(adapter);

    }
	puts("Scan completed");
	pthread_mutex_unlock(&g_mutex);

	// Wait for the thread to complete
	while (g_ble_connections.lh_first != NULL) {
		struct connection_t* connection = g_ble_connections.lh_first;
		pthread_join(connection->thread, NULL);
		LIST_REMOVE(g_ble_connections.lh_first, entries);
		free(connection->addr);
		free(connection);
	}

EXIT:
	gattlib_adapter_close(adapter);
	return ret;
}
