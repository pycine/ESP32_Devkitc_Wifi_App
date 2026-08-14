#ifndef ATTENDANCE_H_
#define ATTENDANCE_H_

#include <stdbool.h>

/**
 * Start the background thread that reports scan events to the server.
 * Call once, after WiFi is connected.
 */
void attendance_start_reporting(void);

/**
 * Queue a scan event to be reported to the server (entry/exit toggling and
 * "who attempted and was denied" are computed server-side, since the
 * server has the full history — the device just reports what happened
 * locally: which UID scanned, and whether the local whitelist granted it).
 *
 * Non-blocking and never fails loudly: if the report queue is full (e.g.
 * the server's been unreachable for a while), the event is dropped and a
 * warning is logged, but the RFID scan loop is never blocked or delayed
 * waiting on the network.
 *
 * @param rfid_uid   scanned UID, e.g. "04A1B2C3"
 * @param authorized whatever employees_is_authorized() returned for it
 */
void attendance_report_event(const char *rfid_uid, bool authorized);

#endif /* ATTENDANCE_H_ */
