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
#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>
#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <time.h>
#include <mach/mach.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
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

/*
 * Wi-Fi self test. Nobody sits in front of the phone during bring-up, so this process (already running as root, with its
 * stdout on the kernel console) also acts as a MobileWiFi client, the same way Settings does: it asks wifid for the
 * device list and a scan, and prints what comes back. Every symbol is looked up at run time, so a missing one is reported
 * instead of killing the Bluetooth stand-in.
 */
typedef const void *CFTypeRef_;
typedef void (*scan_cb_t)(void *dev, const void *results, const void *err, void *ctx);

static void
wt_scan_done(void *dev, const void *results, const void *err, void *ctx)
{
	(void)dev;
	(void)ctx;
	printf("vince-wifitest: scan callback results=%p error=%p\n", results, err);
	if (results) {
		CFStringRef d = CFCopyDescription(results);
		if (d) {
			char buf[2048];
			if (CFStringGetCString(d, buf, sizeof buf, kCFStringEncodingUTF8)) {
				printf("vince-wifitest: %s\n", buf);
			}
			CFRelease(d);
		}
		printf("vince-wifitest: %ld networks\n", (long)CFArrayGetCount(results));
	}
	if (err) {
		CFStringRef d = CFCopyDescription(err);
		char buf[512];
		if (d && CFStringGetCString(d, buf, sizeof buf, kCFStringEncodingUTF8)) {
			printf("vince-wifitest: error %s\n", buf);
		}
	}
}

static void
wt_where(const char *tag, uintptr_t pc)
{
	Dl_info di;

	if (dladdr((void *)pc, &di) && di.dli_fname) {
		const char *base = strrchr(di.dli_fname, '/');
		printf("vince-wifitest:   %s %#lx %s+%#lx (%s)\n", tag, (unsigned long)pc, di.dli_sname ? di.dli_sname : "?",
		    di.dli_sname ? (unsigned long)(pc - (uintptr_t)di.dli_saddr) : 0ul, base ? base + 1 : di.dli_fname);
	} else {
		printf("vince-wifitest:   %s %#lx\n", tag, (unsigned long)pc);
	}
}

/* stack of another thread, to see where a stuck MobileWiFi call waits */
static void
wt_stack(pthread_t t)
{
	mach_port_t mp = pthread_mach_thread_np(t);
	arm_thread_state64_t st;
	mach_msg_type_number_t cnt = ARM_THREAD_STATE64_COUNT;

	thread_suspend(mp);
	if (thread_get_state(mp, ARM_THREAD_STATE64, (thread_state_t)&st, &cnt) == KERN_SUCCESS) {
		wt_where("pc", (uintptr_t)st.__pc);
		wt_where("lr", (uintptr_t)st.__lr);
		uintptr_t fp = (uintptr_t)st.__fp;
		for (int i = 0; i < 24 && fp && !(fp & 7); i++) {
			uintptr_t next = ((uintptr_t *)fp)[0];
			wt_where("fr", ((uintptr_t *)fp)[1]);
			if (next <= fp || next - fp > (1u << 20)) {
				break;
			}
			fp = next;
		}
	}
	thread_resume(mp);
}

/* every thread of another process (wifid): where does it wait? Shared cache images sit at the same address in all
 * processes, so dladdr() names those frames */
