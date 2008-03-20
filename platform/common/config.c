/*
 * Human-readable config file management for PicoDrive
 * (c)
 */

#include <string.h>
#include <stdlib.h>
#include "config.h"
#include "menu.h"
#include "emu.h"
#include <Pico/Pico.h>

extern menu_entry opt_entries[];
extern menu_entry opt2_entries[];
extern menu_entry cdopt_entries[];
extern const int opt_entry_count;
extern const int opt2_entry_count;
extern const int cdopt_entry_count;

static menu_entry *cfg_opts[] = { opt_entries, opt2_entries, cdopt_entries };
static const int *cfg_opt_counts[] = { &opt_entry_count, &opt2_entry_count, &cdopt_entry_count };

#define NL "\n"


static int seek_sect(FILE *f, const char *section)
{
	char line[128], *tmp;
	int len;

	len = strlen(section);
	// seek to the section needed
	while (!feof(f))
	{
		tmp = fgets(line, sizeof(line), f);
		if (tmp == NULL) break;

		if (line[0] != '[') continue; // not section start
		if (strncmp(line + 1, section, len) == 0 && line[len+1] == ']')
			return 1; // found it
	}

	return 0;
}


static void custom_write(FILE *f, const menu_entry *me)
{
	char *str, str24[24];

	switch (me->id)
	{
		case MA_OPT_RENDERER:
			if (!((defaultConfig.s_PicoOpt^PicoOpt)&0x10) &&
				!((defaultConfig.EmuOpt^currentConfig.EmuOpt)&0x80)) return;
			if (PicoOpt&0x10)
				str = "8bit fast";
			else if (currentConfig.EmuOpt&0x80)
				str = "16bit accurate";
			else
				str = "8bit accurate";
			fprintf(f, "Renderer = %s", str);
			break;

		case MA_OPT_SCALING:
			if (defaultConfig.scaling == currentConfig.scaling) return;
			switch (currentConfig.scaling) {
				default: str = "OFF"; break;
				case 1:  str = "hw horizontal";     break;
				case 2:  str = "hw horiz. + vert."; break;
				case 3:  str = "sw horizontal";     break;
			}
			fprintf(f, "Scaling = %s", str);
			break;
		case MA_OPT_FRAMESKIP:
			if (defaultConfig.Frameskip == currentConfig.Frameskip) return;
			if (currentConfig.Frameskip < 0)
			     strcpy(str24, "Auto");
			else sprintf(str24, "%i", currentConfig.Frameskip);
			fprintf(f, "Frameskip = %s", str24);
			break;
		case MA_OPT_SOUND_QUALITY:
			if (!((defaultConfig.s_PicoOpt^PicoOpt)&8) && 
				defaultConfig.s_PsndRate == PsndRate) return;
			str = (PicoOpt&0x08)?"stereo":"mono";
			fprintf(f, "Sound Quality = %i %s", PsndRate, str);
			break;
		case MA_OPT_REGION:
			if (defaultConfig.s_PicoRegion == PicoRegionOverride &&
				defaultConfig.s_PicoAutoRgnOrder == PicoAutoRgnOrder) return;
			fprintf(f, "Region = %s", me_region_name(PicoRegionOverride, PicoAutoRgnOrder));
			break;
		case MA_OPT_CONFIRM_STATES:
			if (!((defaultConfig.EmuOpt^currentConfig.EmuOpt)&(5<<9))) return;
			switch ((currentConfig.EmuOpt >> 9) & 5) {
				default: str = "OFF";    break;
				case 1:  str = "writes"; break;
				case 4:  str = "loads";  break;
				case 5:  str = "both";   break;
			}
			fprintf(f, "Confirm savestate = %s", str);
			break;
		case MA_OPT_CPU_CLOCKS:
			if (defaultConfig.CPUclock == currentConfig.CPUclock) return;
			fprintf(f, "GP2X CPU clocks = %i", currentConfig.CPUclock);
			break;
		case MA_OPT2_GAMMA:
			if (defaultConfig.gamma == currentConfig.gamma) return;
			fprintf(f, "Gamma correction = %.3f", (double)currentConfig.gamma / 100.0);
			break;
		case MA_OPT2_SQUIDGEHACK:
			if (!((defaultConfig.EmuOpt^currentConfig.EmuOpt)&0x0010)) return;
			fprintf(f, "Squidgehack = %i", (currentConfig.EmuOpt&0x0010)>>4);
			break;
		case MA_CDOPT_READAHEAD:
			if (defaultConfig.s_PicoCDBuffers == PicoCDBuffers) return;
			sprintf(str24, "%i", PicoCDBuffers * 2);
			fprintf(f, "ReadAhead buffer = %s", str24);
			break;

		default:
			printf("unhandled custom_write: %i\n", me->id);
			return;
	}
	fprintf(f, NL);
}


