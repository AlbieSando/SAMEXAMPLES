/* ----------------------------------------------------------------------------
 *         SAM Software Package License
 * ----------------------------------------------------------------------------
 * Copyright (c) 2015, Atmel Corporation
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the disclaimer below.
 *
 * Atmel's name may not be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * DISCLAIMER: THIS SOFTWARE IS PROVIDED BY ATMEL "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * DISCLAIMED. IN NO EVENT SHALL ATMEL BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ----------------------------------------------------------------------------
 */

#include "chip.h"
#include "peripherals/mpddrc.h"
#include "peripherals/sfrbu.h"
#include "peripherals/pmc.h"
#include "trace.h"
#include "timer.h"
#include "compiler.h"
#include <stdlib.h>

static void _set_ddr_timings(struct _mpddrc_desc* desc)
{
	MPDDRC->MPDDRC_TPR0 = desc->tpr0;
	MPDDRC->MPDDRC_TPR1 = desc->tpr1;
	MPDDRC->MPDDRC_TPR2 = desc->tpr2;
}

static uint32_t _compute_ba_offset(void)
{
	/* Compute BA[] offset according to CR configuration */
	uint32_t offset = (MPDDRC->MPDDRC_CR & MPDDRC_CR_NC_Msk) + 9;
	if (!(MPDDRC->MPDDRC_CR & MPDDRC_CR_DECOD_INTERLEAVED))
		offset += ((MPDDRC->MPDDRC_CR & MPDDRC_CR_NR_Msk) >> 2) + 11;

	offset += (MPDDRC->MPDDRC_MD & MPDDRC_MD_DBW) ? 1 : 2;

	return offset;
}

/**
 * \brief Send a NOP command
 */
static void _send_nop_cmd(void)
{
	MPDDRC->MPDDRC_MR = MPDDRC_MR_MODE_NOP_CMD;
	/* Perform a write to a DDR memory access to acknoledge the command */
	*((uint32_t *)DDR_CS_ADDR) = 0;
}

static void _send_lmr_cmd(void)
{
	MPDDRC->MPDDRC_MR = MPDDRC_MR_MODE_LMR_CMD;
	/* Perform a write to a DDR memory access to acknoledge the command */
	*((uint32_t *)DDR_CS_ADDR) = 0u;
}

static void _send_ext_lmr_cmd(uint32_t opcode, uint32_t ba_offset)
{
	MPDDRC->MPDDRC_MR = MPDDRC_MR_MODE_EXT_LMR_CMD;
	/* Perform a write to a DDR memory access to acknoledge the command */
	*((uint32_t *)(DDR_CS_ADDR + (opcode << ba_offset))) = 0u;
}

static void _send_normal_cmd(void)
{
	MPDDRC->MPDDRC_MR = MPDDRC_MR_MODE_NORMAL_CMD;
	/* Perform a write to a DDR memory access to acknoledge the command */
	*((uint32_t *)DDR_CS_ADDR) = 0;
}

static void _send_precharge_cmd(void)
{
	MPDDRC->MPDDRC_MR = MPDDRC_MR_MODE_PRCGALL_CMD;
	/* Perform a write to a DDR memory access to acknoledge the command */
	*((uint32_t *)DDR_CS_ADDR) = 0;
}

static void _send_refresh_cmd(void)
{
	MPDDRC->MPDDRC_MR = MPDDRC_MR_MODE_RFSH_CMD;
	/* Perform a write to a DDR memory access to acknoledge the command */
	*((uint32_t *)DDR_CS_ADDR) = 0;
}

static void _send_lpddr_cmd(uint32_t mrs)
{
#ifdef MPDDRC_MR_MODE_LPDDR2_LPDDR3_CMD
	MPDDRC->MPDDRC_MR = MPDDRC_MR_MODE_LPDDR2_LPDDR3_CMD | MPDDRC_MR_MRS(mrs);
#else
	MPDDRC->MPDDRC_MR = MPDDRC_MR_MODE_LPDDR2_CMD | MPDDRC_MR_MRS(mrs);
#endif
	/* Perform a write access to low-power DDR2/3-SDRAM address to acknowledge the command */
	*((uint32_t *)DDR_CS_ADDR) = 0;
}

