#include <stdio.h>
#include <stdlib.h>
#include "mountwatch.h"
#include "config.h"
#include "package.h"
#include "dbp.h"

#include <dbus/dbus.h>

void test_dbus_unregister(DBusConnection *dc, void *n) {
	fprintf(stderr, "Unregister handler called\n");
	return;
}


DBusHandlerResult test_dbus_msg_handler(DBusConnection *dc, DBusMessage *dm, void *n) {
	struct package_s *p = n;
	DBusMessage *ndm;
	DBusMessageIter iter;
	const char *arg;
	char *ret;

	if (!dbus_message_iter_init(dm, &iter)) {
		fprintf(stderr, "Message has no arguments\n");
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING) {
		fprintf(stderr, "Message has bad argument type\n");
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	dbus_message_iter_get_basic(&iter, &arg); 
	ret = malloc(11);

	/* Process message */
	if (dbus_message_is_method_call(dm, DBP_DBUS_DAEMON_PREFIX, "Mount")) {
		sprintf(ret, "%i", package_run(p, arg));
	} else if (dbus_message_is_method_call(dm, DBP_DBUS_DAEMON_PREFIX, "UMount")) {
		sprintf(ret, "%i", package_stop(p, atoi(arg)));
	} else
		fprintf(stderr, "Bad method call\n");

	ndm = dbus_message_new_method_return(dm);
	dbus_message_append_args(ndm, DBUS_TYPE_STRING, &ret, DBUS_TYPE_INVALID);
	dbus_connection_send(dc, ndm, NULL);
	dbus_connection_flush(dc);
	dbus_message_unref(ndm);
	free(ret);

	return DBUS_HANDLER_RESULT_HANDLED;
}


void *test_dbus_loop(void *n) {
	struct package_s *p = n;
	DBusError err;
	DBusConnection *dc;
	DBusMessage *dm;
	DBusObjectPathVTable vt;

	vt.unregister_function = test_dbus_unregister;
	vt.message_function = test_dbus_msg_handler;

	dbus_error_init(&err);
//	if (!(dc = dbus_bus_get(DBUS_BUS_SYSTEM, &err))) {
	if (!(dc = dbus_bus_get(DBUS_BUS_SESSION, &err))) {
		fprintf(stderr, "Unable to connect to dbus system bus\n");
		exit(-1);
	}

	dbus_bus_request_name(dc, DBP_DBUS_DAEMON_PREFIX, 0, &err);
	if (dbus_error_is_set(&err))
		fprintf(stderr, "unable to name bus: %s\n", err.message);
	if (!dbus_connection_register_object_path(dc, DBP_DBUS_DAEMON_OBJECT, &vt, p))
		fprintf(stderr, "Unable to register object path\n");
	while (dbus_connection_read_write_dispatch (dc, -1));
	pthread_exit(NULL);
}


void test_dbus_register(struct package_s *p) {
	pthread_t th;

	if (pthread_create(&th, NULL, test_dbus_loop, p)) {
		fprintf(stderr, "Unable to create dbus listen thread\n");
		exit(-1);
	}
}


int main(int argc, char **argv) {
	struct mountwatch_change_s change;
	struct package_s p;
	int i;

	p = package_init();
	test_dbus_register(&p);

	if (!mountwatch_init())
		exit(-1);
	config_init();
	
	for (;;) {
		change = mountwatch_diff();
		for (i = 0; i < change.entries; i++) {
			switch (change.entry[i].tag) {
				case MOUNTWATCH_TAG_REMOVED:
					package_release_mount(&p, change.entry[i].device);
					fprintf(stderr, "%s umounted from %s\n", change.entry[i].device, change.entry[i].mount);
					break;
				case MOUNTWATCH_TAG_ADDED:
					package_crawl_mount(&p, change.entry[i].device, change.entry[i].mount);
					fprintf(stderr, "%s mounted at %s\n", change.entry[i].device, change.entry[i].mount);
					break;
				default:
					break;
			}
		}

		mountwatch_change_free(change);
	}

	return 0;
}