int config_writesect(const char *fname, const char *section)
{
	FILE *fo = NULL, *fn = NULL; // old and new
	menu_entry *me;
	int t, i, tlen, ret;
	char line[128], *tmp;

	if (section != NULL)
	{
		fo = fopen(fname, "r");
		if (fo == NULL) {
			fn = fopen(fname, "w");
			goto write;
		}

		ret = seek_sect(fo, section);
		if (!ret) {
			// sect not found, we can simply append
			fclose(fo); fo = NULL;
			fn = fopen(fname, "a");
			goto write;
		}

		// use 2 files..
		fclose(fo);
		rename(fname, "tmp.cfg");
		fo = fopen("tmp.cfg", "r");
		fn = fopen(fname, "w");
		if (fo == NULL || fn == NULL) goto write;

		// copy everything until sect
		tlen = strlen(section);
		while (!feof(fo))
		{
			tmp = fgets(line, sizeof(line), fo);
			if (tmp == NULL) break;

			if (line[0] == '[' && strncmp(line + 1, section, tlen) == 0 && line[tlen+1] == ']')
				break;
			fputs(line, fn);
		}

		// now skip to next sect
		while (!feof(fo))
		{
			tmp = fgets(line, sizeof(line), fo);
			if (tmp == NULL) break;
			if (line[0] == '[') {
				fseek(fo, -strlen(line), SEEK_CUR);
				break;
			}
		}
		if (feof(fo))
		{
			fclose(fo); fo = NULL;
			remove("tmp.cfg");
		}
	}
	else
	{
		fn = fopen(fname, "w");
	}

write:
	if (fn == NULL) {
		if (fo) fclose(fo);
		return -1;
	}
	if (section != NULL)
		fprintf(fn, "[%s]" NL, section);

	for (t = 0; t < sizeof(cfg_opts) / sizeof(cfg_opts[0]); t++)
	{
		me = cfg_opts[t];
		tlen = *(cfg_opt_counts[t]);
		for (i = 0; i < tlen; i++, me++)
		{
			if (!me->need_to_save) continue;
			if ((me->beh != MB_ONOFF && me->beh != MB_RANGE) || me->name == NULL)
				custom_write(fn, me);
			else if (me->beh == MB_ONOFF)
				fprintf(fn, "%s = %i" NL, me->name, (*(int *)me->var & me->mask) ? 1 : 0);
			else if (me->beh == MB_RANGE)
				fprintf(fn, "%s = %i" NL, me->name, *(int *)me->var);
		}
	}
	fprintf(fn, NL);

	if (fo != NULL)
	{
		// copy whatever is left
		while (!feof(fo))
		{
			tmp = fgets(line, sizeof(line), fo);
			if (tmp == NULL) break;

			fputs(line, fn);
		}
		fclose(fo);
		remove("tmp.cfg");
	}

	fclose(fn);
	return 0;
}


static void mystrip(char *str)
{
	int i, len;

	len = strlen(str);
	for (i = 0; i < len; i++)
		if (str[i] != ' ') break;
	if (i > 0) memmove(str, str + i, len - i + 1);
	len = strlen(str);
	for (i = len - 1; i >= 0; i--)
		if (str[i] != ' ') break;
	str[i+1] = 0;
}


int config_writelrom(const char *fname)
{
	char line[128], *tmp, *optr = NULL;
	char *old_data = NULL;
	int size;
	FILE *f;

	if (strlen(currentConfig.lastRomFile) == 0) return 0;

	f = fopen(fname, "r");
	if (f != NULL)
	{
		fseek(f, 0, SEEK_END);
		size = ftell(f);
		fseek(f, 0, SEEK_SET);
		old_data = malloc(size + size/8);
		if (old_data != NULL)
		{
			optr = old_data;
			while (!feof(f))
			{
				tmp = fgets(line, sizeof(line), f);
				if (tmp == NULL) break;
				mystrip(line);
				if (strncasecmp(line, "LastUsedROM", 11) == 0)
					continue;
				sprintf(optr, "%s", line);
				optr += strlen(optr);
			}
		}
		fclose(f);
	}

	f = fopen(fname, "w");
	if (f == NULL) return -1;

	if (old_data != NULL) {
		fwrite(old_data, 1, optr - old_data, f);
		free(old_data);
	}
	fprintf(f, "LastUsedROM = %s" NL, currentConfig.lastRomFile);
	fclose(f);
	return 0;
}


