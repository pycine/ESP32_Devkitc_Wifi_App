#ifndef EMPLOYEE_SYNC_H_
#define EMPLOYEE_SYNC_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * Sync the local RFID access whitelist with the server using
 * "Method 6: Timestamp Sync + Single File Update".
 *
 * Sends the last sync cursor, receives only the UIDs added/removed since
 * then, and merges them into the on-flash whitelist (/storage/access_list.txt).
 * The cursor is advanced using the value the SERVER returns, never a value
 * computed from the ESP32's own clock, so this stays correct even while
 * NTP is misbehaving.
 *
 * The device does not need or store employee names — only the fact that a
 * given UID is currently authorized.
 *
 * @return 0 on success (even with zero changes), negative errno on
 *         network / parse / storage failure. On failure the on-flash
 *         whitelist and sync cursor are left untouched.
 */
int employees_sync(void);

/**
 * Check whether a scanned RFID UID is currently authorized.
 * This is a pure membership test against the local whitelist —
 * present = access granted, absent = access denied. Fail-closed:
 * any lookup that can't be resolved (e.g. cache not yet loaded and
 * load fails) returns false.
 *
 * @param rfid_uid  Null-terminated UID string, e.g. "04A1B2C3"
 * @return true if authorized, false otherwise
 */
bool employees_is_authorized(const char *rfid_uid);

#endif /* EMPLOYEE_SYNC_H_ */