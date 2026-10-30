/*
 * rescue-updater -- update-binary for the M20-T stock recovery (Android 4.4).
 *
 * Invoked by stock recovery as:
 *     /tmp/update_binary <UI-fd> <package.zip> /tmp
 *
 * Self-contained: walks the zip central directory itself, extracts the
 * STORE-method entry "boot.img" to /tmp/boot.img, then writes it raw to the
 * boot partition and reports through the UI pipe.  No external binaries, no
 * /system dependence, no floating point.  Build: -static cross armv7.
 *
 * Exit 0 == install success; exit 1 == failure (message on screen).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

static int uifd = 3;

static void wr_ui(const char *s)
{
	char buf[560];
	int n = snprintf(buf, sizeof(buf), "ui_print %s\nui_print\n", s);
	if (uifd > 0 && n > 0)
		(void)!write(uifd, buf, (size_t)n);
	(void)!fputs(s, stderr);
	(void)!fputc('\n', stderr);
}

static void fail(const char *s)
{
	char buf[560];
	snprintf(buf, sizeof(buf), "E:%s", s);
	wr_ui(buf);
	exit(1);
}

static uint16_t le16(const unsigned char *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t le32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

#define EOCD_SIG 0x06054b50UL
#define CDH_SIG  0x02014b50UL
#define LFH_SIG  0x04034b50UL

/* find EOCD signature by backwards scan, return its file offset */
static long find_eocd(FILE *z)
{
	long zlen = -1;

	fseek(z, 0, SEEK_END);
	zlen = ftell(z);
	if (zlen < 22)
		return -1;

	long scan = zlen > 220000L ? zlen - 220000L : 0;
	/* 220KB covers EOCD + huge trailing comment safety margin */
	for (long p = zlen - 22; p >= scan; p--) {
		fseek(z, p, SEEK_SET);
		int d0 = fgetc(z);
		if (d0 != (int)(EOCD_SIG & 0xff))
			continue;
		unsigned char b[3];
		if (fread(b, 1, 3, z) != 3)
			break;
		if ((uint32_t)d0 == (EOCD_SIG & 0xff) && b[0] == 'K' &&
		    b[1] == 5 && b[2] == 6)
			return p;
	}
	return -1;
}

enum { F_OK_NOEOCD = 1, F_OK_NOENTRY, F_OK_BADMETHOD, F_OK_SHRINK, F_OK_LOCAL,
       F_OK_OPEN };

static int find_stored_entry(FILE *z, const char *name, long *data_out,
			     uint32_t *size_out)
{
	unsigned char cd[46], lfh[30], nm[1024];
	uint32_t cdo;
	long eocd = find_eocd(z);
	if (eocd < 0)
		return F_OK_NOEOCD;

	fseek(z, eocd + 4, SEEK_SET);
	unsigned char e[18];
	if (fread(e, 1, 18, z) != 18)
		return F_OK_NOEOCD;
	cdo = le32(e + 12);

	fseek(z, (long)cdo, SEEK_SET);
	for (;;) {
		if (fread(cd, 1, 46, z) != 46)
			return F_OK_NOENTRY;
		if (le32(cd) != CDH_SIG)
			return F_OK_NOENTRY;
		uint16_t fnl = le16(cd + 28);
		uint16_t exl = le16(cd + 30);
		uint16_t cml = le16(cd + 32);
		uint32_t csz = le32(cd + 20);
		uint16_t meth = le16(cd + 10);
		long lho = (long)le32(cd + 42);

		if (fnl >= sizeof(nm))
			fnl = (uint16_t)(sizeof(nm) - 1);
		if (fnl && fread(nm, 1, fnl, z) != fnl)
			return F_OK_NOENTRY;
		nm[fnl] = 0;

		if (strcmp((char *)nm, name) == 0) {
			if (meth != 0)
				return F_OK_BADMETHOD;
			fseek(z, lho, SEEK_SET);
			if (fread(lfh, 1, 30, z) != 30)
				return F_OK_LOCAL;
			if (le32(lfh) != LFH_SIG)
				return F_OK_LOCAL;
			uint16_t lfn = le16(lfh + 26);
			uint16_t lex = le16(lfh + 28);
			long data = lho + 30 + lfn + lex;
			if (csz == 0)
				return F_OK_SHRINK;
			(void)exl;
			(void)cml;
			*size_out = csz;
			*data_out = data;
			return 0;
		}
		/* skip rest of this central entry (extra+comment) and continue */
		fseek(z, (long)(exl + cml), SEEK_CUR);
	}
}