static int custom_read(menu_entry *me, const char *var, const char *val)
{
	char *tmp;
	int tmpi;

	switch (me->id)
	{
		case MA_OPT_RENDERER:
			if (strcasecmp(var, "Renderer") != 0) return 0;
			if      (strcasecmp(val, "8bit fast") == 0) {
				PicoOpt |=  0x10;
			}
			else if (strcasecmp(val, "16bit accurate") == 0) {
				PicoOpt &= ~0x10;
				currentConfig.EmuOpt |=  0x80;
			}
			else if (strcasecmp(val, "8bit accurate") == 0) {
				PicoOpt &= ~0x10;
				currentConfig.EmuOpt &= ~0x80;
			}
			else
				return 0;
			return 1;

		case MA_OPT_SCALING:
			if (strcasecmp(var, "Scaling") != 0) return 0;
			if        (strcasecmp(val, "OFF") == 0) {
				currentConfig.scaling = 0;
			} else if (strcasecmp(val, "hw horizontal") == 0) {
				currentConfig.scaling = 1;
			} else if (strcasecmp(val, "hw horiz. + vert.") == 0) {
				currentConfig.scaling = 2;
			} else if (strcasecmp(val, "sw horizontal") == 0) {
				currentConfig.scaling = 3;
			} else
				return 0;
			return 1;

		case MA_OPT_FRAMESKIP:
			if (strcasecmp(var, "Frameskip") != 0) return 0;
			if (strcasecmp(val, "Auto") == 0)
			     currentConfig.Frameskip = -1;
			else currentConfig.Frameskip = atoi(val);
			return 1;

		case MA_OPT_SOUND_QUALITY:
			if (strcasecmp(var, "Sound Quality") != 0) return 0;
			PsndRate = strtoul(val, &tmp, 10);
			if (PsndRate < 8000 || PsndRate > 44100)
				PsndRate = 22050;
			while (*tmp == ' ') tmp++;
			if        (strcasecmp(tmp, "stereo") == 0) {
				PicoOpt |=  8;
			} else if (strcasecmp(tmp, "mono") == 0) {
				PicoOpt &= ~8;
			} else
				return 0;
			return 1;

		case MA_OPT_REGION:
			if (strcasecmp(var, "Region") != 0) return 0;
			if       (strncasecmp(val, "Auto: ", 6) == 0)
			{
				const char *p = val + 5, *end = val + strlen(val);
				int i;
				PicoRegionOverride = PicoAutoRgnOrder = 0;
				for (i = 0; p < end && i < 3; p += 3, i++) {
					if        (p[0] == 'J' && p[1] == 'P') {
						PicoAutoRgnOrder |= 1 << (i*4);
					} else if (p[0] == 'U' && p[1] == 'S') {
						PicoAutoRgnOrder |= 4 << (i*4);
					} else if (p[0] == 'E' && p[1] == 'U') {
						PicoAutoRgnOrder |= 8 << (i*4);
					}
				}
			}
			else   if (strcasecmp(val, "Auto") == 0) {
				PicoRegionOverride = 0;
			} else if (strcasecmp(val, "Japan NTSC") == 0) {
				PicoRegionOverride = 1;
			} else if (strcasecmp(val, "Japan PAL") == 0) {
				PicoRegionOverride = 2;
			} else if (strcasecmp(val, "USA") == 0) {
				PicoRegionOverride = 4;
			} else if (strcasecmp(val, "Europe") == 0) {
				PicoRegionOverride = 8;
			} else
				return 0;
			return 1;

		case MA_OPT_CONFIRM_STATES:
			if (strcasecmp(var, "Confirm savestate") != 0) return 0;
			if        (strcasecmp(val, "OFF") == 0) {
				currentConfig.EmuOpt &= 5<<9;
			} else if (strcasecmp(val, "writes") == 0) {
				currentConfig.EmuOpt &= 5<<9;
				currentConfig.EmuOpt |= 1<<9;
			} else if (strcasecmp(val, "loads") == 0) {
				currentConfig.EmuOpt &= 5<<9;
				currentConfig.EmuOpt |= 4<<9;
			} else if (strcasecmp(val, "both") == 0) {
				currentConfig.EmuOpt &= 5<<9;
				currentConfig.EmuOpt |= 5<<9;
			} else
				return 0;
			return 1;

		case MA_OPT_CPU_CLOCKS:
			if (strcasecmp(var, "GP2X CPU clocks") != 0) return 0;
			currentConfig.CPUclock = atoi(val);
			return 1;

		case MA_OPT2_GAMMA:
			if (strcasecmp(var, "Gamma correction") != 0) return 0;
			currentConfig.gamma = (int) (atof(val) * 100.0);
			return 1;

		case MA_OPT2_SQUIDGEHACK:
			if (strcasecmp(var, "Squidgehack") != 0) return 0;
			tmpi = atoi(val);
			if (tmpi) *(int *)me->var |=  me->mask;
			else      *(int *)me->var &= ~me->mask;
			return 1;

		case MA_CDOPT_READAHEAD:
			if (strcasecmp(var, "ReadAhead buffer") != 0) return 0;
			PicoCDBuffers = atoi(val) / 2;
			return 1;

		default:
			if (strcasecmp(var, "LastUsedROM") == 0) {
				tmpi = sizeof(currentConfig.lastRomFile);
				strncpy(currentConfig.lastRomFile, val, tmpi);
				currentConfig.lastRomFile[tmpi-1] = 0;
				return 1;
			}
			printf("unhandled custom_read: %i\n", me->id);
			return 0;
	}
}


