# nwipe fork notes

Upstream: https://github.com/martijnvanbrummelen/nwipe
Current base: upstream **v0.43.1**

This file lists every change the fork carries on top of upstream so the changes can
be re-applied when a new upstream release comes out. All fork code is marked in the
source with a `FORK` comment, so `grep -rn FORK src/` shows
every touched spot.

---

## 1. Features

### 1.1 Host identity in the GUI header
The ncurses header shows the **hostname**, **system S/N** and **chassis S/N** so the
operator can tell which machine is being wiped.

- Data is read through `nwipe_host_identity_get()` (see 1.2).
- `--quiet` does not anonymise the GUI header (it is only shown on the local console).

### 1.2 `host_identity.{c,h}` (new files)
A shared helper used by the GUI and by all PDF reports.

| Function | Purpose |
|---|---|
| `nwipe_read_dmi_field(path, out, len)` | Reads `/sys/class/dmi/id/<field>`, trims it and drops vendor placeholders ("To Be Filled By O.E.M.", "Default string", "System Serial Number", ...). Result is "" if not usable. |
| `nwipe_host_identity_get(id, anonymize)` | Fills `nwipe_host_identity_t {hostname, system_serial, chassis_serial}`. Empty values become `Unknown`. When `anonymize` is set (`--quiet`), every value becomes `XXXXXXXXXXXXXXX`. |
| `nwipe_host_identity_filename_safe(in, out, len)` | Maps `[^A-Za-z0-9.-]` to `_`. An empty result becomes `unknown`. Used for PDF filenames. |

### 1.3 Compact PDF report (new, default)
`create_compact_pdf.{c,h}` writes **one PDF per nwipe run** that covers all wiped
disks. It replaces upstream's N+1 files (one per disk plus one system report).

It contains only the information needed for an erasure record:

- **Title + overall result badge**: `ERASED` (green), `ERASED (!)` (amber, some disk
  needed I/O retries), `FAILED` (red, some disk failed or was aborted).
- **Optional company logo** top left, before the title (see 1.8).
- **Host**: hostname, system S/N, chassis S/N, model (dmidecode manufacturer + product,
  falling back to sysfs, else `Unknown`), start and finish times.
- **Parameters**: method (with direction), PRNG (`N/A (no random data)` for zero/one
  methods), verify, blanking, rounds, number of disks.
- **Organisation / Customer / Tag**: printed only when actually configured. Upstream
  "Not Applicable (...)" defaults and the `Empty Tag` default are skipped. The tag is
  only shown when `PDFtag` is on.
- **Disk table**: Device, Model, Serial, Size, Duration, Errors (pass+sync+verify,
  `+nR` = I/O retries), Status.
- **HPA/DCO is intentionally not shown and does not affect the status.** Upstream's
  certificate marks a disk "ERASED with warnings" whenever HPA/DCO state is not
  "disabled". Behind RAID controllers (e.g. Dell PERC H755) hdparm cannot query it, so
  every disk came out as `Unknown` / `ERASED (!)` although it was fully wiped and
  verified. In the compact report a disk is `ERASED` when nwipe reports it erased with
  no I/O retries, `ERASED (!)` only when I/O retries happened, and otherwise shows
  nwipe's own status (`FAILED`, `ABORTED`, ...). HPA/DCO details remain in the nwipe
  log and in the FULL (upstream) reports.
- A legend line appears only when some row has a warning or retries.
- Technician / signature / date lines, plus a footer with the nwipe version, hostname
  and `Page x of y`.
- Long runs continue on extra pages with a short continuation header: about 40 disks
  fit on page 1 and about 55 fit on each later page.

Filename:
```
nwipe_compact_report_<YYYY-mm-dd_HH-MM-SS>_host-<hostname>_SN-<system S/N | chassis S/N | unknown>.pdf
```

### 1.4 PDF report mode: `--pdfmode` / `PDF_Report_Mode`
New option to choose which reports are written:

| Mode | Files written |
|---|---|
| `COMPACT` (default) | 1 compact report |
| `FULL` | upstream behaviour: one report per disk + system report |
| `BOTH` | all of the above |

- CLI: `--pdfmode=compact|full|both` (case-insensitive). Any other value exits with an error.
- Config: `PDF_Certificate.PDF_Report_Mode = "COMPACT"` in `/etc/nwipe/nwipe.conf`
  (added automatically by `conf.c`).
- The CLI value overrides the config value.
- All modes still require PDF output to be enabled (`PDF_Enable` / `--PDFreportpath`).

**Behaviour change vs upstream:** upstream always writes the system report, even when
PDF output is disabled. In the fork, the system report is written only when
`PDF_enable == 1` and the mode is `FULL` or `BOTH`.

### 1.5 Hostname in upstream (FULL) reports
- Footer of every page of the per-disk and system reports:
  `Disc Erasure by NWIPE version <v> - Host: <hostname>` (hostname capped at 48 chars
  so the footer stays under `MAX_PDF_FOOTER_TEXT_LENGTH`).
- Per-disk filename:
  `nwipe_report_<time>_host-<host>_Model_<model>_Serial_<serial>_device_<dev>.pdf`
- System report filename:
  `nwipe_system_report_<time>_host-<host>_SN-<system S/N | chassis S/N | unknown>.pdf`
  (the UUID is no longer part of the filename).

