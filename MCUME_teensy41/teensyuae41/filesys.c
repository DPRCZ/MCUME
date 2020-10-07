 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Unix file system handler for AmigaDOS
  *
  * Copyright 1996 Ed Hanway
  * Copyright 1996, 1997 Bernd Schmidt
  *
  * Version 0.4: 970308
  *
  * Based on example code (c) 1988 The Software Distillery
  * and published in Transactor for the Amiga, Volume 2, Issues 2-5.
  * (May - August 1989)
  *
  * Known limitations:
  * Does not support ACTION_INHIBIT (big deal).
  * Does not support any 2.0+ packet types (except ACTION_SAME_LOCK)
  * Does not support removable volumes.
  * May not return the correct error code in some cases.
  * Does not check for sane values passed by AmigaDOS.  May crash the emulation
  * if passed garbage values.
  * Could do tighter checks on malloc return values.
  * Will probably fail spectacularly in some cases if the filesystem is
  * modified at the same time by another process while UAE is running.
  */

#include "shared.h"

#ifdef HAS_FILESYS

#include "uae.h"
#include "memory.h"
#include "custom.h"
#include "readcpu.h"
#include "newcpu.h"
#include "xwin.h"
#include "filesys.h"
#include "autoconf.h"
#include "compiler.h"

extern uae_u8 filesysory[65536];

typedef struct {
    char *devname; /* device name, e.g. UAE0: */
    uaecptr devname_amiga;
    uaecptr startup;
    char *volname; /* volume name, e.g. CDROM, WORK, etc. */
    char *rootdir; /* root unix directory */
    int readonly; /* disallow write access? */
    int devno;
} UnitInfo;

#define MAX_UNITS 20
static int num_units = 0, num_filesys_units = 0;
static UnitInfo ui[MAX_UNITS];
uaecptr filesys_initcode;
static uae_u32 fsdevname, filesys_configdev;

void add_filesys_unit(char *volname, char *rootdir, int readonly)
{
    if (num_units >= MAX_UNITS) {
	write_log ("Maximum number of file systems mounted.\n");
	return;
    }

    if (volname != 0) {
	    num_filesys_units++;
	    ui[num_units].volname = my_strdup(volname);	
    } else
	    ui[num_units].volname = 0;
    //ui[num_units].rootdir = my_strdup(rootdir);
    ui[num_units].readonly = readonly;

    num_units++;
}

void filesys_store_devinfo (uae_u8 *where)
{
    int i;

    do_put_mem_long ((uae_u32 *)where, EXPANSION_explibname);
    do_put_mem_long ((uae_u32 *)(where + 4), filesys_configdev);
    do_put_mem_long ((uae_u32 *)(where + 8), EXPANSION_doslibname);
    do_put_mem_long ((uae_u32 *)(where + 12), num_units);

    where += 16;

    for (i = 0; i < num_units; i++) {
	int is_hardfile = ui[i].volname == 0;
	uae_u32 *thisdev = (uae_u32 *)where;
  emu_printf("what is filesys:");
	if (is_hardfile)
    emu_printf("filesys is hdd");
	do_put_mem_long (thisdev, ui[i].devname_amiga);	
	do_put_mem_long (thisdev + 1, is_hardfile ? ROM_hardfile_resname : fsdevname);
	do_put_mem_long (thisdev + 2, ui[i].devno);
	do_put_mem_long (thisdev + 3, is_hardfile);

	where += 16;
    }
}


static uae_u32 startup_handler (void)
{
    return 0;
}

static uae_u32 exter_int_helper (void)
{
    return 0;
}


static uae_u32 filesys_handler(void)
{
    return 0;
}

void filesys_reset (void)
{
}

void filesys_prepare_reset (void)
{
}

