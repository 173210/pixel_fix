/*
 * Copyright (C) 2014 173210 <root.3.173210@live.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspge.h>

PSP_MODULE_INFO("PIXEL_FIX", PSP_MODULE_KERNEL, 0, 0);

static SceUID thid;

static struct {
	char *top;
	char *end;
} buf;
static char *vram_end;
static int cur_pixelformat;

static int (* _orig_sceDisplayWaitVblankStart)();
static int (* _orig_sceDisplaySetFrameBuf)(void *topaddr, int bufferwidth, int pixelformat, int sync);
static int (* _orig_sceDisplayGetFrameBuf)(void **topaddr, int *bufferwidth, int *pixelformat, int sync);

static int _hook_sceDisplaySetFrameBuf(
	void *topaddr, int bufferwidth, int pixelformat, int sync)
{
	char *new_buf_top;
	char *new_buf_end;
	int ret;

	if (topaddr == NULL)
		return SCE_KERNEL_ERROR_ILLEGAL_ADDR;

	switch (pixelformat) {
		case PSP_DISPLAY_PIXEL_FORMAT_565:
			bufferwidth *= 2;
			new_buf_top = new_buf_end
				= ((int)vram_end - (int)buf.end < bufferwidth) ?
					vram_end - bufferwidth :
					buf.top - bufferwidth;
			cur_pixelformat = pixelformat;

			while (bufferwidth > 0) {
				*new_buf_end++ = 0xFF;
				*new_buf_end++ = (*(short *)topaddr & 0x1F) << 3;
				*new_buf_end++ = ((*(short *)topaddr >> 5) & 0x3F) << 2;
				*new_buf_end++ = (*(short *)topaddr >> 11) << 3;

				bufferwidth -= sizeof(short);
			}

			break;

		case PSP_DISPLAY_PIXEL_FORMAT_5551:
			bufferwidth *= 2;
			new_buf_top = new_buf_end
				= ((int)vram_end - (int)buf.end < bufferwidth) ?
					vram_end - bufferwidth :
					buf.top - bufferwidth;
			cur_pixelformat = pixelformat;

			while (bufferwidth > 0) {
				*new_buf_end++ = (*(short *)topaddr & 1) << 7;
				*new_buf_end++ = ((*(short *)topaddr >> 1) & 0x1F) << 3;
				*new_buf_end++ = ((*(short *)topaddr >> 6) & 0x1F) << 3;
				*new_buf_end++ = (*(short *)topaddr >> 11) << 3;

				bufferwidth -= sizeof(short);
			}

			break;

		case PSP_DISPLAY_PIXEL_FORMAT_4444:
			bufferwidth *= 2;
			new_buf_top = new_buf_end
				= ((int)vram_end - (int)buf.end < bufferwidth) ?
					vram_end - bufferwidth :
					buf.top - bufferwidth;
			cur_pixelformat = pixelformat;

			while (bufferwidth > 0) {
				*new_buf_end++ = (*(short *)topaddr & 0xF) << 4;
				*new_buf_end++ = ((*(short *)topaddr >> 4) & 0xF) << 4;
				*new_buf_end++ = ((*(short *)topaddr >> 8) & 0xF) << 4;
				*new_buf_end++ = (*(short *)topaddr >> 12) << 4;

				bufferwidth -= sizeof(short);
			}

			break;

		default:
			ret = _orig_sceDisplaySetFrameBuf(topaddr, bufferwidth, pixelformat, sync);

			if (!ret) {
				buf.top = vram_end;
				buf.end = vram_end;
				cur_pixelformat = pixelformat;
			}

			return ret;
	}

	_orig_sceDisplayWaitVblankStart();
	ret = _orig_sceDisplaySetFrameBuf(new_buf_top, bufferwidth, PSP_DISPLAY_PIXEL_FORMAT_8888, sync);

	if (!ret) {
		buf.top = new_buf_top;
		buf.end = new_buf_end;
		cur_pixelformat = pixelformat;
	}

	return ret;
}

static int _hook_sceDisplayGetFrameBuf(void **topaddr, int *bufferwidth, int *pixelformat, int sync)
{
	int ret = _orig_sceDisplayGetFrameBuf(topaddr, bufferwidth, pixelformat, sync);

	if (!ret && pixelformat != NULL)
		*pixelformat = cur_pixelformat;

	return ret;
}

static struct SceLibraryEntryTable *FindLibrary(const char *module, const char *library)
{
	const SceModule *p;
	struct SceLibraryEntryTable *entry;
	int i, j;

	if (module == NULL || library == NULL)
		return NULL;

	p = sceKernelFindModuleByName(module);
	if (p == NULL)
		return NULL;

	entry = p->ent_top;
	for (i = p->ent_size; i > 0; i -= entry->len * 4) {
		if (entry->libname != NULL && library != NULL)
			for (j = 0; entry->libname[j] == library[j]; j++)
				if (!library[j])
					return entry;

		entry++;
	}

	return NULL;
}

static void *FindExport(const struct SceLibraryEntryTable *entry, int nid)
{
	int i;

	if (entry == NULL)
		return NULL;

	for (i = 0; i < entry->stubcount; i++)
		if (((int *)entry->entrytable)[i] == nid)
			return *((void **)entry->entrytable + entry->stubcount + entry->vstubcount + i);

	return NULL;
}


static int HookSyscall(void *orig, void *hook)
{
	void *p;
	void **tbl;
	int size;

	if (orig == NULL || hook == NULL)
		return -1;

	__asm__("cfc0 %0, $12" : "=r"(p));

	tbl = (void **)p + 4;
	for (size = ((int *)p)[3] - 16; size > 0; size -= sizeof(void *)) {
		if (*tbl == orig) {
			*tbl = hook;
			return 0;
		}
		tbl++;
	}

	return -1;
}

static int mainThread()
{
	const struct SceLibraryEntryTable *entry;

	vram_end = (void *)((int)sceGeEdramGetAddr() | 0x40000000) + sceGeEdramGetSize();
	buf.top = vram_end;
	buf.end = vram_end;

	do
		entry = FindLibrary("sceDisplay_Service", "sceDisplay");
	while (entry == NULL);

	_orig_sceDisplayWaitVblankStart = FindExport(entry, 0x46F186C3);
	if (_orig_sceDisplayWaitVblankStart == NULL)
		return -1;
	_orig_sceDisplaySetFrameBuf = FindExport(entry, 0x289D82FE);
	if (_orig_sceDisplaySetFrameBuf == NULL)
		return -1;
	_orig_sceDisplayGetFrameBuf = FindExport(entry, 0xEEDA2E54);
	if (_orig_sceDisplayGetFrameBuf == NULL)
		return -1;

	if (HookSyscall(_orig_sceDisplaySetFrameBuf, _hook_sceDisplaySetFrameBuf))
		return -1;
	if (HookSyscall(_orig_sceDisplayGetFrameBuf, _hook_sceDisplayGetFrameBuf))
		return -1;

	thid = -1;
	return 0;
}

int module_start(SceSize arglen, void *argp)
{
	thid = sceKernelCreateThread(module_info.modname, mainThread, 111, 512, 0, NULL);
	return thid < 0 ? thid : sceKernelStartThread(thid, arglen, argp);
}

int module_stop()
{
	if (thid >= 0)
		sceKernelTerminateDeleteThread(thid);

	if (_orig_sceDisplaySetFrameBuf != NULL)
		HookSyscall(_hook_sceDisplaySetFrameBuf, _orig_sceDisplaySetFrameBuf);
	if (_orig_sceDisplayGetFrameBuf != NULL)
		HookSyscall(_hook_sceDisplayGetFrameBuf, _orig_sceDisplayGetFrameBuf);

	return 0;
}
