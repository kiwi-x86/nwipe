/*
 *  create_compact_pdf.h: Compact, single file erasure certificate.
 *
 *  Fork-specific, see FORK.md.
 *
 *  This program is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free Software
 *  Foundation, version 2.
 */

#ifndef CREATE_COMPACT_PDF_H_
#define CREATE_COMPACT_PDF_H_

#include "context.h"

/* Write one PDF that certifies every wiped disk of this host: host identity,
 * erasure parameters and one table row per disk. Fits on a single A4 page for
 * up to ~30 disks, continuation pages are added automatically beyond that.
 * Returns 0 on success, -1 on error. */
int create_compact_pdf( nwipe_thread_data_ptr_t* ptrx );

#endif /* CREATE_COMPACT_PDF_H_ */