static void
wt_remote(const char *name)
{
	pid_t pid = 0;

	int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };
	size_t sz = 0;

	if (sysctl(mib, 3, NULL, &sz, NULL, 0) == 0) {
		struct kinfo_proc *kp = malloc(sz + 4096);

		sz += 4096;
		if (kp && sysctl(mib, 3, kp, &sz, NULL, 0) == 0) {
			for (size_t i = 0; i < sz / sizeof(*kp); i++) {
				if (!strcmp(kp[i].kp_proc.p_comm, name)) {
					pid = kp[i].kp_proc.p_pid;
				}
			}
		}
		printf("vince-wifitest: sysctl proc list %zu entries\n", kp ? sz / sizeof(*kp) : 0);
		free(kp);
	}
	mach_port_t task = MACH_PORT_NULL;
	kern_return_t (*tfp)(mach_port_t, int, mach_port_t *) = dlsym(RTLD_DEFAULT, "task_for_pid");
	kern_return_t kr = pid && tfp ? tfp(mach_task_self(), pid, &task) : KERN_FAILURE;

	printf("vince-wifitest: %s pid %d task_for_pid %d (%s)\n", name, pid, kr, tfp ? "sym" : "nosym");
	if (kr != KERN_SUCCESS) {
		static int aborted;

		if (pid && !aborted++) {
			printf("vince-wifitest: aborting %s to get a kernel crash backtrace\n", name);
			kill(pid, SIGABRT);
		}
		return;
	}
	thread_act_array_t th;
	mach_msg_type_number_t nth = 0;

	if (task_threads(task, &th, &nth) != KERN_SUCCESS) {
		return;
	}
	for (mach_msg_type_number_t t = 0; t < nth; t++) {
		arm_thread_state64_t st;
		mach_msg_type_number_t cnt = ARM_THREAD_STATE64_COUNT;

		thread_suspend(th[t]);
		if (thread_get_state(th[t], ARM_THREAD_STATE64, (thread_state_t)&st, &cnt) == KERN_SUCCESS) {
			printf("vince-wifitest: %s thread %u\n", name, t);
			wt_where("pc", (uintptr_t)st.__pc);
			wt_where("lr", (uintptr_t)st.__lr);
			uintptr_t fp = (uintptr_t)st.__fp;
			for (int i = 0; i < 20 && fp && !(fp & 7); i++) {
				uintptr_t fr[2] = { 0, 0 };
				vm_size_t got = 0;

				if (vm_read_overwrite(task, fp, 16, (vm_address_t)fr, &got) != KERN_SUCCESS || got != 16) {
					break;
				}
				wt_where("fr", fr[1]);
				if (fr[0] <= fp || fr[0] - fp > (1u << 20)) {
					break;
				}
				fp = fr[0];
			}
		}
		thread_resume(th[t]);
	}
}

static volatile int wt_done;
static pthread_t wt_worker;

static void *wt_work(void *arg);

static void *
wt_thread(void *arg)
{
	(void)arg;
	if (access("/private/var/tmp/vince-wifitest", F_OK)) {
		return NULL;    /* the test client scans and so interrupts joins: only on request */
	}
	sleep(90);
	pthread_create(&wt_worker, NULL, wt_work, NULL);
	for (int i = 0; i < 40 && !wt_done; i++) {
		sleep(20);
		if (!wt_done && (i & 1)) {
			printf("vince-wifitest: worker still busy, stack:\n");
			wt_stack(wt_worker);
			wt_remote("wifid");
		}
	}
	return NULL;
}