#ifdef CONFIG_HAVE_DDR3

static void _send_calib_cmd(void)
{
	MPDDRC->MPDDRC_MR = MPDDRC_MR_MODE_DEEP_CALIB_MD;
	/* Perform a write to a DDR memory access to acknoledge the command */
	*((uint32_t *)DDR_CS_ADDR) = 0;
}

static void _configure_ddr3(struct _mpddrc_desc* desc)
{
	/* Timings */
	_set_ddr_timings(desc);
	uint32_t ba_offset = _compute_ba_offset();

	/*
	 * Step 3: Issue a NOP command to the memory controller using
	 * its mode register (MPDDRC_MR).
	 */
	_send_nop_cmd();

	/*
	 * Step 4: A pause of at least 500us must be observed before a
	 * single toggle.
	 */
	timer_sleep(50);

	/*
	 * Step 5: Issue a NOP command to the memory controller using
	 * its mode register (MPDDRC_MR). CKE is now driven high.
	 */
	_send_nop_cmd();
	timer_sleep(1);

	/*
	 * Step 6: Issue Extended Mode Register Set 2 (EMRS2) cycle to
	 * choose between commercial or high temperature
	 * operations.
	 */
	_send_ext_lmr_cmd(0x2, ba_offset);
	timer_sleep(1);

	/*
	 * Step 7: Issue Extended Mode Register Set 3 (EMRS3) cycle to set
	 * the Extended Mode Register to 0.
	 */
	_send_ext_lmr_cmd(0x3, ba_offset);
	timer_sleep(1);

	/*
	 * Step 8: Issue Extended Mode Register Set 1 (EMRS1) cycle to
	 * disable and to program O.D.S. (Output Driver Strength).
	 */
	_send_ext_lmr_cmd(0x1, ba_offset);
	timer_sleep(1);

	/*
	 * Step 9: Write a one to the DLL bit (enable DLL reset) in the MPDDRC
	 * Configuration Register (MPDDRC_CR)
	 */
	/* Not done for DDR3 */

	/*
	 * Step 10: Issue a Mode Register Set (MRS) cycle to reset DLL.
	 */
	_send_lmr_cmd();
	timer_sleep(5);

	/*
	 * Step 11: Issue a Calibration command (MRS) cycle to calibrate RTT and
	 * RON values for the Process Voltage Temperature (PVT).
	 */
	_send_calib_cmd();
	timer_sleep(1);

	/*
	 * Step 12: A Normal Mode command is provided.
	 * Program the Normal mode in the MPDDRC_MR and perform a write access
	 * to any DDR3-SDRAM address to acknowledge this command.
	 */
	_send_normal_cmd();

	/*
	 * Step 13: Perform a write access to any DDR3-SDRAM address.
	 */
	*((uint32_t *)(DDR_CS_ADDR)) = 0;
}