int main(int argc, char **argv)
{
	if (argc >= 2) {
		int fd = atoi(argv[1]);
		if (fd > 0)
			uifd = fd;
	}
	const char *zip = (argc >= 3) ? argv[2] : "/tmp/update.zip";
	const char *dir = (argc >= 4) ? argv[3] : "/tmp";

	char buf[560];
	wr_ui("M20-T rescue: flashing mainline boot");

	FILE *z = fopen(zip, "rb");
	if (!z) {
		snprintf(buf, sizeof(buf), "cannot open package (errno=%d)", errno);
		fail(buf);
	}

	long data = 0;
	uint32_t sz = 0;
	int fe = find_stored_entry(z, "boot.img", &data, &sz);
	if (fe != 0) {
		const char *why[] = { "ok", "not a zip (no EOCD)",
			"boot.img not in package", "boot.img is compressed",
			"boot.img empty", "bad local header", "open failed" };
		const char *w = (fe < 7) ? why[fe] : "unknown";
		(void)F_OK_OPEN;
		fail(w);
	}

	char tmpimg[300];
	snprintf(tmpimg, sizeof(tmpimg), "%s/boot.img", dir);
	FILE *out = fopen(tmpimg, "wb");
	if (!out) {
		snprintf(buf, sizeof(buf), "cannot create %s (errno=%d)", tmpimg, errno);
		fail(buf);
	}

	fseek(z, data, SEEK_SET);
	unsigned char chunk[65536];
	uint32_t left = sz;
	while (left > 0) {
		uint32_t want = (left > sizeof(chunk)) ? sizeof(chunk) : left;
		if (fread(chunk, 1, want, z) != want)
			fail("short read from package");
		if (fwrite(chunk, 1, want, out) != want)
			fail("write to tmp failed");
		left -= want;
	}
	fclose(out);
	fclose(z);
	sync();

	snprintf(buf, sizeof(buf), "boot.img extracted: %u bytes", sz);
	wr_ui(buf);

	const char *bootdev[] = {
		"/dev/block/bootdevice/by-name/boot",
		"/dev/block/platform/7824900.sdhci/by-name/boot",
		NULL
	};
	int fd = -1;
	const char *dev = NULL;
	for (int i = 0; bootdev[i]; i++) {
		fd = open(bootdev[i], O_WRONLY);
		if (fd >= 0) {
			dev = bootdev[i];
			break;
		}
	}
	if (fd < 0) {
		snprintf(buf, sizeof(buf), "cannot open boot partition (errno=%d)", errno);
		fail(buf);
	}
	snprintf(buf, sizeof(buf), "writing %s ...", dev);
	wr_ui(buf);

	FILE *src = fopen(tmpimg, "rb");
	if (!src)
		fail("tmp image vanished");

	uint32_t total = sz;
	uint32_t at = 0;
	while (at < total) {
		uint32_t want = (total - at > sizeof(chunk)) ? sizeof(chunk) : (total - at);
		if (fread(chunk, 1, want, src) != want)
			fail("tmp read failed");
		uint32_t off = 0;
		while (off < want) {
			ssize_t w = write(fd, chunk + off, want - off);
			if (w < 0) {
				snprintf(buf, sizeof(buf), "write failed at %u (errno=%d)",
					 at + off, errno);
				fail(buf);
			}
			off += (uint32_t)w;
		}
		at += want;
	}
	fclose(src);

	/* read-back verification of the first 64KB */
	{
		int rf = open(dev, O_RDONLY);
		if (rf >= 0) {
			unsigned char v1[65536], v2[65536];
			ssize_t r1 = read(rf, v1, sizeof(v1));
			close(rf);
			FILE *c = fopen(tmpimg, "rb");
			if (c) {
				(void)!fread(v2, 1, sizeof(v2), c);
				fclose(c);
				if (r1 < 0 || memcmp(v1, v2, (size_t)r1) != 0)
					fail("read-back verification MISMATCH");
				else
					wr_ui("read-back verified (64KB)");
			}
		}
	}

	fsync(fd);
	close(fd);
	sync();

	wr_ui("boot partition flashed OK");
	wr_ui("menu > Reboot system now, mainline Linux boots headless");
	wr_ui("USB: NCM network, telnet 172.16.42.1");
	exit(0);
}