static void *
wt_work(void *arg)
{
	(void)arg;
	void *h = dlopen("/System/Library/PrivateFrameworks/MobileWiFi.framework/MobileWiFi", RTLD_NOW);
	printf("vince-wifitest: MobileWiFi handle %p\n", h);
	if (!h) {
		return NULL;
	}
	void *(*mgr_create)(CFAllocatorRef, long) = dlsym(h, "WiFiManagerClientCreate");
	void (*mgr_sched)(void *, CFRunLoopRef, CFStringRef) = dlsym(h, "WiFiManagerClientScheduleWithRunLoop");
	CFArrayRef (*mgr_devs)(void *) = dlsym(h, "WiFiManagerClientCopyDevices");
	int (*dev_scan)(void *, CFDictionaryRef, scan_cb_t, void *) = dlsym(h, "WiFiDeviceClientScanAsync");
	int (*mgr_getpower)(void *) = dlsym(h, "WiFiManagerClientGetPower");
	printf("vince-wifitest: create=%p sched=%p devs=%p scan=%p getpower=%p\n", mgr_create, mgr_sched, mgr_devs, dev_scan, mgr_getpower);
	if (!mgr_create || !mgr_devs || !dev_scan) {
		return NULL;
	}
	void *mgr = mgr_create(kCFAllocatorDefault, 0);
	printf("vince-wifitest: manager %p\n", mgr);
	if (!mgr) {
		return NULL;
	}
	if (mgr_sched) {
		mgr_sched(mgr, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
	}
	for (int round = 0; round < 8; round++) {
		if (mgr_getpower) {
			printf("vince-wifitest: power %d\n", mgr_getpower(mgr));
		}
		void (*mgr_setpower)(void *, int) = dlsym(h, "WiFiManagerClientSetPower");
		if (round == 1 && mgr_setpower) {
			mgr_setpower(mgr, 1);
			printf("vince-wifitest: requested power on\n");
		}
		CFArrayRef devs = mgr_devs(mgr);
		long n = devs ? CFArrayGetCount(devs) : -1;
		printf("vince-wifitest: round %d devices %ld\n", round, n);
		if (n > 0) {
			CFDictionaryRef p = CFDictionaryCreate(kCFAllocatorDefault, NULL, NULL, 0, &kCFTypeDictionaryKeyCallBacks,
			    &kCFTypeDictionaryValueCallBacks);
			int r = dev_scan((void *)CFArrayGetValueAtIndex(devs, 0), p, wt_scan_done, NULL);
			printf("vince-wifitest: scan started -> %d\n", r);
		}
		CFRunLoopRunInMode(kCFRunLoopDefaultMode, 20, false);
	}
	wt_done = 1;
	return NULL;
}

/*
 * Network configuration dump: which SystemConfiguration keys exist for en0 (is there a service, what IPv4 method, did
 * IPConfiguration publish anything). configd logs only to os_log, which nothing reads here.
 */
static void
sc_print(const char *tag, CFTypeRef obj)
{
	CFStringRef d = obj ? CFCopyDescription(obj) : NULL;
	static char buf[24576];

	if (!d || !CFStringGetCString(d, buf, sizeof buf, kCFStringEncodingUTF8)) {
		printf("vince-sc: %s: (none or too long)\n", tag);
	} else {
		char *line = buf;

		printf("vince-sc: %s:\n", tag);
		for (char *p = buf; ; p++) {
			if (*p == '\n' || *p == '\0') {
				char c = *p;

				*p = '\0';
				printf("vince-sc:   %s\n", line);
				if (c == '\0') {
					break;
				}
				line = p + 1;
			}
		}
	}
	if (d) {
		CFRelease(d);
	}
}

static void
sc_dump(void)
{
	void *h = dlopen("/System/Library/Frameworks/SystemConfiguration.framework/SystemConfiguration", RTLD_NOW);
	CFTypeRef (*create)(CFAllocatorRef, CFStringRef, void *, void *) = h ? dlsym(h, "SCDynamicStoreCreate") : NULL;
	CFDictionaryRef (*multi)(CFTypeRef, CFArrayRef, CFArrayRef) = h ? dlsym(h, "SCDynamicStoreCopyMultiple") : NULL;

	if (!create || !multi) {
		printf("vince-sc: no SystemConfiguration\n");
		return;
	}
	CFTypeRef st = create(NULL, CFSTR("vince-sc"), NULL, NULL);
	CFStringRef pats[] = {
		CFSTR("State:/Network/Interface/en0/.*"), CFSTR("State:/Network/Service/.*"), CFSTR("State:/Network/Global/.*"),
		CFSTR("Setup:/Network/Service/.*/Interface"), CFSTR("Setup:/Network/Service/.*/IPv4"), CFSTR("Setup:/Network/Global/.*"),
	};
	CFArrayRef pa = CFArrayCreate(NULL, (const void **)pats, sizeof(pats) / sizeof(pats[0]), &kCFTypeArrayCallBacks);
	CFDictionaryRef r = st ? multi(st, NULL, pa) : NULL;

	sc_print("dynamic store", r);
	if (r) {
		CFRelease(r);
	}
	CFRelease(pa);
	CFURLRef u = CFURLCreateWithFileSystemPath(NULL, CFSTR("/private/var/preferences/SystemConfiguration/preferences.plist"),
	    kCFURLPOSIXPathStyle, false);
	CFReadStreamRef rs = CFReadStreamCreateWithFile(NULL, u);

	if (rs && CFReadStreamOpen(rs)) {
		CFPropertyListRef pl = CFPropertyListCreateWithStream(NULL, rs, 0, kCFPropertyListImmutable, NULL, NULL);
		CFTypeRef ns = pl ? CFDictionaryGetValue(pl, CFSTR("NetworkServices")) : NULL;

		sc_print("preferences.plist NetworkServices", ns);
		if (pl) {
			CFRelease(pl);
		}
		CFReadStreamClose(rs);
	} else {
		printf("vince-sc: preferences.plist not readable\n");
	}
	if (rs) {
		CFRelease(rs);
	}
	CFRelease(u);
}

/*
 * wifid's own debug log. With /private/var/Managed Preferences/mobile/com.apple.MobileWiFi.debug.plist saying
 * WiFiManagerLoggingEnabled = "true" (read when wifid starts, so it takes effect from the next boot on) wifid writes
 * its full log to /var/mobile/Library/Logs/CrashReporter/WiFi/WiFiManager/wifi-buf-*.log. This copies new lines of the
 * newest such file to stdout (the kernel console) as "vince-wlog:".
 */
#define WLOG_DIR "/var/mobile/Library/Logs/CrashReporter/WiFi/WiFiManager"

static void
wlog_enable(void)
{
	static const char plist[] =
	    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
	    "<plist version=\"1.0\">\n<dict>\n\t<key>WiFiManagerLoggingEnabled</key>\n\t<string>true</string>\n</dict>\n</plist>\n";
	const char *dir = "/private/var/Managed Preferences/mobile";
	char path[160];

	mkdir("/private/var/Managed Preferences", 0755);
	mkdir(dir, 0755);
	snprintf(path, sizeof path, "%s/com.apple.MobileWiFi.debug.plist", dir);
	/*
	 * The diagnostic profile turned out to move wifid's log from /Library/Logs/wifimanager.log into an in-memory ring
	 * that is only written out on demand. The plain file log is verbose enough: remove the profile again (once).
	 */
	(void)plist;
	if (access(path, F_OK) == 0) {
		FILE *f = stdout;

		unlink(path);
		if (f) {
			int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };
			size_t sz = 0;

			printf("vince-wlog: wifid diagnostic profile removed; restarting wifid once so it logs to the file again\n");
			if (sysctl(mib, 3, NULL, &sz, NULL, 0) == 0) {
				struct kinfo_proc *kp = malloc(sz + 4096);

				sz += 4096;
				if (kp && sysctl(mib, 3, kp, &sz, NULL, 0) == 0) {
					for (size_t i = 0; i < sz / sizeof(*kp); i++) {
						if (!strcmp(kp[i].kp_proc.p_comm, "wifid")) {
							kill(kp[i].kp_proc.p_pid, SIGTERM);
						}
					}
				}
				free(kp);
			}
		} else {
			printf("vince-wlog: cannot write %s\n", path);
		}
	}
}

