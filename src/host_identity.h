/*
 *  host_identity.h: Host identification helpers (hostname, DMI serials).
 *
 *  Fork-specific. Used by the GUI header and the PDF
 *  reports so every output identifies the physical server in the same way.
 *
 *  This program is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free Software
 *  Foundation, version 2.
 */

#ifndef HOST_IDENTITY_H_
#define HOST_IDENTITY_H_

#include <stddef.h>

#define NWIPE_HOSTNAME_LENGTH 256 /* RFC 1035 caps a FQDN at 255 chars */
#define NWIPE_DMI_SERIAL_LENGTH 128

typedef struct
{
    char hostname[NWIPE_HOSTNAME_LENGTH]; /* gethostname(), "" if unknown */
    char system_serial[NWIPE_DMI_SERIAL_LENGTH]; /* DMI type 1, /sys/class/dmi/id/product_serial */
    char chassis_serial[NWIPE_DMI_SERIAL_LENGTH]; /* DMI type 3, /sys/class/dmi/id/chassis_serial */
} nwipe_host_identity_t;

/* Read a DMI/SMBIOS string field from sysfs into 'out'. Missing files and
 * vendor placeholder values ("None", "Not Specified", ...) give "". */
void nwipe_read_dmi_field( const char* path, char* out, size_t len );

/* Populate 'id' with the hostname and both serial numbers. When 'anonymize'
 * is non zero (nwipe --quiet) every value is replaced by "XXXXXXXXXXXXXXX"
 * if it is known, mirroring upstream's anonymisation of DMI data. */
void nwipe_host_identity_get( nwipe_host_identity_t* id, int anonymize );

/* Copy 'in' to 'out' with every character that is not [A-Za-z0-9.-]
 * replaced by '_', suitable for use in a filename. "" becomes "unknown". */
void nwipe_host_identity_filename_safe( const char* in, char* out, size_t len );

#endif /* HOST_IDENTITY_H_ */