static uae_u32 filesys_diagentry(void)
{
  
    uaecptr resaddr = m68k_areg(regs, 2) + 0x10;
    
    filesys_configdev = m68k_areg(regs, 3);
    
    filesys_store_devinfo (filesysory + 0x2100);

    if (ROM_hardfile_resid != 0) {
	/* Build a struct Resident. This will set up and initialize
	 * the uae.device */
	put_word(resaddr + 0x0, 0x4AFC);
	put_long(resaddr + 0x2, resaddr);
	put_long(resaddr + 0x6, resaddr + 0x1A); /* Continue scan here */
	put_word(resaddr + 0xA, 0x8101); /* RTF_AUTOINIT|RTF_COLDSTART; Version 1 */
	put_word(resaddr + 0xC, 0x0305); /* NT_DEVICE; pri 05 */
	put_long(resaddr + 0xE, ROM_hardfile_resname);
	put_long(resaddr + 0x12, ROM_hardfile_resid);
	put_long(resaddr + 0x16, ROM_hardfile_init); /* calls filesys_init */
    }
    resaddr += 0x1A;

    filesys_reset ();
    /* The good thing about this function is that it always gets called
     * when we boot. So we could put all sorts of stuff that wants to be done
     * here. */
 
    return 1;
}

static uae_u32 filesys_dev_remember(void)
{
    /* In: a3: devicenode, d6: unit_no */

    ui[m68k_dreg (regs, 6)].startup = get_long(m68k_areg (regs, 3) + 28);
    return m68k_areg (regs, 3);
}



void filesys_install (void)
{
    int i;
    uaecptr loop;

    ROM_filesys_resname = ds("UAEunixfs.resource");
    ROM_filesys_resid = ds("UAE unixfs 0.4");

    fsdevname = ds("uae.device"); /* does not really exist */

    for(i = 0; i < num_units; i++) {
	   ui[i].devno = get_new_device(&ui[i].devname, &ui[i].devname_amiga);
    }

    ROM_filesys_diagentry = here();
    calltrap (deftrap(filesys_diagentry)); dw(RTS);
    
    loop = here ();
    /* Special trap for the assembly make_dev routine */
    org(0xF0FF20);
    calltrap (deftrap (filesys_dev_remember));
    dw(RTS);

    org(0xF0FF30);
    calltrap (deftrap (filesys_handler));
    dw(RTS);

    org(0xF0FF40);
    calltrap (deftrap (startup_handler));
    dw(RTS);

    org(0xF0FF50);
    calltrap (deftrap (exter_int_helper));
    dw(RTS);

    org(0xF0FF70);
    calltrap (deftrap (mousehack_helper));
    dw(RTS);

    org (loop);    
}