static void *
wlog_thread(void *arg)
{
	char cur[256] = "";
	long off = 0;
	static char line[1024];

	(void)arg;
	for (;;) {
		DIR *d = opendir(WLOG_DIR);
		char best[256] = "";
		time_t bt = 0;
		static const char *fixed[] = {
			"/var/mobile/Library/Logs/wifimanager.log", "/Library/Logs/wifimanager.log",
			"/private/var/root/Library/Logs/wifimanager.log", "/var/mobile/Library/Logs/CrashReporter/WiFi/wifimanager.log",
		};

		for (unsigned k = 0; k < sizeof(fixed) / sizeof(fixed[0]); k++) {
			struct stat st;

			if (stat(fixed[k], &st) == 0 && st.st_mtime >= bt) {
				bt = st.st_mtime;
				strlcpy(best, fixed[k], sizeof best);
			}
		}
		if (d) {
			struct dirent *e;

			while ((e = readdir(d))) {
				char p[256];
				struct stat st;

				size_t nl = strlen(e->d_name);

				if (nl < 5 || strcmp(e->d_name + nl - 4, ".log") != 0) {
					continue;
				}
				snprintf(p, sizeof p, "%s/%s", WLOG_DIR, e->d_name);
				if (stat(p, &st) == 0 && st.st_mtime >= bt) {
					bt = st.st_mtime;
					strlcpy(best, p, sizeof best);
				}
			}
			closedir(d);
		}
		if (best[0] && strcmp(best, cur) != 0) {
			strlcpy(cur, best, sizeof cur);
			off = 0;
			printf("vince-wlog: following %s\n", cur);
		}
		if (cur[0]) {
			FILE *f = fopen(cur, "r");

			if (f) {
				int n = 0;

				fseek(f, off, SEEK_SET);
				while (n < 400 && fgets(line, sizeof line, f)) {
					size_t l = strlen(line);

					if (l && line[l - 1] == '\n') {
						line[l - 1] = '\0';
					} else if (!feof(f)) {
						continue;       /* an over-long line: its rest follows */
					} else {
						break;          /* a partial last line: read it next time */
					}
					off = ftell(f);
					printf("vince-wlog: %.300s\n", line);
					n++;
				}
				fclose(f);
			}
		}
		sleep(3);
	}
	return NULL;
}

