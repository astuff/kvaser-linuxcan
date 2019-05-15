/*
** Copyright 2002-2006 KVASER AB, Sweden.  All rights reserved.
*/

/* Kvaser CAN driver PCIcan hardware specific parts                    */
/* Functions for accessing the Dallas circuits.                        */

#include <linux/config.h>

#include <linux/delay.h>
#include <linux/types.h>
#include <asm/io.h>
// new
#if LINUX_2_6
#   include <linux/spinlock.h>
#endif
//
#include "dallas.h"

/*****************************************************************************/

#define T_SLOT          120
#define T_LOW            15
#define T_WR_SAMPLE      60
#define T_RELEASE        45

/*****************************************************************************/
// new
#if LINUX_2_6
spinlock_t dallas_lock  = SPIN_LOCK_UNLOCKED;
#endif
//
static void dallas_set_bit (DALLAS_CONTEXT *dc)
{
  unsigned char tmp;
  tmp = inb(dc->address_out);
  outb(tmp | dc->out_mask, dc->address_out);
} /* dallas_set_bit */


static void dallas_clr_bit (DALLAS_CONTEXT *dc)
{
  unsigned char tmp;
  tmp = inb(dc->address_out);
  outb (tmp & ~dc->out_mask, dc->address_out);
} /* dallas_clr_bit */


static int dallas_bit_value (DALLAS_CONTEXT *dc)
{
  return inb(dc->address_in) & dc->in_mask;
} /* dallas_bit_value */


/*****************************************************************************/

/* Write one bit.
 */
static void ds_write_bit (DALLAS_CONTEXT *dc, int value)
{
#if LINUX_2_6
    spin_lock_irq(&dallas_lock);
#else
    cli();
#endif
  
  //REPT
  dallas_clr_bit (dc);

  if (value) {
    /* A "one" is a low level; 1us < t < 15 us. */
    udelay (1);
    dallas_set_bit (dc);
#if LINUX_2_6
    spin_unlock_irq(&dallas_lock);
#else
    sti();
#endif
    udelay (T_SLOT);
  }
  else {
    /* A "zero" is a low level; 60us < t < 120 us. */
    udelay (T_WR_SAMPLE);
    dallas_set_bit (dc);
#if LINUX_2_6
    spin_unlock_irq(&dallas_lock);
#else
    sti();
#endif
    udelay (T_SLOT - T_WR_SAMPLE);
  }
} /* ds_write_bit */



/* Read one bit.
 */
static int ds_read_bit (DALLAS_CONTEXT *dc)
{
  int b;

#if LINUX_2_6
  spin_lock_irq(&dallas_lock);
#else
  cli();
#endif

  //REPT
  dallas_clr_bit (dc);
  udelay (1);
  dallas_set_bit (dc);
  b = dallas_bit_value (dc);

#if LINUX_2_6
  spin_unlock_irq(&dallas_lock);
#else
  sti();
#endif

  udelay (T_SLOT);
  return b;
} /* ds_read_bit */


/*****************************************************************************/

/* Initialize the I/O port. */
dsStatus ds_init (DALLAS_CONTEXT *dc)
{
  if (!dc->in_mask) return dsError_NoDevice;
  //dallas_init (dc);
  dallas_set_bit (dc);
  return dsError_None;
} /* ds_init */


/* Perform a device reset. */
dsStatus ds_reset (DALLAS_CONTEXT *dc)
{
  int i, x;

  if (!dc->in_mask) return dsError_NoDevice;

  /* Output a low level */
  dallas_clr_bit (dc);

  udelay (550);

  dallas_set_bit (dc);
  udelay (5);

  /* Wait for presence pulse. Sample two times with 5us between
     each sample. Do not loop more than 150 times. */

  for (i = 0; i < 150; i++) {
    x = 0;
    if (dallas_bit_value (dc) == 0) {
      x++;
    }
    udelay (5);
    if (dallas_bit_value (dc) == 0) {
      x++;
    }
    if (x == 2) break;
  }

  if (x != 2) {
    return dsError_NoDevice;
  }

  /* Wait for the bus to become free. Sample two times with 5us between
     each sample. Do not loop more than 150 times. */

  x = 0;
  for (; i < 150; i++) {
    x = 0;
    if (dallas_bit_value (dc)) {
      x++;
    }
    udelay (5);

    if (dallas_bit_value (dc)) {
      x++;
    }

    if (x == 2) break;
  }

  if (x != 2) {
    return dsError_BusStuckLow;
  }

  return dsError_None;
} /* ds_reset */


/*****************************************************************************/

/* Read a whole byte from the device.
 */
