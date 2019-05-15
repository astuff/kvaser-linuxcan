/*
**             Copyright 2017 by Kvaser AB, Molndal, Sweden
**                         http://www.kvaser.com
**
** This software is dual licensed under the following two licenses:
** BSD-new and GPLv2. You may use either one. See the included
** COPYING file for details.
**
** License: BSD-new
** ==============================================================================
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**     * Redistributions of source code must retain the above copyright
**       notice, this list of conditions and the following disclaimer.
**     * Redistributions in binary form must reproduce the above copyright
**       notice, this list of conditions and the following disclaimer in the
**       documentation and/or other materials provided with the distribution.
**     * Neither the name of the <organization> nor the
**       names of its contributors may be used to endorse or promote products
**       derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
** LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
** CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
** SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
** BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
** IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
** ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
** POSSIBILITY OF SUCH DAMAGE.
**
**
** License: GPLv2
** ==============================================================================
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
**
**
** IMPORTANT NOTICE:
** ==============================================================================
** This source code is made available for free, as an open license, by Kvaser AB,
** for use with its applications. Kvaser AB does not accept any liability
** whatsoever for any third party patent or other immaterial property rights
** violations that may result from any usage of this source code, regardless of
** the combination of source code and various applications that it can be used
** in, or with.
**
** -----------------------------------------------------------------------------
*/

/*
** Description:
**  Defines in common for Script devices (both kcanl and kcany)
**
** -----------------------------------------------------------------------------
*/

#ifndef KCANIO_SCRIPT_H
#define KCANIO_SCRIPT_H

//---------------------------------------------------------------------------
// NOTE that these defines HAVE to exactly match the defins in both hydra_host_cmds.h
// and filo_cmds.h, if you add a define add a compiler assert in /drv/
//---------------------------------------------------------------------------

// -------------------
// envvar
#define KCANIO_SCRIPT_ENVVAR_SUBCMD_SET_START         1
#define KCANIO_SCRIPT_ENVVAR_SUBCMD_GET_START         2

#define KCANIO_SCRIPT_ENVVAR_RESP_OK                  0
#define KCANIO_SCRIPT_ENVVAR_RESP_UNKNOWN_VAR         1
#define KCANIO_SCRIPT_ENVVAR_RESP_WRONG_VAR_LEN       2
#define KCANIO_SCRIPT_ENVVAR_RESP_OUT_OF_MEMORY       3

#define KCANIO_SCRIPT_CTRL_ERR_SUCCESS                0
#define KCANIO_SCRIPT_CTRL_ERR_NO_MORE_PROCESSES      1
#define KCANIO_SCRIPT_CTRL_ERR_FILE_NOT_FOUND         2
#define KCANIO_SCRIPT_CTRL_ERR_OPEN_FILE_ERR          3
#define KCANIO_SCRIPT_CTRL_ERR_OPEN_FILE_NO_MEM       4
#define KCANIO_SCRIPT_CTRL_ERR_FILE_READ_ERR          5
#define KCANIO_SCRIPT_CTRL_ERR_LOAD_FILE_ERR          6
#define KCANIO_SCRIPT_CTRL_ERR_OUT_OF_CODE_MEM        7
#define KCANIO_SCRIPT_CTRL_ERR_FILE_REWIND_FAIL       8
#define KCANIO_SCRIPT_CTRL_ERR_LOAD_FAIL              9
#define KCANIO_SCRIPT_CTRL_ERR_SETUP_FAIL            10
#define KCANIO_SCRIPT_CTRL_ERR_SETUP_FUN_TABLE_FAIL  11
#define KCANIO_SCRIPT_CTRL_ERR_SETUP_PARAMS_FAIL     12
#define KCANIO_SCRIPT_CTRL_ERR_PROCESSES_NOT_FOUND   13
#define KCANIO_SCRIPT_CTRL_ERR_START_FAILED          14
#define KCANIO_SCRIPT_CTRL_ERR_STOP_FAILED           15
#define KCANIO_SCRIPT_CTRL_ERR_SPI_BUSY              16
#define KCANIO_SCRIPT_CTRL_ERR_PROCESS_NOT_STOPPED   17
#define KCANIO_SCRIPT_CTRL_ERR_PROCESS_NOT_RUNNING   18
#define KCANIO_SCRIPT_CTRL_ERR_ENVVAR_NOT_FOUND      19

#define KCANIO_SCRIPT_CTRL_ERR_UNKNOWN_COMMAND       20
#define KCANIO_SCRIPT_CTRL_ERR_PROCESS_NOT_LOADED    21
#define KCANIO_SCRIPT_CTRL_ERR_COMPILER_VERSION      22
#define KCANIO_SCRIPT_CTRL_ERR_INVALID_PARAMETER     23

#define KCANIO_SCRIPT_CTRL_ERR_NOT_IMPLEMENTED       43

/*****************************************************************************/

#endif /* KCANIO_SCRIPT_H */