/*
 * PosterBoard (wallpapers) aborts on every start with "Invalid condition not satisfying: !_stateLock_switcherConfiguration"
 * in -[PBFPosterExtensionDataStore _stateLock_setupSwitcherConfigurationForFirstTime]: its data store holds a
 * half-written configuration from an early boot, so lock and home screen stay black. Once (marker file), move the
 * data store aside inside PosterBoard's data container so it is created again; nothing is deleted.
 */
static void
pb_reset(void)
{
	const char *base = "/var/mobile/Containers/Data/Application";
	const char *marker = "/private/var/tmp/vince-pbreset-done";
	DIR *d = opendir(base);
	struct dirent *e;

	if (!d) {
		printf("vince-pb: no %s\n", base);
		return;
	}
	while ((e = readdir(d))) {
		char p[512];

		if (e->d_name[0] == '.') {
			continue;
		}
		snprintf(p, sizeof p, "%s/%s/.com.apple.mobile_container_manager.metadata.plist", base, e->d_name);
		CFURLRef u = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)p, (CFIndex)strlen(p), false);
		CFReadStreamRef rs = u ? CFReadStreamCreateWithFile(NULL, u) : NULL;
		CFPropertyListRef pl = NULL;

		if (rs && CFReadStreamOpen(rs)) {
			pl = CFPropertyListCreateWithStream(NULL, rs, 0, kCFPropertyListImmutable, NULL, NULL);
			CFReadStreamClose(rs);
		}
		if (rs) {
			CFRelease(rs);
		}
		if (u) {
			CFRelease(u);
		}
		CFStringRef id = pl && CFGetTypeID(pl) == CFDictionaryGetTypeID() ?
		    CFDictionaryGetValue(pl, CFSTR("MCMMetadataIdentifier")) : NULL;
		bool hit = id && CFGetTypeID(id) == CFStringGetTypeID() && CFStringFind(id, CFSTR("PosterBoard"), 0).location != kCFNotFound;

		if (pl) {
			CFRelease(pl);
		}
		if (!hit) {
			continue;
		}
		char as[512];

		snprintf(as, sizeof as, "%s/%s/Library/Application Support", base, e->d_name);
		printf("vince-pb: PosterBoard container %s\n", e->d_name);
		DIR *d2 = opendir(as);
		struct dirent *f;
		bool done = access(marker, F_OK) == 0;

		while (d2 && (f = readdir(d2))) {
			char q[600], r[640];

			if (f->d_name[0] == '.') {
				continue;
			}
			snprintf(q, sizeof q, "%s/%s", as, f->d_name);
			printf("vince-pb:   Application Support/%s\n", f->d_name);
			if (!done && !strstr(f->d_name, ".vince-bak")) {
				snprintf(r, sizeof r, "%s.vince-bak", q);
				printf("vince-pb:   moved aside -> %d\n", rename(q, r));
			}
		}
		if (d2) {
			closedir(d2);
		}
		if (!done) {
			FILE *m = fopen(marker, "w");

			if (m) {
				fclose(m);
			}
		}
	}
	closedir(d);
}

