#include "dbp.h"

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>

#include <unistd.h>
#include <limits.h>
#include <sys/inotify.h>
#include "mountwatch.h"
#include "config.h"

/* I keep almost typing mouthwash.. <.< */
struct mountwatch_s mountwatch_struct;


void *mountwatch_dirchange() {
	fd_set set;
	for (;;) {
		FD_ZERO(&set);
		FD_SET(mountwatch_struct.dir_fd, &set);
		select(FD_SETSIZE, &set, NULL, NULL, NULL);

		pthread_mutex_lock(&mountwatch_struct.dir_watch_mutex);
		mountwatch_struct.dir_change = 1;
		sem_post(&mountwatch_struct.changed);
		pthread_mutex_unlock(&mountwatch_struct.dir_watch_mutex);

		sem_wait(&mountwatch_struct.dir_watch_continue);
	}
}

void *mountwatch_loop(void *null) {
	int mountfd;
	fd_set watch;

	if ((mountfd = open("/proc/mounts", O_RDONLY, 0)) < 0) {
		fprintf(dbp_error_log, "Unable to open /proc/mounts\n");
		pthread_exit(NULL);
	}

	for (;;) {
		FD_ZERO(&watch);
		FD_SET(mountfd, &watch);
		select(mountfd + 1, NULL, NULL, &watch, NULL);
		sem_post(&mountwatch_struct.changed);
	}
}


int mountwatch_init() {
	pthread_t th, inth;

	pthread_mutex_init(&mountwatch_struct.dir_watch_mutex, NULL);
	sem_init(&mountwatch_struct.changed, 0, 1);
	sem_init(&mountwatch_struct.dir_watch_continue, 0, 0);
	mountwatch_struct.entry = NULL, mountwatch_struct.entries = 0;
	mountwatch_struct.ientry = NULL, mountwatch_struct.ientries = 0;
	if (pthread_create(&th, NULL, mountwatch_loop, NULL)) {
		fprintf(dbp_error_log, "Error: Unable to create mountpoint watch process\n");
		return 0;
	}

	mountwatch_struct.dir_fd = inotify_init1(IN_NONBLOCK);
	if (pthread_create(&inth, NULL, mountwatch_dirchange, NULL)) {
		fprintf(dbp_error_log, "Error: Unable to create dirwatch process\n");
		return 0;
	}

	return 1;
}


int mountwatch_change_add(struct mountwatch_change_s *change, const char *mount, const char *device, const char *path, int tag) {
	int id;

	id = change->entries++;
	change->entry = realloc(change->entry, sizeof(*change->entry) * change->entries);
	change->entry[id].device = strdup(device);
	change->entry[id].mount = strdup(mount);
	change->entry[id].path = strdup(path);
	change->entry[id].tag = tag;
	return id;
}


static void mountwatch_inotify_remove_entry(int i) {
	mountwatch_struct.ientries--;
	free(mountwatch_struct.ientry[i].mount);
	free(mountwatch_struct.ientry[i].path);
	free(mountwatch_struct.ientry[i].device);
	inotify_rm_watch(mountwatch_struct.dir_fd, mountwatch_struct.ientry[i].handle);
	memmove(&mountwatch_struct.ientry[i], &mountwatch_struct.ientry[i + 1],
	    sizeof(*mountwatch_struct.ientry) * (mountwatch_struct.ientries - i));
	return;
}


static void mountwatch_inotify_remove(const char *mount) {
	int i;
	
	for (i = 0; i < mountwatch_struct.ientries; i++)
		if (!strcmp(mountwatch_struct.ientry[i].mount, mount)) {
			mountwatch_inotify_remove_entry(i), mountwatch_inotify_remove(mount);
			break;
		}
	return;
}


static void mountwatch_inotify_add_entry(const char *mount, const char *device, const char *path) {
	int id;

	id = mountwatch_struct.ientries++;
	mountwatch_struct.ientry = realloc(mountwatch_struct.ientry,
	    sizeof(*mountwatch_struct.ientry) * mountwatch_struct.ientries);
	mountwatch_struct.ientry[id].mount = strdup(mount);
	mountwatch_struct.ientry[id].path = strdup(path);
	mountwatch_struct.ientry[id].device = strdup(device);
	mountwatch_struct.ientry[id].handle = inotify_add_watch(mountwatch_struct.dir_fd,
	    path, MOUNTWATCH_INOTIFY_MASK);
	return;
}


static void mountwatch_inotify_add(const char *mount, const char *device) {
	char path[PATH_MAX];
	int i;

	for (i = 0; i < config_struct.search_dirs; i++) {
		sprintf(path, "%s/%s", mount, config_struct.search_dir[i]);
		mountwatch_inotify_add_entry(mount, device, path);
	}
	
	return;
}


static int mountwatch_path_lookup_entry(int handle) {
	int i;

	for (i = 0; i < mountwatch_struct.ientries; i++)
		if (mountwatch_struct.ientry[i].handle == handle)
			return i;
	return -1;
}