static void _configure_lpddr3(struct _mpddrc_desc* desc)
{
	/* Timings */
	_set_ddr_timings(desc);

	/*
	 * Step 3: Issue a NOP command to the memory controller using
	 * its mode register (MPDDRC_MR).
	 */
	_send_nop_cmd();

	/*
	 * Step 4: A pause of at least 100ns must be observed before a
	 * single toggle.
	 */
	timer_sleep(1);

	/*
	 * Step 5: Issue a NOP command to the memory controller using
	 * its mode register (MPDDRC_MR). CKE is now driven high.
	 */
	_send_nop_cmd();

	/* Step 6. A pause of at least 200 μs must be observed before issuing
	a Reset command. */
	timer_sleep(20);

	/* Step 7. A Reset command is issued to the low-power DDR3-SDRAM. Write a seven
	to the MODE field and a 63 to the MRS field. */
	_send_lpddr_cmd(0x3f);

	/* Step 8. A pause of at least tINIT5 must be observed before issuing
	any commands.*/
	timer_sleep(50);

	/* Step 9. Issued a calibration command with MRS field.*/
	_send_lpddr_cmd(0xa);

	/* Step 10: Perform a write access by issuing mode register write command */
	_send_lpddr_cmd(0x1);

	/* Step 11: Program parameters (CAS Latency) by issuing mode register write command */
	_send_lpddr_cmd(0x2);

	/* Step 12: Program parameters (Drive Strength and Slew Rate) by issuing mode
	 register write command */
	_send_lpddr_cmd(0x3);

	/* Step 13: Program parameters (Partial Array Self Refresh (PASR)) by issuing mode
	 register write command */
	_send_lpddr_cmd(0x10);

	/* Step 14: Read command cycle by issuing Mode register read command */
	_send_lpddr_cmd(0x5);

	/* Step 15: Read revision by issuing Mode register read command  */
	_send_lpddr_cmd(0x6);

	/* Step 16: Read memory organization by issuing Mode register read command */
	_send_lpddr_cmd(0x8);

	/* Step 17: Read device information by issuing Mode register read command */
	_send_lpddr_cmd(0x0);

	/* Step 18: Issue a Normal mode command */
	_send_normal_cmd();
}

#endif /* CONFIG_HAVE_DDR3 */

static void _configure_ddr2(struct _mpddrc_desc* desc)
{
	/* Timings */
	_set_ddr_timings(desc);
	uint32_t ba_offset = _compute_ba_offset();

	/* Step 3: An NOP command is issued to the DDR2-SDRAM. Program
	 * the NOP command into the Mode Register and wait minimum 200
	 * us */
	_send_nop_cmd();
	timer_sleep(20);

	/* Step 4:  Issue a NOP command. */
	_send_nop_cmd();
	timer_sleep(1);

	/* Step 5: Issue all banks precharge command. */
	_send_precharge_cmd();
	timer_sleep(1);

	/* Step 6: Issue an Extended Mode Register set (EMRS2) cycle
	 * to chose between commercialor high  temperature
	 * operations. */
	_send_ext_lmr_cmd(0x2, ba_offset);
	timer_sleep(1);

	/* Step 7: Issue an Extended Mode Register set (EMRS3) cycle
	 * to set all registers to 0. */
	_send_ext_lmr_cmd(0x3, ba_offset);
	timer_sleep(1);

	/* Step 8:  Issue an Extended Mode Register set (EMRS1) cycle
	 * to enable DLL. */
	_send_ext_lmr_cmd(0x1, ba_offset);
	timer_sleep(1);

	/* Step 9:  Program DLL field into the Configuration Register. */
	MPDDRC->MPDDRC_CR |= MPDDRC_CR_DLL_RESET_ENABLED;

	/* Step 10: A Mode Register set (MRS) cycle is issued to reset DLL. */
	MPDDRC->MPDDRC_MR = MPDDRC_MR_MODE_LMR_CMD;
	/* Perform a write to a DDR memory access to acknoledge the command */
	*((uint32_t *)DDR_CS_ADDR) = 0;

	timer_sleep(1);

	/* Step 11: Issue all banks precharge command to the DDR2-SDRAM. */
	_send_precharge_cmd();
	timer_sleep(1);

	/* Step 12: Two auto-refresh (CBR) cycles are
	 * provided. Program the auto refresh command (CBR) into the
	 * Mode Register. */
	_send_refresh_cmd();
	timer_sleep(1);
	_send_refresh_cmd();
	timer_sleep(1);

	/* Step 13: Program DLL field into the Configuration Register
	 * to low(Disable DLL reset). */
	MPDDRC->MPDDRC_CR &= ~MPDDRC_CR_DLL_RESET_ENABLED;

	/* Step 14: Issue a Mode Register set (MRS) cycle to program
	 * the parameters of the DDR2-SDRAM devices. */
	_send_lmr_cmd();
	timer_sleep(1);

	/* Step 15: Program OCD field into the Configuration Register
	 * to high (OCD calibration default). */
	MPDDRC->MPDDRC_CR |= MPDDRC_CR_OCD_DDR2_DEFAULT_CALIB;

	/* Step 16: An Extended Mode Register set (EMRS1) cycle is
	 * issued to OCD default value. */
	_send_ext_lmr_cmd(0x1, ba_offset);
	timer_sleep(1);

	/* Step 19,20: A mode Normal command is provided. Program the
	 * Normal mode into Mode Register. */
	_send_normal_cmd();
	timer_sleep(1);
}