/* where does PosterBoard keep its data store ("PRDataStore_%lu" directories)? Print what matches; move PRDataStore_*
 * aside once (marker v2) so the store is built again */
static int pb_hits;

static void
pb_walk(const char *dir, int depth, bool move)
{
	DIR *d = opendir(dir);
	struct dirent *e;

	if (!d) {
		return;
	}
	while ((e = readdir(d)) && pb_hits < 60) {
		char p[1024];
		struct stat st;

		if (e->d_name[0] == '.' && (e->d_name[1] == '\0' || (e->d_name[1] == '.' && e->d_name[2] == '\0'))) {
			continue;
		}
		snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
		if (lstat(p, &st) != 0 || !S_ISDIR(st.st_mode)) {
			continue;
		}
		if (strstr(e->d_name, "PRDataStore") || strstr(e->d_name, "PosterBoard") || strstr(e->d_name, "Poster")) {
			pb_hits++;
			printf("vince-pb: found %s\n", p);
			if (move && (strncmp(e->d_name, "PRDataStore", 11) == 0 || strcmp(e->d_name, "PRBPosterExtensionDataStore") == 0) &&
			    !strstr(e->d_name, ".vince-bak")) {
				char r[1100];

				snprintf(r, sizeof r, "%s.vince-bak", p);
				printf("vince-pb:   moved aside -> %d\n", rename(p, r));
				continue;
			}
		}
		if (depth > 0) {
			pb_walk(p, depth - 1, move);
		}
	}
	closedir(d);
}

static void *
pb_thread(void *arg)
{
	const char *marker = "/private/var/tmp/vince-pbreset3-done";
	bool move = access(marker, F_OK) != 0;

	(void)arg;
	pb_walk("/var/mobile/Containers/Data", 5, move);
	pb_walk("/var/mobile/Containers/Shared/AppGroup", 4, move);
	pb_walk("/var/mobile/Library", 4, move);
	printf("vince-pb: search done, %d hits\n", pb_hits);
	if (move) {
		FILE *m = fopen(marker, "w");

		if (m) {
			fclose(m);
		}
	}
	return NULL;
}

static void *
sc_thread(void *arg)
{
	(void)arg;
	sleep(330);
	sc_dump();
	sleep(240);
	sc_dump();
	return NULL;
}

int
main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	setvbuf(stdout, NULL, _IOLBF, 0);
	printf("vince-bluetool: stand-in for com.apple.BlueTool starting\n");
	pthread_t wt, sct, wlt;
	pthread_create(&wt, NULL, wt_thread, NULL);
	pthread_create(&sct, NULL, sc_thread, NULL);
	wlog_enable();
	pthread_create(&wlt, NULL, wlog_thread, NULL);
	pb_reset();
	pthread_t pbt;
	pthread_create(&pbt, NULL, pb_thread, NULL);
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
