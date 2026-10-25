/*
 * vince-bluetool: stand-in for /usr/sbin/BlueTool on the Redmi 5 Plus port.
 *
 * bluetoothd "boots" the Bluetooth chip by asking the com.apple.BlueTool XPC service to run a builtin script
 * ("RunBuiltinScript" / "RunCommand"). The real BlueTool drives Apple's PCIe/ACI Broadcom parts through
 * AppleConvergedTransport and AppleOLYHAL, which this phone does not have; the Qualcomm Pronto controller is brought up
 * by the kernel (bsd/dev/vince_bt.c) and bluetoothd talks HCI to it through the AppleBTHci pipes. So the "boot" has
 * nothing left to do: this service answers every request with success.
 *
 * Reply keys come from bluetoothd's reply handler: "result" (int64, 0 = success), "firmware" (string),
 * "manufacturer" (int64, the HCI company id of the controller), "output" (string, for RunCommand).
 */
#include <dispatch/dispatch.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

typedef void *xpc_object_t;
typedef xpc_object_t xpc_connection_t;
typedef void (^xpc_handler_t)(xpc_object_t object);
struct _xpc_type_s;
extern const struct _xpc_type_s _xpc_type_connection;
extern const struct _xpc_type_s _xpc_type_dictionary;
extern const struct _xpc_type_s *xpc_get_type(xpc_object_t object);
extern xpc_connection_t xpc_connection_create_mach_service(const char *name, dispatch_queue_t targetq, uint64_t flags);
extern void xpc_connection_set_event_handler(xpc_connection_t connection, xpc_handler_t handler);
extern void xpc_connection_resume(xpc_connection_t connection);
extern void xpc_connection_send_message(xpc_connection_t connection, xpc_object_t message);
extern xpc_connection_t xpc_dictionary_get_remote_connection(xpc_object_t xdict);
extern xpc_object_t xpc_dictionary_create_reply(xpc_object_t original);
extern void xpc_dictionary_set_int64(xpc_object_t xdict, const char *key, int64_t value);
extern void xpc_dictionary_set_string(xpc_object_t xdict, const char *key, const char *string);
extern const char *xpc_dictionary_get_string(xpc_object_t xdict, const char *key);
extern xpc_object_t xpc_dictionary_get_value(xpc_object_t xdict, const char *key);
extern char *xpc_copy_description(xpc_object_t object);
extern void xpc_release(xpc_object_t object);

#define LISTENER 1u

int
main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	setvbuf(stdout, NULL, _IOLBF, 0);
	printf("vince-bluetool: stand-in for com.apple.BlueTool starting\n");
	xpc_connection_t listener = xpc_connection_create_mach_service("com.apple.BlueTool", dispatch_get_main_queue(), LISTENER);
	if (!listener) {
		printf("vince-bluetool: cannot create the service\n");
		return 1;
	}
	xpc_connection_set_event_handler(listener, ^(xpc_object_t peer) {
		if (xpc_get_type(peer) != &_xpc_type_connection) {
			return;
		}
		xpc_connection_set_event_handler(peer, ^(xpc_object_t msg) {
			if (xpc_get_type(msg) != &_xpc_type_dictionary) {
				return;
			}
			char *d = xpc_copy_description(msg);
			printf("vince-bluetool: request %s\n", d ? d : "?");
			free(d);
			xpc_object_t reply = xpc_dictionary_create_reply(msg);
			if (reply) {
				xpc_dictionary_set_int64(reply, "result", 0);
				xpc_dictionary_set_string(reply, "output", "");
				xpc_dictionary_set_string(reply, "firmware", "VINCE-PRONTO");
				xpc_dictionary_set_int64(reply, "manufacturer", 15);
				xpc_connection_send_message(xpc_dictionary_get_remote_connection(msg), reply);
				xpc_release(reply);
			}
		});
		xpc_connection_resume(peer);
	});
	xpc_connection_resume(listener);
	dispatch_main();
	return 0;
}
