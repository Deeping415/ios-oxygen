/*
 * vince-p2pd: stand-in for /usr/libexec/wifip2pd on the Redmi 5 Plus port.
 *
 * The real wifip2pd (AWDL / Wi-Fi Direct) exits with status 1 as soon as it cannot initialise its P2P interface, which
 * this phone's Wi-Fi driver does not have. wifid asks it things with synchronous NSXPCConnection calls, and every
 * respawn of the failing daemon kept wifid's serial queue waiting (the join result was only published afterwards, so
 * Settings spun). This stand-in keeps the mach service alive and drops every connection at once, which makes those
 * calls fail immediately.
 */
#include <dispatch/dispatch.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef void *xpc_object_t;
typedef xpc_object_t xpc_connection_t;
typedef void (^xpc_handler_t)(xpc_object_t object);
struct _xpc_type_s;
extern const struct _xpc_type_s _xpc_type_connection;
extern const struct _xpc_type_s *xpc_get_type(xpc_object_t object);
extern xpc_connection_t xpc_connection_create_mach_service(const char *name, dispatch_queue_t targetq, uint64_t flags);
extern void xpc_connection_set_event_handler(xpc_connection_t connection, xpc_handler_t handler);
extern void xpc_connection_resume(xpc_connection_t connection);
extern void xpc_connection_cancel(xpc_connection_t connection);

#define LISTENER 1u

int
main(void)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	printf("vince-p2pd: stand-in for com.apple.wifip2pd starting\n");
	xpc_connection_t listener = xpc_connection_create_mach_service("com.apple.wifip2pd", dispatch_get_main_queue(), LISTENER);
	if (!listener) {
		printf("vince-p2pd: cannot create the service\n");
		return 1;
	}
	xpc_connection_set_event_handler(listener, ^(xpc_object_t peer) {
		if (xpc_get_type(peer) != &_xpc_type_connection) {
			return;
		}
		xpc_connection_set_event_handler(peer, ^(xpc_object_t msg) {
			(void)msg;
		});
		xpc_connection_resume(peer);
		xpc_connection_cancel(peer);
	});
	xpc_connection_resume(listener);
	dispatch_main();
	return 0;
}