static void parse(const char *var, const char *val)
{
	menu_entry *me;
	int t, i, tlen, tmp, ret = 0;

	for (t = 0; t < sizeof(cfg_opts) / sizeof(cfg_opts[0]) && ret == 0; t++)
	{
		me = cfg_opts[t];
		tlen = *(cfg_opt_counts[t]);
		for (i = 0; i < tlen && ret == 0; i++, me++)
		{
			if (!me->need_to_save) continue;
			if (me->name != NULL) {
				if (strcasecmp(var, me->name) != 0) continue; // surely not this one
				if (me->beh == MB_ONOFF) {
					tmp = atoi(val);
					if (tmp) *(int *)me->var |=  me->mask;
					else     *(int *)me->var &= ~me->mask;
					return;
				} else if (me->beh == MB_RANGE) {
					tmp = atoi(val);
					if (tmp < me->min) tmp = me->min;
					if (tmp > me->max) tmp = me->max;
					*(int *)me->var = tmp;
					return;
				}
			}
			ret = custom_read(me, var, val);
		}
	}
	if (!ret) printf("config_readsect: unhandled var: %s\n", var);
}


int config_readsect(const char *fname, const char *section)
{
	char line[128], *var, *val, *tmp;
	FILE *f = fopen(fname, "r");
	int len, i, ret;

	if (f == NULL) return 0;

	if (section != NULL)
	{
		ret = seek_sect(f, section);
		if (!ret) {
			printf("config_readsect: %s: missing section [%s]\n", fname, section);
			fclose(f);
			return -1;
		}
	}

	while (!feof(f))
	{
		tmp = fgets(line, sizeof(line), f);
		if (tmp == NULL) break;

		if (line[0] == '[') break; // other section

		// strip comments, linefeed, spaces..
		len = strlen(line);
		for (i = 0; i < len; i++)
			if (line[i] == '#' || line[i] == '\r' || line[i] == '\n') { line[i] = 0; break; }
		mystrip(line);
		len = strlen(line);
		if (len <= 0) continue;

		// get var and val
		for (i = 0; i < len; i++)
			if (line[i] == '=') break;
		if (i >= len || strchr(&line[i+1], '=') != NULL) {
			printf("config_readsect: can't parse: %s\n", line);
			continue;
		}
		line[i] = 0;
		var = line;
		val = &line[i+1];
		mystrip(var);
		mystrip(val);
		if (strlen(var) == 0 || strlen(val) == 0) {
			printf("config_readsect: something's empty: \"%s\" = \"%s\"\n", var, val);
			continue;
		}

		parse(var, val);
	}

	fclose(f);
	return 0;
}
