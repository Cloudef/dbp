#ifndef __DBP_H__
#define	__DBP_H__


#define	DBP_DBUS_CLIENT_PREFIX	"de.dragonbox.DBP.run"
#define	DBP_DBUS_DAEMON_PREFIX	"de.dragonbox.PackageDaemon"
#define	DBP_DBUS_DAEMON_OBJECT	"/de/dragonbox/PackageDaemon"

#define	DBP_FS_NAME		"squashfs"
#define	DBP_UNIONFS_NAME	"aufs"


/* ERROR CODES */

#define	DBP_ERROR_NO_REPLY	-1000	/* dbp daemon did not reply */
#define	DBP_ERROR_INTERNAL_MSG	-1001	/* internal dbp message error */
#define	DBP_ERROR_NO_LOOP	-1002	/* No loop device available */
#define	DBP_ERROR_SET_LOOP	-1003	/* Error setting up loop device */
#define	DBP_ERROR_SET_LOOP2	-1004	/* Error setting up loop device */
#define	DBP_ERROR_NO_PKG_ACCESS	-1005	/* Package file couldn't be opened */
#define	DBP_ERROR_NO_MEMORY	-1006	/* A malloc failed or something */
#define	DBP_ERROR_BAD_PKG_ID	-1007	/* Package doesn't exist in database */
#define	DBP_ERROR_BAD_FSIMG	-1008	/* Package doesn't have a valid FS */
#define	DBP_ERROR_ILL_DIRNAME	-1009	/* A mountpoint contained an illegal char */
#define	DBP_ERROR_UNION_FAILED	-1010	/* UnionFS mount failed */
#define	SBP_ERROR_APPD_NOPERM	-1011	/* Unable to create appdata directory */

#endif