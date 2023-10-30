/* -*- Mode: C; indent-tabs-mode:t ; c-basic-offset:8 -*- */
/*
 * Bulk-Only Mass Storage (BOMS) I/O functions for libusb
 * Copyright © 2009-2012 Pete Batard <pete@akeo.ie>
 * Contributions to Mass Storage by Alan Stern.
 * BOMS functions moved from xusb example and generalized a little bit
 * by Paul Lebedev <paul.e.lebedev@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <string.h>

#include "libusbi.h"

#define RETRY_MAX            5
#define REQUEST_SENSE_LENGTH 0x12

// Section 5.1: Command Block Wrapper (CBW)
struct command_block_wrapper {
  uint8_t dCBWSignature[4];
  uint32_t dCBWTag;
  uint32_t dCBWDataTransferLength;
  uint8_t bmCBWFlags;
  uint8_t bCBWLUN;
  uint8_t bCBWCBLength;
  uint8_t CBWCB[16];
};

// Section 5.2: Command Status Wrapper (CSW)
struct command_status_wrapper {
  uint8_t dCSWSignature[4];
  uint32_t dCSWTag;
  uint32_t dCSWDataResidue;
  uint8_t bCSWStatus;
};

static const uint8_t cdb_length[256] = {
//   0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
  06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,  //  0
  06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,  //  1
  10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  2
  10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  3
  10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  4
  10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  5
  00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  6
  00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  7
  16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,  //  8
  16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,  //  9
  12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,  //  A
  12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,  //  B
  00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  C
  00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  D
  00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  E
  00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  F
};

int API_EXPORTED libusb_boms_send_command(libusb_device_handle *handle, uint8_t endpoint, uint8_t lun,
  uint8_t *cdb, uint8_t direction, int data_length, uint32_t *ret_tag, uint8_t cdb_len)
{
  static uint32_t tag = 1;
  int i, r, size;
  struct command_block_wrapper cbw;

  if (cdb == NULL) {
    usbi_err(HANDLE_CTX(handle), "%s: cdb can't be NULL", __FUNCTION__);
    return LIBUSB_ERROR_INVALID_PARAM;
  }

  if (endpoint & LIBUSB_ENDPOINT_IN) {
    usbi_err(HANDLE_CTX(handle), "%s: cannot send command on IN endpoint", __FUNCTION__);
    return LIBUSB_ERROR_INVALID_PARAM;
  }

  if (0 == cdb_len) {
    cdb_len = cdb_length[cdb[0]];
  }
  if ((cdb_len == 0) || (cdb_len > sizeof(cbw.CBWCB))) {
    usbi_err(
        HANDLE_CTX(handle),
        "%s: don't know how to handle this command (%02X, length %d)",
        __FUNCTION__,
        cdb[0],
        cdb_len
    );
    return LIBUSB_ERROR_NOT_SUPPORTED;
  }

  memset(&cbw, 0, sizeof(cbw));
  cbw.dCBWSignature[0] = 'U';
  cbw.dCBWSignature[1] = 'S';
  cbw.dCBWSignature[2] = 'B';
  cbw.dCBWSignature[3] = 'C';
  *ret_tag = tag;
  cbw.dCBWTag = tag++;
  cbw.dCBWDataTransferLength = data_length;
  cbw.bmCBWFlags = direction;
  cbw.bCBWLUN = lun;
  // Subclass is 1 or 6 => cdb_len
  cbw.bCBWCBLength = cdb_len;
  memcpy(cbw.CBWCB, cdb, cdb_len);

  i = 0;
  do {
    // The transfer length must always be exactly 31 bytes.
    r = libusb_bulk_transfer(handle, endpoint, (unsigned char*)&cbw, 31, &size, 1000);
    if (r == LIBUSB_ERROR_PIPE) {
      libusb_clear_halt(handle, endpoint);
    }
    i++;
  } while ((r == LIBUSB_ERROR_PIPE) && (i<RETRY_MAX));
  if (r != LIBUSB_SUCCESS) {
    return r;
  }

  usbi_dbg(HANDLE_CTX(handle), "%s: sent %d CDB bytes", __FUNCTION__, cdb_len);
  return LIBUSB_SUCCESS;
}

int API_EXPORTED libusb_boms_get_status(libusb_device_handle *handle,
    uint8_t endpoint, uint32_t expected_tag, uint8_t* should_request_sence)
{
  int i, r, size;
  struct command_status_wrapper csw;

  if (should_request_sence) {
    *should_request_sence = 0;
  }

  // The device is allowed to STALL this transfer. If it does, you have to
  // clear the stall and try again.
  i = 0;
  do {
    r = libusb_bulk_transfer(handle, endpoint, (unsigned char*)&csw, 13, &size, 1000);
    if (r == LIBUSB_ERROR_PIPE) {
      libusb_clear_halt(handle, endpoint);
    }
    i++;
  } while ((r == LIBUSB_ERROR_PIPE) && (i<RETRY_MAX));
  if (r != LIBUSB_SUCCESS) {
    return r;
  }
  if (size != 13) {
    usbi_err(HANDLE_CTX(handle), "%s: received %d bytes (expected 13)", __FUNCTION__, size);
    return LIBUSB_ERROR_OTHER;
  }
  if (csw.dCSWTag != expected_tag) {
    usbi_err(HANDLE_CTX(handle), "%s: mismatched tags (expected %08X, received %08X)", __FUNCTION__, expected_tag, csw.dCSWTag);
    return LIBUSB_ERROR_OTHER;
  }
  // For this test, we ignore the dCSWSignature check for validity...

  if (csw.bCSWStatus) {
    usbi_err(HANDLE_CTX(handle), "%s: status %02X (FAILED)", __FUNCTION__, csw.bCSWStatus);
    // REQUEST SENSE is appropriate only if bCSWStatus is 1, meaning that the
    // command failed somehow.  Larger values (2 in particular) mean that
    // the command couldn't be understood.
    if (csw.bCSWStatus == 1 && should_request_sence) {
      *should_request_sence = 1; // request Get Sense
    }
    return LIBUSB_ERROR_OTHER;
  }

  usbi_dbg(HANDLE_CTX(handle), "%s: status %02X (Success)", __FUNCTION__, csw.bCSWStatus);

  // In theory we also should check dCSWDataResidue.  But lots of devices
  // set it wrongly.
  return LIBUSB_SUCCESS;
}

int API_EXPORTED libusb_boms_get_sense(libusb_device_handle *handle,
    uint8_t endpoint_in, uint8_t endpoint_out, uint8_t* sense)
{
  uint8_t cdb[16];  // SCSI Command Descriptor Block
  uint32_t expected_tag;
  int size;
  int rc;

  // Request Sense
  memset(sense, 0, 18);
  memset(cdb, 0, sizeof(cdb));
  cdb[0] = 0x03;  // Request Sense
  cdb[4] = REQUEST_SENSE_LENGTH;

  rc = libusb_boms_send_command(handle, endpoint_out, 0, cdb, LIBUSB_ENDPOINT_IN, REQUEST_SENSE_LENGTH, &expected_tag, 0);
  if (rc < 0) {
    return rc;
  }
  rc = libusb_bulk_transfer(handle, endpoint_in, (unsigned char*)&sense, REQUEST_SENSE_LENGTH, &size, 1000);
  if (rc < 0) {
    return rc;
  }

  usbi_dbg(HANDLE_CTX(handle), "%s: received %d bytes", __FUNCTION__, size);

  rc = libusb_boms_get_status(handle, endpoint_in, expected_tag, NULL);
  if (rc < 0) {
    return rc;
  }

  if ((sense[0] != 0x70) && (sense[0] != 0x71)) {
    usbi_dbg(HANDLE_CTX(handle), "%s: ERROR No sense data", __FUNCTION__);
  } else {
    usbi_dbg(HANDLE_CTX(handle), "%s: ERROR Sense: %02X %02X %02X", __FUNCTION__, sense[2]&0x0F, sense[12], sense[13]);
  }

  return LIBUSB_SUCCESS;
}