static unsigned int ds_read_byte (DALLAS_CONTEXT *dc)
{
  int x;
  unsigned int data;

  data = 0;
  for (x = 0; x < 8; x++) {
    data |= ((ds_read_bit (dc) ? 1 : 0) << x);
  }

  return data;
} /* ds_read_byte */


/* Write a whole byte to the device.
 */
static void ds_write_byte (DALLAS_CONTEXT *dc, unsigned int data)
{
  int x;
  for (x = 0; x < 8; x++) {
    ds_write_bit (dc, (data >> x) & 1);
  }
} /* ds_write_byte */


/*****************************************************************************/


/* Return 1 if at least one device is present on the bus.
 */
int ds_check_for_presence (DALLAS_CONTEXT *dc)
{
  if (!dc->in_mask) return 0;
  return ds_reset (dc) == dsError_None;
} /* ds_check_for_presence */


/* Return 1 if at least one device is present on the bus.
 * Loops 100 times if no device is found.
 */
int ds_check_for_presence_loop (DALLAS_CONTEXT *dc)
{
  int i;

  if (!dc->in_mask) return 0;

  for (i = 0; i < 100; i++) {
    if (ds_reset (dc) == dsError_None) return 1;
  }

  return 0;
} /* ds_check_for_presence_loop */


/* Read data from the memory.
 * This will work for commands READ MEMORY, READ SCRATCHPAD,
 * READ STATUS, READ APPLICATION REG
 */
dsStatus ds_read_string (DALLAS_CONTEXT *dc,
                         unsigned int command,
                         unsigned int address,
                         unsigned char *data,
                         int bufsiz)
{
  unsigned int i;

  if (!dc->in_mask) return dsError_NoDevice;

  ds_write_byte (dc, command);
  ds_write_byte (dc, address);

  for (i = 0; i < bufsiz; i++) *(data + i) = (unsigned char) ds_read_byte (dc);

  return ds_reset (dc);
} /* ds_read_string */


/* Read data from the memory.
 */
dsStatus ds_read_memory (DALLAS_CONTEXT *dc,
                         unsigned int address,
                         unsigned char *data,
                         int bufsiz)
{
  dsStatus stat;

  if (!dc->in_mask) return dsError_NoDevice;

  address &= 0x1F;
  stat = ds_reset (dc);
  if (stat != dsError_None) return stat;

  /* Only one device; skip ROM check */
  ds_write_byte (dc, SKIP_ROM);

  return ds_read_string (dc, READ_MEMORY, address, data, bufsiz );
} /* ds_read_memory */


/* Read the 64-bit (8-byte) application area of some devices.
 */
dsStatus ds_read_application_area (DALLAS_CONTEXT *dc,
                                   unsigned int address,
                                   unsigned char *data,
                                   int bufsiz)
{
  unsigned int x;
  dsStatus stat;

  if (!dc->in_mask) return dsError_NoDevice;

  stat = ds_reset (dc);
  if (stat != dsError_None) return stat;

  ds_write_byte (dc, SKIP_ROM);
  ds_write_byte (dc, READ_STATUS);
  ds_write_byte (dc, 0); /* Validation byte */

  x = ds_read_byte (dc);
  if (x != 0xFC) return dsError_NotProgrammed;

  stat = ds_reset (dc);
  if (stat != dsError_None) return stat;

  ds_write_byte (dc, SKIP_ROM);
  ds_write_byte (dc, READ_APPLICATION);

  /* LSB of address */
  ds_write_byte (dc, address & 0xFF);

  for (x = 0; x < bufsiz; x++) *(data + x) = (unsigned char) ds_read_byte (dc);

  return dsError_None;
} /* ds_read_application_area */


/* Read data from the ROM (which contains the S/N).
 */
dsStatus ds_read_rom_64bit (DALLAS_CONTEXT *dc, int port, unsigned char *data)
{
  int x;
  dsStatus stat;

  if (!dc->in_mask) return dsError_NoDevice;

  stat = ds_reset (dc);
  if (stat != dsError_None) return stat;

  ds_write_byte (dc, READ_ROM);

  for (x = 0; x < 8; x++) *(data + x) = (unsigned char) ds_read_byte (dc);

  if (*data != DS2430_CODE) {
    return dsError_WrongDevice;
  }
  /* qqq check checksum */
  return dsError_None;
} /* ds_read_rom_64bit */


int ds_verify_area (unsigned char *data, int bufsiz)
{
  unsigned int i;
  unsigned char sum = 0;
  for (i = 0; i < bufsiz; i++) sum += data[i];
  return (sum == 0xFF);
} /* ds_verify_area */


/*****************************************************************************/


