/******************************************************************************
 *
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2015 - 2017 Intel Deutschland GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_XVT_FW_API_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_XVT_FW_API_H_

#include "fw/api/alive.h"
#include "fw/api/commands.h"
#include "fw/api/config.h"
#include "fw/api/datapath.h"
#include "fw/api/nvm-reg.h"
#include "fw/api/phy.h"
#include "fw/api/rs.h"
#include "fw/api/tof.h"
#include "fw/api/tx.h"
#include "fw/api/txq.h"

#define IWL_XVT_DEFAULT_TX_QUEUE 1
#define IWL_XVT_DEFAULT_TX_FIFO 3

#define IWL_XVT_TX_STA_ID_DEFAULT 0

#define XVT_LMAC_0_ID 0
#define XVT_LMAC_1_ID 1

enum {
    APMG_PD_SV_CMD = 0x43,

    /* ToF */
    LOCATION_GROUP_NOTIFICATION = 0x11,

    NVM_COMMIT_COMPLETE_NOTIFICATION = 0xad,
    GET_SET_PHY_DB_CMD = 0x8f,

    /* BFE */
    REPLY_HD_PARAMS_CMD = 0xa6,

    REPLY_RX_DSP_EXT_INFO = 0xc4,

    REPLY_DEBUG_XVT_CMD = 0xf3,
};

struct xvt_alive_resp_ver2 {
    __le16 status;
    __le16 flags;
    uint8_t ucode_minor;
    uint8_t ucode_major;
    __le16 id;
    uint8_t api_minor;
    uint8_t api_major;
    uint8_t ver_subtype;
    uint8_t ver_type;
    uint8_t mac;
    uint8_t opt;
    __le16 reserved2;
    __le32 timestamp;
    __le32 error_event_table_ptr; /* SRAM address for error log */
    __le32 log_event_table_ptr;   /* SRAM address for LMAC event log */
    __le32 cpu_register_ptr;
    __le32 dbgm_config_ptr;
    __le32 alive_counter_ptr;
    __le32 scd_base_ptr; /* SRAM address for SCD */
    __le32 st_fwrd_addr; /* pointer to Store and forward */
    __le32 st_fwrd_size;
    uint8_t umac_minor;     /* UMAC version: minor */
    uint8_t umac_major;     /* UMAC version: major */
    __le16 umac_id;         /* UMAC version: id */
    __le32 error_info_addr; /* SRAM address for UMAC error log */
    __le32 dbg_print_buff_addr;
} __packed; /* ALIVE_RES_API_S_VER_2 */

enum {
    XVT_DBG_GET_SVDROP_VER_OP = 0x01,
};

struct xvt_debug_cmd {
    __le32 opcode;
    __le32 dw_num;
}; /* DEBUG_XVT_CMD_API_S_VER_1 */

struct xvt_debug_res {
    __le32 dw_num;
    __le32 data[0];
}; /* DEBUG_XVT_RES_API_S_VER_1 */

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_XVT_FW_API_H_