void filesys_install_code (void)
{
    align(4);

    /* The last offset comes from the code itself, look for it near the top. */
    filesys_initcode = here() + 8 + 0x6c;
    EXPANSION_bootcode = here () + 8 + 0x14;
    /* Ouch. Make sure this is _always_ a multiple of two bytes. */
 db(0x00); db(0x00); db(0x00); db(0x10); db(0x00); db(0x00); db(0x00); db(0x00);
 db(0x60); db(0x00); db(0x01); db(0xda); db(0x00); db(0x00); db(0x01); db(0x2c);
 db(0x00); db(0x00); db(0x00); db(0x6c); db(0x00); db(0x00); db(0x00); db(0x28);
 db(0x00); db(0x00); db(0x00); db(0x14); db(0x43); db(0xfa); db(0x03); db(0x13);
 db(0x4e); db(0xae); db(0xff); db(0xa0); db(0x20); db(0x40); db(0x20); db(0x28);
 db(0x00); db(0x16); db(0x20); db(0x40); db(0x4e); db(0x90); db(0x4e); db(0x75);
 db(0x48); db(0xe7); db(0x00); db(0x20); db(0x70); db(0x00); db(0x4e); db(0xb9);
 db(0x00); db(0xf0); db(0xff); db(0x50); db(0x4a); db(0x80); db(0x67); db(0x2e);
 db(0x2c); db(0x78); db(0x00); db(0x04); db(0x70); db(0x02); db(0x4e); db(0xb9);
 db(0x00); db(0xf0); db(0xff); db(0x50); db(0x4a); db(0x80); db(0x67); db(0x14);
 db(0x24); db(0x40); db(0x70); db(0x03); db(0x4e); db(0xb9); db(0x00); db(0xf0);
 db(0xff); db(0x50); db(0x20); db(0x4a); db(0x22); db(0x40); db(0x4e); db(0xae);
 db(0xfe); db(0x92); db(0x60); db(0xe0); db(0x70); db(0x04); db(0x4e); db(0xb9);
 db(0x00); db(0xf0); db(0xff); db(0x50); db(0x70); db(0x01); db(0x4c); db(0xdf);
 db(0x04); db(0x00); db(0x4e); db(0x75); db(0x48); db(0xe7); db(0xff); db(0xfe);
 db(0x2c); db(0x78); db(0x00); db(0x04); db(0x2a); db(0x79); db(0x00); db(0xf0);
 db(0xff); db(0xfc); db(0x43); db(0xfa); db(0x02); db(0xb9); db(0x70); db(0x24);
 db(0x7a); db(0x00); db(0x4e); db(0xae); db(0xfd); db(0xd8); db(0x4a); db(0x80);
 db(0x66); db(0x0c); db(0x43); db(0xfa); db(0x02); db(0xa9); db(0x70); db(0x00);
 db(0x7a); db(0x01); db(0x4e); db(0xae); db(0xfd); db(0xd8); db(0x28); db(0x40);
 db(0x70); db(0x58); db(0x72); db(0x01); db(0x4e); db(0xae); db(0xff); db(0x3a);
 db(0x26); db(0x40); db(0x7e); db(0x54); db(0x27); db(0xb5); db(0x78); db(0x00);
 db(0x78); db(0x00); db(0x59); db(0x87); db(0x64); db(0xf6); db(0x7c); db(0x00);
 db(0xbc); db(0xad); db(0x01); db(0x0c); db(0x64); db(0x12); db(0x20); db(0x4b);
 db(0x48); db(0xe7); db(0x02); db(0x10); db(0x7e); db(0x01); db(0x61); db(0x6c);
 db(0x4c); db(0xdf); db(0x08); db(0x40); db(0x52); db(0x86); db(0x60); db(0xe8);
 db(0x2c); db(0x78); db(0x00); db(0x04); db(0x22); db(0x4c); db(0x4e); db(0xae);
 db(0xfe); db(0x62); db(0x61); db(0x28); db(0x2c); db(0x78); db(0x00); db(0x04);
 db(0x4e); db(0xb9); db(0x00); db(0xf0); db(0xff); db(0x80); db(0x72); db(0x03);
 db(0x74); db(0xf6); db(0x20); db(0x7c); db(0x00); db(0x20); db(0x00); db(0x00);
 db(0x90); db(0x88); db(0x65); db(0x0a); db(0x67); db(0x08); db(0x78); db(0x00);
 db(0x22); db(0x44); db(0x4e); db(0xae); db(0xfd); db(0x96); db(0x4c); db(0xdf);
 db(0x7f); db(0xff); db(0x4e); db(0x75); db(0x2c); db(0x78); db(0x00); db(0x04);
 db(0x70); db(0x1a); db(0x22); db(0x3c); db(0x00); db(0x01); db(0x00); db(0x01);
 db(0x4e); db(0xae); db(0xff); db(0x3a); db(0x22); db(0x40); db(0x41); db(0xfa);
 db(0x02); db(0x0a); db(0x23); db(0x48); db(0x00); db(0x0a); db(0x41); db(0xfa);
 db(0xff); db(0x10); db(0x23); db(0x48); db(0x00); db(0x0e); db(0x41); db(0xfa);
 db(0xff); db(0x08); db(0x23); db(0x48); db(0x00); db(0x12); db(0x70); db(0x0d);
 db(0x4e); db(0xee); db(0xff); db(0x58); db(0x2a); db(0x79); db(0x00); db(0xf0);
 db(0xff); db(0xfc); db(0x26); db(0x06); db(0xe9); db(0x8b); db(0xd6); db(0xbc);
 db(0x00); db(0x00); db(0x01); db(0x10); db(0x20); db(0x35); db(0x38); db(0x0c);
 db(0xc0); db(0x85); db(0x66); db(0xb6); db(0x20); db(0xb5); db(0x38); db(0x00);
 db(0x21); db(0x75); db(0x38); db(0x04); db(0x00); db(0x04); db(0x21); db(0x75);
 db(0x38); db(0x08); db(0x00); db(0x08); db(0x2c); db(0x4c); db(0x4e); db(0xae);
 db(0xff); db(0x70); db(0x26); db(0x40); db(0x4e); db(0xb9); db(0x00); db(0xf0);
 db(0xff); db(0x20); db(0x70); db(0x00); db(0x27); db(0x40); db(0x00); db(0x08);
 db(0x27); db(0x40); db(0x00); db(0x10); db(0x27); db(0x40); db(0x00); db(0x20);
 db(0x20); db(0x35); db(0x38); db(0x0c); db(0x4a); db(0x80); db(0x66); db(0x1c);
 db(0x27); db(0x7c); db(0x00); db(0x00); db(0x0f); db(0xa0); db(0x00); db(0x14);
 db(0x43); db(0xfa); db(0xfe); db(0x7a); db(0x20); db(0x09); db(0xe4); db(0x88);
 db(0x27); db(0x40); db(0x00); db(0x20); db(0x27); db(0x7c); db(0xff); db(0xff);
 db(0xff); db(0xff); db(0x00); db(0x24); db(0x4a); db(0x87); db(0x67); db(0x36);
 db(0x2c); db(0x78); db(0x00); db(0x04); db(0x70); db(0x14); db(0x72); db(0x00);
 db(0x4e); db(0xae); db(0xff); db(0x3a); db(0x22); db(0x40); db(0x70); db(0x00);
 db(0x22); db(0x80); db(0x23); db(0x40); db(0x00); db(0x04); db(0x33); db(0x40);
 db(0x00); db(0x0e); db(0x30); db(0x3c); db(0x10); db(0xff); db(0x90); db(0x06);
 db(0x33); db(0x40); db(0x00); db(0x08); db(0x23); db(0x6d); db(0x01); db(0x04);
 db(0x00); db(0x0a); db(0x23); db(0x4b); db(0x00); db(0x10); db(0x41); db(0xec);
 db(0x00); db(0x4a); db(0x4e); db(0xee); db(0xfe); db(0xf2); db(0x20); db(0x4b);
 db(0x72); db(0x00); db(0x22); db(0x41); db(0x70); db(0xff); db(0x2c); db(0x4c);
 db(0x4e); db(0xee); db(0xff); db(0x6a); db(0x2c); db(0x78); db(0x00); db(0x04);
 db(0x70); db(0x00); db(0x22); db(0x40); db(0x4e); db(0xae); db(0xfe); db(0xda);
 db(0x20); db(0x40); db(0x4b); db(0xe8); db(0x00); db(0x5c); db(0x43); db(0xfa);
 db(0x01); db(0x39); db(0x70); db(0x00); db(0x4e); db(0xae); db(0xfd); db(0xd8);
 db(0x24); db(0x40); db(0x20); db(0x3c); db(0x00); db(0x00); db(0x00); db(0x9d);
 db(0x22); db(0x3c); db(0x00); db(0x01); db(0x00); db(0x01); db(0x4e); db(0xae);
 db(0xff); db(0x3a); db(0x26); db(0x40); db(0x7c); db(0x00); db(0x26); db(0x86);
 db(0x27); db(0x46); db(0x00); db(0x04); db(0x27); db(0x46); db(0x00); db(0x08);
 db(0x7a); db(0x00); db(0x20); db(0x4d); db(0x4e); db(0xae); db(0xfe); db(0x80);
 db(0x20); db(0x4d); db(0x4e); db(0xae); db(0xfe); db(0x8c); db(0x28); db(0x40);
 db(0x26); db(0x2c); db(0x00); db(0x0a); db(0x70); db(0x00); db(0x4e); db(0xb9);
 db(0x00); db(0xf0); db(0xff); db(0x40); db(0x60); db(0x74); db(0x20); db(0x4d);
 db(0x4e); db(0xae); db(0xfe); db(0x80); db(0x20); db(0x4d); db(0x4e); db(0xae);
 db(0xfe); db(0x8c); db(0x28); db(0x40); db(0x26); db(0x2c); db(0x00); db(0x0a);
 db(0x66); db(0x38); db(0x70); db(0x01); db(0x4e); db(0xb9); db(0x00); db(0xf0);
 db(0xff); db(0x50); db(0x45); db(0xeb); db(0x00); db(0x04); db(0x20); db(0x52);
 db(0x20); db(0x08); db(0x67); db(0xda); db(0x22); db(0x50); db(0x20); db(0x40);
 db(0x20); db(0x28); db(0x00); db(0x04); db(0x6a); db(0x16); db(0x48); db(0xe7);
 db(0x00); db(0xc0); db(0x28); db(0x68); db(0x00); db(0x0a); db(0x61); db(0x40);
 db(0x53); db(0x85); db(0x4c); db(0xdf); db(0x03); db(0x00); db(0x24); db(0x89);
 db(0x20); db(0x49); db(0x60); db(0xdc); db(0x24); db(0x48); db(0x20); db(0x49);
 db(0x60); db(0xd6); db(0xba); db(0xbc); db(0x00); db(0x00); db(0x00); db(0x14);
 db(0x65); db(0x08); db(0x70); db(0x01); db(0x29); db(0x40); db(0x00); db(0x04);
 db(0x60); db(0x0e); db(0x61); db(0x2a); db(0x4e); db(0xb9); db(0x00); db(0xf0);
 db(0xff); db(0x30); db(0x4a); db(0x80); db(0x67); db(0x0c); db(0x52); db(0x85);
 db(0x28); db(0xab); db(0x00); db(0x04); db(0x27); db(0x4c); db(0x00); db(0x04);
 db(0x60); db(0x8c); db(0x28); db(0x43); db(0x61); db(0x02); db(0x60); db(0x86);
 db(0x22); db(0x54); db(0x20); db(0x6c); db(0x00); db(0x04); db(0x29); db(0x4d);
 db(0x00); db(0x04); db(0x4e); db(0xee); db(0xfe); db(0x92); db(0x2f); db(0x05);
 db(0x7a); db(0xfc); db(0x24); db(0x53); db(0x2e); db(0x0a); db(0x22); db(0x0a);
 db(0x67); db(0x0a); db(0x52); db(0x85); db(0x67); db(0x1e); db(0x22); db(0x4a);
 db(0x24); db(0x52); db(0x60); db(0xf2); db(0x52); db(0x85); db(0x67); db(0x3c);
 db(0x24); db(0x47); db(0x70); db(0x18); db(0x72); db(0x01); db(0x4e); db(0xae);
 db(0xff); db(0x3a); db(0x52); db(0x46); db(0x24); db(0x40); db(0x24); db(0x87);
 db(0x2e); db(0x0a); db(0x60); db(0xe8); db(0x20); db(0x12); db(0x67); db(0x24);
 db(0x20); db(0x40); db(0x20); db(0x10); db(0x67); db(0x1e); db(0x20); db(0x40);
 db(0x20); db(0x10); db(0x67); db(0x18); db(0x70); db(0x00); db(0x22); db(0x80);
 db(0x22); db(0x4a); db(0x24); db(0x51); db(0x70); db(0x18); db(0x4e); db(0xae);
 db(0xff); db(0x2e); db(0xdc); db(0xbc); db(0x00); db(0x01); db(0x00); db(0x00);
 db(0x20); db(0x0a); db(0x66); db(0xec); db(0x26); db(0x87); db(0x2a); db(0x1f);
 db(0x4e); db(0x75); db(0x55); db(0x41); db(0x45); db(0x20); db(0x66); db(0x69);
 db(0x6c); db(0x65); db(0x73); db(0x79); db(0x73); db(0x74); db(0x65); db(0x6d);
 db(0x00); db(0x64); db(0x6f); db(0x73); db(0x2e); db(0x6c); db(0x69); db(0x62);
 db(0x72); db(0x61); db(0x72); db(0x79); db(0x00); db(0x65); db(0x78); db(0x70);
 db(0x61); db(0x6e); db(0x73); db(0x69); db(0x6f); db(0x6e); db(0x2e); db(0x6c);
 db(0x69); db(0x62); db(0x72); db(0x61); db(0x72); db(0x79); db(0x00); db(0x00);
}

#endif