### 1.6 Bug fixes / robustness
- `create_single_disc_pdf()`: upstream sanitises `c->device_model` and
  `c->device_serial_no` **in place** while building the filename. The fork sanitises
  copies, so reports written later (compact report) still see the real values.
- `create_system_multi_disc_pdf()`: dmidecode fields are no longer modified while
  building the filename.
- `nwipe.c`: `nwipe_misc_thread_data` is zeroed with `memset()` before use. Upstream
  leaves it uninitialised, so when dmidecode returns nothing (containers, some VMs) the
  dmidecode fields contain stack garbage.

### 1.7 Not changed (on purpose)
- Standalone hardware Secure Erase / Sanitize (`se_ata_gui.c`, `se_nvme_gui.c`) still
  calls `create_single_disc_pdf()` directly, as upstream does. These runs produce the
  upstream per-disk report (now with hostname in the footer and filename) whatever the
  `--pdfmode` setting.

---

### 1.8 Company logo (build time only)
The logo is compiled into the binary; there is no runtime option or config key.

```sh
./configure --with-logo=/path/logo.png      # or .jpg
NWIPE_LOGO=/path/logo.png sudo ./scripts/install.sh
```

- `configure` checks the file: PNG or JPEG only. PNG with transparency (alpha) or
  interlacing is rejected with a hint, because PDFGen cannot embed those. Flatten
  onto white first: `convert logo.png -background white -alpha remove -alpha off out.png`.
- `src/Makefile.am` turns the file into `src/nwipe_logo.h` (byte array, made with `od`)
  and defines `NWIPE_HAVE_LOGO`. Without `--with-logo` the header is empty and the
  report looks as before.
- Drawn into a 120 x 40 pt box, aspect ratio kept. If the image cannot be added, a
  warning is logged and the report is written without it.
- Keep the logo file outside the repo.

## 2. Files touched

| File | Change |
|---|---|
| `src/host_identity.c`, `src/host_identity.h` | **new**, see 1.2 |
| `src/create_compact_pdf.c`, `src/create_compact_pdf.h` | **new**, see 1.3 |
| `src/Makefile.am` | add `host_identity.c/.h` to `NWIPE_CORE` (after `display_help.c`), add `create_compact_pdf.c/.h` to `NWIPE_PDF`, generate `nwipe_logo.h` |
| `configure.ac` | `--with-logo=FILE` (checks file type), summary line |
| `.gitignore` | `src/nwipe_logo.h` |
| `src/gui.c` | include `host_identity.h`; header shows hostname / system S/N / chassis S/N |
| `src/logging.c` | include `create_compact_pdf.h`; in `nwipe_log_summary()` gate per-disk and system PDFs by mode, call `create_compact_pdf()` |
| `src/create_single_disc_pdf.c` | hostname in footer + filename; sanitise copies |
| `src/create_system_multi_disc_pdf.c` | hostname in footer; new filename; no mutation of dmidecode fields |
| `src/options.h` | `NWIPE_PDF_MODE_COMPACT/FULL/BOTH`, `int PDF_report_mode` |
| `src/options.c` | `--pdfmode` parsing, reads `PDF_Certificate.PDF_Report_Mode` |
| `src/conf.c` | default `PDF_Certificate.PDF_Report_Mode = "COMPACT"` |
| `src/display_help.c`, `man/nwipe.8` | document `--pdfmode` |
| `src/nwipe.c` | `memset()` of `nwipe_misc_thread_data` |
| `CHANGELOG.md` | fork entry at the top |
| `FORK.md` | this file |

---

## 3. Re-applying on a new upstream release

```sh
git fetch upstream --tags
git checkout -b update-vX.Y master
git merge vX.Y            # or: git rebase vX.Y
grep -rn "FORK" src/      # check every fork hunk survived
```

Hot spots most likely to conflict:
1. `logging.c` `nwipe_log_summary()`: the PDF call block at the end. Keep the
   upstream per-disk loop, wrap it in the mode check, gate the system report, and add
   the compact call.
2. `create_single_disc_pdf.c` / `create_system_multi_disc_pdf.c`: footer `snprintf`
   and filename block.
3. `options.c`: long-options table, `getopt` switch and the config-reading section.
4. `gui.c`: header banner built in `nwipe_gui_create_header_window()`.
5. `Makefile.am`: source lists.
6. `nwipe_thread_data_ptr_t` / `nwipe_context_t`: if upstream renames fields used by
   `create_compact_pdf.c` (`start_time`, `end_time`, `pass_errors`, `fsyncdata_errors`,
   `verify_errors`, `io_retries`, `HPA_status`, `wipe_status`, `device_size_text`, ...),
   update the compact report.

## 4. Testing (Docker only)
The fork is built and tested in Docker against loop devices (no real hardware):

```sh
docker run --rm --privileged --hostname wipe-test01 \
  -v <repo>:/src:ro -v $PWD:/work [-e NDISK=70 -e DISKSIZE=2M] -v $PWD/out:/out \
  nwipe-build /work/run-test.sh [--pdfmode=full|both] [--quiet]
```
Run tests one after another; parallel runs collide on the host loop devices.
Checks: number of files per mode, page count (`pdfinfo`), header/footer text
(`pdftotext`), visual check (`pdftoppm -png`), 2 / 40 / 70 disks, `--quiet`
anonymisation, invalid `--pdfmode` value.