static void _configure_lpddr2(struct _mpddrc_desc* desc)
{
	/* Timings */
	_set_ddr_timings(desc);

	/* Step 3: An NOP command is issued to the DDR2-SDRAM. Program
	 * the NOP command into the Mode Register and wait minimum 200
	 * us */
	_send_nop_cmd();

	/* Step 4: A pause of at least 100 ns must be observed before a signal toggle */
	timer_sleep(1);

	/* Step 5:  Issue a NOP command. */
	_send_nop_cmd();

	/* Step 6: A pause of at least 200 μs must be observed before
	 * issuing a Reset command. */
	timer_sleep(20);

	/* Step 7. A Reset command is issued to the low-power DDR3-SDRAM. Write a seven
	 * to the MODE field and a 63 to the MRS field. */
	_send_lpddr_cmd(0x3f);

	/* Step 8. A pause of at least tINIT5 must be observed before issuing
	 * any commands.*/
	timer_sleep(50);

	/* Step 9. Issued a calibration command by issuing mode register write command */
	_send_lpddr_cmd(0xa);

	/* Step 10: Perform a write access by issuing mode register write command */
	_send_lpddr_cmd(0x1);

	/* Step 11: Program parameters (CAS Latency) by issuing mode register write command */
	_send_lpddr_cmd(0x2);

	/* Step 12: Program parameters (Drive Strength and Slew Rate) by issuing mode
	 * register write command */
	_send_lpddr_cmd(0x3);

	/* Step 13: Program parameters (Partial Array Self Refresh (PASR)) by issuing mode
	 * register write command */
	_send_lpddr_cmd(0x10);

	/* Step 14: Read command cycle by issuing Mode register read command */
	_send_lpddr_cmd(0x5);

	/* Step 15: Read revision by issuing Mode register read command  */
	_send_lpddr_cmd(0x6);

	/* Step 16: Read memory organization by issuing Mode register read command */
	_send_lpddr_cmd(0x8);

	/* Step 17: Read device information by issuing Mode register read command */
	_send_lpddr_cmd(0x0);

	/* Step 18: Issue a Normal mode command */
	_send_normal_cmd();
}