static void mountwatch_inotify_handle(struct mountwatch_change_s *change) {
	struct inotify_event *ie;
	int ndata, max_sz = (sizeof(struct inotify_event) + NAME_MAX + 1), ientry;
	char buff[sizeof(struct inotify_event) + NAME_MAX + 1];
	char *next_buff, *name, path[PATH_MAX];
	struct mountwatch_inotify_s *in;

	while ((ndata = read(mountwatch_struct.dir_fd, buff, max_sz)) > 0) {
		next_buff = buff;
		ie = (void *) next_buff;
		for (;;) {
			name = next_buff + ie->len;
			if (ie->len > 0) {
				if ((ientry = mountwatch_path_lookup_entry(ie->wd)) < 0)
					goto no;
				in = &mountwatch_struct.ientry[ientry];
				
				sprintf(path, "%s/%s", in->path, name);
				/* Remove-add sequence */
				if ((ie->mask & IN_CLOSE_WRITE)) {
					mountwatch_change_add(change, in->mount, in->device, path, MOUNTWATCH_TAG_PKG_REMOVED);
					mountwatch_change_add(change, in->mount, in->device, path, MOUNTWATCH_TAG_PKG_ADDED);
				} if ((ie->mask & IN_DELETE) || (ie->mask & IN_MOVED_FROM)) {
					mountwatch_change_add(change, in->mount, in->device, path, MOUNTWATCH_TAG_PKG_REMOVED);
				} if ((ie->mask & IN_CREATE) || (ie->mask & IN_MOVED_TO)) {
					mountwatch_change_add(change, in->mount, in->device, path, MOUNTWATCH_TAG_PKG_ADDED);
				}
			}

			no: 
			if (ndata > sizeof(*ie) + ie->len + (next_buff - buff))
				next_buff = &next_buff[ie->len + sizeof(*ie)];
			else
				break;
		}
	}

	return;
}


struct mountwatch_change_s mountwatch_diff() {
	struct mountwatch_change_s change;
	FILE *fp;
	char mount[256], device[256];
	int i, n;

	wait:
	sem_wait(&mountwatch_struct.changed);
	pthread_mutex_lock(&mountwatch_struct.dir_watch_mutex);

	change.entry = NULL, change.entries = 0;

	if (!(fp = fopen("/proc/mounts", "r"))) {
		fprintf(dbp_error_log, "Unable to open /proc/mounts\n");
		pthread_mutex_unlock(&mountwatch_struct.dir_watch_mutex);
		return change;
	}


	while (!feof(fp)) {
		*mount = *device = 0;
		fscanf(fp, "%256s %256s \n", device, mount);

		if (*device != '/')
			/* Special filesystem, ignore */
			/* This will break sshfs, samba etc, but that's	*
			** probably a good thing 			*/
			continue;

		if (strstr(device, "/dev/loop") == device)
			/* Loop-back devices should probably not be	*
			** watched..					*/
			continue;

		for (i = 0; i < mountwatch_struct.entries; i++) {
			if (!strcmp(mountwatch_struct.entry[i].mount, mount)) {
				mountwatch_struct.entry[i].tag = 1;
				if (strcmp(mountwatch_struct.entry[i].device, device)) {
					mountwatch_inotify_remove(mountwatch_struct.entry[i].mount);
					mountwatch_inotify_add(mount, device);
					mountwatch_change_add(&change, mount, device, "", MOUNTWATCH_TAG_CHANGED);
					free(mountwatch_struct.entry[i].mount);
					mountwatch_struct.entry[i].mount = strdup(mount);
					mountwatch_struct.entry[i].tag = 1;
				} else
					break;
			}
		}

		if (i == mountwatch_struct.entries) {
			/* Mount was added */
			mountwatch_change_add(&change, mount, device, "", MOUNTWATCH_TAG_ADDED);
			mountwatch_inotify_add(mount, device);
			n = mountwatch_struct.entries++;
			mountwatch_struct.entry = realloc(mountwatch_struct.entry,
			    sizeof(*mountwatch_struct.entry) * mountwatch_struct.entries);
			mountwatch_struct.entry[n].mount = strdup(mount);
			mountwatch_struct.entry[n].device = strdup(device);
			mountwatch_struct.entry[n].tag = 1;
		}
	}

	for (i = 0; i < mountwatch_struct.entries; i++) {
		if (!mountwatch_struct.entry[i].tag) {
			/* Mount was removed */
			mountwatch_change_add(&change, mountwatch_struct.entry[i].device,
			    mountwatch_struct.entry[i].device, "", MOUNTWATCH_TAG_REMOVED);
			
			mountwatch_inotify_remove(mountwatch_struct.entry[i].mount);

			free(mountwatch_struct.entry[i].mount);
			free(mountwatch_struct.entry[i].device);
			memmove(&mountwatch_struct.entry[i], &mountwatch_struct.entry[i + 1],
			    (mountwatch_struct.entries - 1 - i) * sizeof(*mountwatch_struct.entry));
			mountwatch_struct.entries--;
			i--;
		} else
			mountwatch_struct.entry[i].tag = 0;
	}

	fclose(fp);

	n = mountwatch_struct.dir_change, mountwatch_struct.dir_change = 0;
	pthread_mutex_unlock(&mountwatch_struct.dir_watch_mutex);
	if (!change.entries && !n)
		goto wait;
	if (n)
		mountwatch_inotify_handle(&change), sem_post(&mountwatch_struct.dir_watch_continue);
	
	return change;
}


void mountwatch_change_free(struct mountwatch_change_s change) {
	int i;
	for (i = 0; i < change.entries; i++)
		free(change.entry[i].device), free(change.entry[i].mount), free(change.entry[i].path);
	free(change.entry);
	return;
}