extern void mpddrc_configure(struct _mpddrc_desc* desc)
{
	/* Retrieve the current resolution to put it back later */
	uint32_t resolution = timer_get_resolution();

	/* Configure time to have 10 microseconds resolution */
	timer_configure(10);

	/* controller and DDR clock */
	pmc_enable_peripheral(ID_MPDDRC);
	pmc_enable_system_clock(PMC_SYSTEM_CLOCK_DDR);

	/* Step1: Program memory device type */
	MPDDRC->MPDDRC_MD = desc->mode;

	/* set driver impedance */
	uint32_t value = MPDDRC->MPDDRC_IO_CALIBR;
	value &= ~MPDDRC_IO_CALIBR_RDIV_Msk;
	value &= ~MPDDRC_IO_CALIBR_TZQIO_Msk;
	value &= ~MPDDRC_IO_CALIBR_CALCODEP_Msk;
	value &= ~MPDDRC_IO_CALIBR_CALCODEN_Msk;
	value |= desc->io_calibr;
	MPDDRC->MPDDRC_IO_CALIBR = value;

#ifdef MPDDRC_RD_DATA_PATH_SHIFT_SAMPLING
	MPDDRC->MPDDRC_RD_DATA_PATH = desc->data_path;
#endif

	/* Step 2: Program features of the DDR3-SDRAM device in the
	 * configuration register and timing parameter registers (TPR0
	 * ans TPR1) */

	/* Configurations */
	MPDDRC->MPDDRC_CR = desc->control;

#ifdef CONFIG_HAVE_DDR3_SELFREFRESH
	if (sfrbu_is_ddr_backup_enabled())
		/* DDR memory had been initilized and in backup mode */
		MPDDRC->MPDDRC_LPR =
			MPDDRC_LPR_LPCB_SELFREFRESH |
			MPDDRC_LPR_CLK_FR_ENABLED |
			MPDDRC_LPR_PASR(0) |
			MPDDRC_LPR_DS(2) |
			MPDDRC_LPR_TIMEOUT_NONE |
			MPDDRC_LPR_APDE_DDR2_FAST_EXIT |
			MPDDRC_LPR_UPD_MR(0);
	else
		/* DDR memory is not in backup mode */
		MPDDRC->MPDDRC_LPR =
			MPDDRC_LPR_LPCB_SELFREFRESH |
			MPDDRC_LPR_CLK_FR_ENABLED |
			MPDDRC_LPR_PASR(0) |
			MPDDRC_LPR_DS(2) |
			MPDDRC_LPR_TIMEOUT_DELAY_128_CLK |
			MPDDRC_LPR_APDE_DDR2_SLOW_EXIT |
			MPDDRC_LPR_UPD_MR(0);
#endif

	switch(desc->type) {
	case MPDDRC_TYPE_DDR2:
		_configure_ddr2(desc);
		break;
	case MPDDRC_TYPE_LPDDR2:
		_configure_lpddr2(desc);
		break;
#ifdef CONFIG_HAVE_DDR3
	case MPDDRC_TYPE_DDR3:
#ifdef CONFIG_HAVE_DDR3_SELFREFRESH
		_set_ddr_timings(desc);
		/* Initialize DDR chip when needed */
		if (!sfrbu_is_ddr_backup_enabled())
#endif /* CONFIG_HAVE_DDR3_SELFREFRESH */
		_configure_ddr3(desc);
		break;
	case MPDDRC_TYPE_LPDDR3:
		_configure_lpddr3(desc);
		break;
#endif /* CONFIG_HAVE_DDR3 */
	default:
		trace_error("DDRAM type not handled\r\n");
		abort();
	}

	/* Last step: Write the refresh rate */
	/* Refresh Timer is (64ms / (bank_size)) * master_clock */
	uint32_t master_clock = pmc_get_master_clock() / 1000000;
	MPDDRC->MPDDRC_RTR = MPDDRC_RTR_COUNT(64000 * master_clock / desc->bank);

	/* wait for end of calibration */
	timer_sleep(1);

	/* Restore resolution or put the default one if not already set */
	timer_configure(resolution);

#ifdef CONFIG_HAVE_DDR3_SELFREFRESH
	if (sfrbu_is_ddr_backup_enabled()) {
		MPDDRC->MPDDRC_MR = MPDDRC_MR_MODE_NORMAL_CMD;
		sfrbu_disable_ddr_backup();
	}
#endif
}

void mpddrc_issue_low_power_command(uint32_t cmd)
{
	uint32_t value;
	value = MPDDRC->MPDDRC_LPR & ~MPDDRC_LPR_LPCB_Msk;
	MPDDRC->MPDDRC_LPR = value | (cmd & MPDDRC_LPR_LPCB_Msk);
}
