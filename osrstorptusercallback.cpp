///////////////////////////////////////////////////////////////////////////////
//
//	(C) Copyright 2009-2010 OSR Open Systems Resources, Inc.
//	All Rights Reserved
//
//  This work is distributed under the OSR Non-Commercial Software License which is provided
//  at "http://www.osronline.com/page.cfm?name=NonCommLicense" in the hope that it will be
//  enlightening, but WITHOUT ANY WARRANTY; without even the implied warranty of MECHANTABILITY
//
//	OSR Open Systems Resources, Inc.
//	105 Route 101A Suite 19
//	Amherst, NH 03031  (603) 595-6500 FAX: (603) 595-6503
//
//    MODULE:
//
//        $File: osrstorpt\osrstorptusercallback.cpp $
//
//    ABSTRACT:
//
//      This contains the Osr Virtual Miniport Driver routines that are
//		called by the user interface module to notify it of changes in
//		the underlying user interface.
//
//    AUTHOR:
//
//        OSR Open Systems Resources, Inc.
// 
//    REVISION:   
//
//        $Revision: #2 $
//
///////////////////////////////////////////////////////////////////////////////
#include "osrstorpt.h"

PUCHAR SRB_FUNCTION_CODE_STRINGS[] = {

    //
    // SRB Functions
    //

    (PUCHAR) "SRB_FUNCTION_EXECUTE_SCSI", //           0x00
    (PUCHAR) "SRB_FUNCTION_CLAIM_DEVICE", //           0x01
    (PUCHAR) "SRB_FUNCTION_IO_CONTROL", //             0x02
    (PUCHAR) "SRB_FUNCTION_RECEIVE_EVENT", //          0x03
    (PUCHAR) "SRB_FUNCTION_RELEASE_QUEUE", //          0x04
    (PUCHAR) "SRB_FUNCTION_ATTACH_DEVICE", //          0x05
    (PUCHAR) "SRB_FUNCTION_RELEASE_DEVICE", //         0x06
    (PUCHAR) "SRB_FUNCTION_SHUTDOWN", //               0x07
    (PUCHAR) "SRB_FUNCTION_FLUSH", //                  0x08
    (PUCHAR) "SRB_FUNCTION_UNKNOWN_9", //              
    (PUCHAR) "SRB_FUNCTION_UNKNOWN_A", //              
    (PUCHAR) "SRB_FUNCTION_UNKNOWN_B", //              
    (PUCHAR) "SRB_FUNCTION_UNKNOWN_C", //              
    (PUCHAR) "SRB_FUNCTION_UNKNOWN_D", //              
    (PUCHAR) "SRB_FUNCTION_UNKNOWN_E", //              
    (PUCHAR) "SRB_FUNCTION_UNKNOWN_F", //              
    (PUCHAR) "SRB_FUNCTION_ABORT_COMMAND", //          0x10
    (PUCHAR) "SRB_FUNCTION_RELEASE_RECOVERY", //       0x11
    (PUCHAR) "SRB_FUNCTION_RESET_BUS", //              0x12
    (PUCHAR) "SRB_FUNCTION_RESET_DEVICE", //           0x13
    (PUCHAR) "SRB_FUNCTION_TERMINATE_IO", //           0x14
    (PUCHAR) "SRB_FUNCTION_FLUSH_QUEUE", //            0x15
    (PUCHAR) "SRB_FUNCTION_REMOVE_DEVICE", //          0x16
    (PUCHAR) "SRB_FUNCTION_WMI", //                    0x17
    (PUCHAR) "SRB_FUNCTION_LOCK_QUEUE", //             0x18
    (PUCHAR) "SRB_FUNCTION_UNLOCK_QUEUE" //            0x19

};

PUCHAR SCSI_STATUS_STRINGS[] = {

    (PUCHAR) "SCSISTAT_GOOD", //                  0x00
    (PUCHAR) "SCSISTAT_UNKNOWN_1",
    (PUCHAR) "SCSISTAT_CHECK_CONDITION", //       0x02
    (PUCHAR) "SCSISTAT_UNKNOWN_3",
    (PUCHAR) "SCSISTAT_CONDITION_MET", //         0x04
    (PUCHAR) "SCSISTAT_UNKNOWN_5",
    (PUCHAR) "SCSISTAT_UNKNOWN_6",
    (PUCHAR) "SCSISTAT_UNKNOWN_7",
    (PUCHAR) "SCSISTAT_BUSY", //                  0x08
    (PUCHAR) "SCSISTAT_UNKNOWN_9",
    (PUCHAR) "SCSISTAT_UNKNOWN_A",
    (PUCHAR) "SCSISTAT_UNKNOWN_B",
    (PUCHAR) "SCSISTAT_UNKNOWN_C",
    (PUCHAR) "SCSISTAT_UNKNOWN_D",
    (PUCHAR) "SCSISTAT_UNKNOWN_E",
    (PUCHAR) "SCSISTAT_UNKNOWN_F",
    (PUCHAR) "SCSISTAT_INTERMEDIATE", //          0x10
    (PUCHAR) "SCSISTAT_UNKNOWN_11",
    (PUCHAR) "SCSISTAT_UNKNOWN_12",
    (PUCHAR) "SCSISTAT_UNKNOWN_13",
    (PUCHAR) "SCSISTAT_INTERMEDIATE_COND_MET", // 0x14
    (PUCHAR) "SCSISTAT_UNKNOWN_15",
    (PUCHAR) "SCSISTAT_UNKNOWN_16",
    (PUCHAR) "SCSISTAT_UNKNOWN_17",
    (PUCHAR) "SCSISTAT_RESERVATION_CONFLICT", //  0x18
    (PUCHAR) "SCSISTAT_UNKNOWN_19",
    (PUCHAR) "SCSISTAT_UNKNOWN_1A",
    (PUCHAR) "SCSISTAT_UNKNOWN_1B",
    (PUCHAR) "SCSISTAT_UNKNOWN_1C",
    (PUCHAR) "SCSISTAT_UNKNOWN_1D",
    (PUCHAR) "SCSISTAT_UNKNOWN_1E",
    (PUCHAR) "SCSISTAT_UNKNOWN_1F",
    (PUCHAR) "SCSISTAT_UNKNOWN_20",
    (PUCHAR) "SCSISTAT_UNKNOWN_21",
    (PUCHAR) "SCSISTAT_COMMAND_TERMINATED", //    0x22
    (PUCHAR) "SCSISTAT_UNKNOWN_23",
    (PUCHAR) "SCSISTAT_UNKNOWN_24",
    (PUCHAR) "SCSISTAT_UNKNOWN_25",
    (PUCHAR) "SCSISTAT_UNKNOWN_26",
    (PUCHAR) "SCSISTAT_UNKNOWN_27",
    (PUCHAR) "SCSISTAT_QUEUE_FULL" //             0x28

};

PUCHAR SCS_STATUS_STRINGS[] = {

    (PUCHAR) "SCS_COMPLETE", //                    0x00
    (PUCHAR) "SCS_INCOMPLETE", //                  0x01
    (PUCHAR) "SCS_DMA_ERROR", //                   0x02
    (PUCHAR) "SCS_TRANSPORT_ERROR", //             0x03
    (PUCHAR) "SCS_RESET_OCCURRED", //              0x04
    (PUCHAR) "SCS_ABORTED", //                     0x05
    (PUCHAR) "SCS_TIMEOUT", //                     0x06
    (PUCHAR) "SCS_DATA_OVERRUN", //                0x07
    (PUCHAR) "SCS_DATA_UNKNOWN_8",
    (PUCHAR) "SCS_DATA_UNKNOWN_9",
    (PUCHAR) "SCS_DATA_UNKNOWN_A",
    (PUCHAR) "SCS_DATA_UNKNOWN_B",
    (PUCHAR) "SCS_DATA_UNKNOWN_C",
    (PUCHAR) "SCS_DATA_UNKNOWN_D",
    (PUCHAR) "SCS_ABORT_MSG_FAILED", //            0x0E
    (PUCHAR) "SCS_DATA_UNKNOWN_F",
    (PUCHAR) "SCS_DATA_UNKNOWN_10",
    (PUCHAR) "SCS_DATA_UNKNOWN_11",
    (PUCHAR) "SCS_DEVICE_RESET_MSG_FAILED", //     0x12
    (PUCHAR) "SCS_DATA_UNKNOWN_13",
    (PUCHAR) "SCS_DATA_UNKNOWN_14",
    (PUCHAR) "SCS_DATA_UNDERRUN", //               0x15
    (PUCHAR) "SCS_DATA_UNKNOWN_16",
    (PUCHAR) "SCS_DATA_UNKNOWN_17",
    (PUCHAR) "SCS_DATA_UNKNOWN_18",
    (PUCHAR) "SCS_DATA_UNKNOWN_19",
    (PUCHAR) "SCS_DATA_UNKNOWN_1A",
    (PUCHAR) "SCS_DATA_UNKNOWN_1B",
    (PUCHAR) "SCS_QUEUE_FULL", //                  0x1C
    (PUCHAR) "SCS_DATA_UNKNOWN_1D",
    (PUCHAR) "SCS_DATA_UNKNOWN_1E",
    (PUCHAR) "SCS_DATA_UNKNOWN_1F",
    (PUCHAR) "SCS_DATA_UNKNOWN_20",
    (PUCHAR) "SCS_DATA_UNKNOWN_21",
    (PUCHAR) "SCS_DATA_UNKNOWN_22",
    (PUCHAR) "SCS_DATA_UNKNOWN_23",
    (PUCHAR) "SCS_DATA_UNKNOWN_24",
    (PUCHAR) "SCS_DATA_UNKNOWN_25",
    (PUCHAR) "SCS_DATA_UNKNOWN_26",
    (PUCHAR) "SCS_DATA_UNKNOWN_27",
    (PUCHAR) "SCS_PORT_UNAVAILABLE", //            0x28
    (PUCHAR) "SCS_PORT_LOGGED_OUT", //             0x29
    (PUCHAR) "SCS_PORT_CONFIGURATION_CHANGED", //  0x2A

};

PUCHAR SCSI_FUNCTION_CODE_STRINGS[] = {

//
// SCSI CDB operation codes
//

(PUCHAR) "SCSIOP_TEST_UNIT_READY",  // 0x00
//(PUCHAR) "SCSIOP_REZERO_UNIT",  // 0x01
(PUCHAR) "SCSIOP_REWIND",  // 0x01
(PUCHAR) "SCSIOP_REQUEST_BLOCK_ADDR",  // 0x02
(PUCHAR) "SCSIOP_REQUEST_SENSE",  // 0x03
(PUCHAR) "SCSIOP_FORMAT_UNIT",  // 0x04
(PUCHAR) "SCSIOP_READ_BLOCK_LIMITS",  // 0x05
(PUCHAR) "SCSIOP_UNKNOWN_06",
(PUCHAR) "SCSIOP_REASSIGN_BLOCKS",  // 0x07
//(PUCHAR) "SCSIOP_INIT_ELEMENT_STATUS",  // 0x07
//(PUCHAR) "SCSIOP_READ6",  // 0x08
(PUCHAR) "SCSIOP_RECEIVE",  // 0x08
(PUCHAR) "SCSIOP_UNKNOWN_09",
(PUCHAR) "SCSIOP_WRITE6",  // 0x0A
//(PUCHAR) "SCSIOP_PRINT",  // 0x0A
//(PUCHAR) "SCSIOP_SEND",  // 0x0A
(PUCHAR) "SCSIOP_SEEK6",  // 0x0B
//(PUCHAR) "SCSIOP_TRACK_SELECT",  // 0x0B
//(PUCHAR) "SCSIOP_SLEW_PRINT",  // 0x0B
(PUCHAR) "SCSIOP_SEEK_BLOCK",  // 0x0C
(PUCHAR) "SCSIOP_PARTITION",  // 0x0D
(PUCHAR) "SCSIOP_UNKNOWN_0E",
(PUCHAR) "SCSIOP_READ_REVERSE",  // 0x0F
//(PUCHAR) "SCSIOP_WRITE_FILEMARKS",  // 0x10
(PUCHAR) "SCSIOP_FLUSH_BUFFER",  // 0x10
(PUCHAR) "SCSIOP_SPACE",  // 0x11
(PUCHAR) "SCSIOP_INQUIRY",  // 0x12
(PUCHAR) "SCSIOP_VERIFY6",  // 0x13
(PUCHAR) "SCSIOP_RECOVER_BUF_DATA",  // 0x14
(PUCHAR) "SCSIOP_MODE_SELECT",  // 0x15
(PUCHAR) "SCSIOP_RESERVE_UNIT",  // 0x16
(PUCHAR) "SCSIOP_RELEASE_UNIT",  // 0x17
(PUCHAR) "SCSIOP_COPY",  // 0x18
(PUCHAR) "SCSIOP_ERASE",  // 0x19
(PUCHAR) "SCSIOP_MODE_SENSE",  // 0x1A
(PUCHAR) "SCSIOP_START_STOP_UNIT",  // 0x1B
//(PUCHAR) "SCSIOP_STOP_PRINT",  // 0x1B
//(PUCHAR) "SCSIOP_LOAD_UNLOAD",  // 0x1B
(PUCHAR) "SCSIOP_RECEIVE_DIAGNOSTIC",  // 0x1C
(PUCHAR) "SCSIOP_SEND_DIAGNOSTIC",  // 0x1D
(PUCHAR) "SCSIOP_MEDIUM_REMOVAL",  // 0x1E
(PUCHAR) "SCSIOP_UNKNOWN_1F",
(PUCHAR) "SCSIOP_UNKNOWN_20",
(PUCHAR) "SCSIOP_UNKNOWN_21",
(PUCHAR) "SCSIOP_UNKNOWN_22",
(PUCHAR) "SCSIOP_READ_FORMATTED_CAPACITY",  // 0x23
(PUCHAR) "SCSIOP_UNKNOWN_24",
(PUCHAR) "SCSIOP_READ_CAPACITY",  // 0x25
(PUCHAR) "SCSIOP_UNKNOWN_26",
(PUCHAR) "SCSIOP_UNKNOWN_27",
(PUCHAR) "SCSIOP_READ",  // 0x28
(PUCHAR) "SCSIOP_UNKNOWN_29",
(PUCHAR) "SCSIOP_WRITE",  // 0x2A
(PUCHAR) "SCSIOP_SEEK",  // 0x2B
//(PUCHAR) "SCSIOP_LOCATE",  // 0x2B
//(PUCHAR) "SCSIOP_POSITION_TO_ELEMENT",  // 0x2B
(PUCHAR) "SCSIOP_UNKNOWN_2C",
(PUCHAR) "SCSIOP_UNKNOWN_2D",
(PUCHAR) "SCSIOP_WRITE_VERIFY",  // 0x2E
(PUCHAR) "SCSIOP_VERIFY",  // 0x2F
(PUCHAR) "SCSIOP_SEARCH_DATA_HIGH",  // 0x30
(PUCHAR) "SCSIOP_SEARCH_DATA_EQUAL",  // 0x31
(PUCHAR) "SCSIOP_SEARCH_DATA_LOW",  // 0x32
(PUCHAR) "SCSIOP_SET_LIMITS",  // 0x33
(PUCHAR) "SCSIOP_READ_POSITION",  // 0x34
(PUCHAR) "SCSIOP_SYNCHRONIZE_CACHE",  // 0x35
(PUCHAR) "SCSIOP_UNKNOWN_36",
(PUCHAR) "SCSIOP_UNKNOWN_37",
(PUCHAR) "SCSIOP_UNKNOWN_38",
(PUCHAR) "SCSIOP_COMPARE",  // 0x39
(PUCHAR) "SCSIOP_COPY_COMPARE",  // 0x3A
(PUCHAR) "SCSIOP_WRITE_DATA_BUFF",  // 0x3B
(PUCHAR) "SCSIOP_READ_DATA_BUFF",  // 0x3C
(PUCHAR) "SCSIOP_UNKNOWN_3D",
(PUCHAR) "SCSIOP_UNKNOWN_3E",
(PUCHAR) "SCSIOP_UNKNOWN_3F",
(PUCHAR) "SCSIOP_CHANGE_DEFINITION",  // 0x40
(PUCHAR) "SCSIOP_UNKNOWN_41",
(PUCHAR) "SCSIOP_READ_SUB_CHANNEL",  // 0x42
(PUCHAR) "SCSIOP_READ_TOC",  // 0x43
(PUCHAR) "SCSIOP_READ_HEADER",  // 0x44
(PUCHAR) "SCSIOP_PLAY_AUDIO",  // 0x45
(PUCHAR) "SCSIOP_UNKNOWN_46",
(PUCHAR) "SCSIOP_PLAY_AUDIO_MSF",  // 0x47
(PUCHAR) "SCSIOP_PLAY_TRACK_INDEX",  // 0x48
(PUCHAR) "SCSIOP_PLAY_TRACK_RELATIVE",  // 0x49
(PUCHAR) "SCSIOP_UNKNOWN_4A",
(PUCHAR) "SCSIOP_PAUSE_RESUME",  // 0x4B
(PUCHAR) "SCSIOP_LOG_SELECT",  // 0x4C
(PUCHAR) "SCSIOP_LOG_SENSE",  // 0x4D
(PUCHAR) "SCSIOP_STOP_PLAY_SCAN",  // 0x4E
(PUCHAR) "SCSIOP_UNKNOWN_4F",
(PUCHAR) "SCSIOP_UNKNOWN_50",
(PUCHAR) "SCSIOP_READ_DISK_INFORMATION",  // 0x51
(PUCHAR) "SCSIOP_READ_TRACK_INFORMATION",  // 0x52
(PUCHAR) "SCSIOP_UNKNOWN_53",
(PUCHAR) "SCSIOP_UNKNOWN_54",
(PUCHAR) "SCSIOP_MODE_SELECT10",  // 0x55
(PUCHAR) "SCSIOP_UNKNOWN_56",
(PUCHAR) "SCSIOP_UNKNOWN_57",
(PUCHAR) "SCSIOP_UNKNOWN_58",
(PUCHAR) "SCSIOP_UNKNOWN_59",
(PUCHAR) "SCSIOP_MODE_SENSE10",  // 0x5A
(PUCHAR) "SCSIOP_CLOSE_TRACK_SESSION", //      0x5B
(PUCHAR) "SCSIOP_READ_BUFFER_CAPACITY", //     0x5C
(PUCHAR) "SCSIOP_SEND_CUE_SHEET", //           0x5D
(PUCHAR) "SCSIOP_PERSISTENT_RESERVE_IN", //    0x5E
(PUCHAR) "SCSIOP_PERSISTENT_RESERVE_OUT", //   0x5F
(PUCHAR) "", // 0x60
(PUCHAR) "", // 0x-1
(PUCHAR) "", // 0x-2
(PUCHAR) "", // 0x-3
(PUCHAR) "", // 0x-4
(PUCHAR) "", // 0x-5
(PUCHAR) "", // 0x-6
(PUCHAR) "", // 0x-7
(PUCHAR) "", // 0x-8
(PUCHAR) "", // 0x-9
(PUCHAR) "", // 0x-a
(PUCHAR) "", // 0x-b
(PUCHAR) "", // 0x-c
(PUCHAR) "", // 0x-d
(PUCHAR) "", // 0x-e
(PUCHAR) "", // 0x-f
(PUCHAR) "", // 0x70
(PUCHAR) "", // 0x-1
(PUCHAR) "", // 0x-2
(PUCHAR) "", // 0x-3
(PUCHAR) "", // 0x-4
(PUCHAR) "", // 0x-5
(PUCHAR) "", // 0x-6
(PUCHAR) "", // 0x-7
(PUCHAR) "", // 0x-8
(PUCHAR) "", // 0x-9
(PUCHAR) "", // 0x-a
(PUCHAR) "", // 0x-b
(PUCHAR) "", // 0x-c
(PUCHAR) "", // 0x-d
(PUCHAR) "", // 0x-e
(PUCHAR) "", // 0x-f
(PUCHAR) "", // 0x80
(PUCHAR) "", // 0x-1
(PUCHAR) "", // 0x-2
(PUCHAR) "", // 0x-3
(PUCHAR) "", // 0x-4
(PUCHAR) "", // 0x-5
(PUCHAR) "", // 0x-6
(PUCHAR) "", // 0x-7
(PUCHAR) "", // 0x-8
(PUCHAR) "", // 0x-9
(PUCHAR) "", // 0x-a
(PUCHAR) "", // 0x-b
(PUCHAR) "", // 0x-c
(PUCHAR) "", // 0x-d
(PUCHAR) "", // 0x-e
(PUCHAR) "", // 0x-f
(PUCHAR) "", // 0x90
(PUCHAR) "", // 0x-1
(PUCHAR) "", // 0x-2
(PUCHAR) "", // 0x-3
(PUCHAR) "", // 0x-4
(PUCHAR) "", // 0x-5
(PUCHAR) "", // 0x-6
(PUCHAR) "", // 0x-7
(PUCHAR) "", // 0x-8
(PUCHAR) "", // 0x-9
(PUCHAR) "", // 0x-a
(PUCHAR) "", // 0x-b
(PUCHAR) "", // 0x-c
(PUCHAR) "", // 0x-d
(PUCHAR) "", // 0x-e
(PUCHAR) "", // 0x-f
(PUCHAR) "SCSIOP_REPORT_LUNS",  // 0xA0
(PUCHAR) "", // 0x-1
(PUCHAR) "", // 0x-2
(PUCHAR) "SCSIOP_SEND_KEY",  // 0xA3
(PUCHAR) "SCSIOP_REPORT_KEY",  // 0xA4
(PUCHAR) "SCSIOP_MOVE_MEDIUM",  // 0xA5
(PUCHAR) "SCSIOP_LOAD_UNLOAD_SLOT",  // 0xA6
//(PUCHAR) "SCSIOP_EXCHANGE_MEDIUM",  // 0xA6
(PUCHAR) "SCSIOP_SET_READ_AHEAD",  // 0xA7
(PUCHAR) "", // 0x-8
(PUCHAR) "", // 0x-9
(PUCHAR) "", // 0x-a
(PUCHAR) "", // 0x-b
(PUCHAR) "", // 0x-c
(PUCHAR) "SCSIOP_READ_DVD_STRUCTURE",  // 0xAD
(PUCHAR) "", // 0x-e
(PUCHAR) "", // 0x-f
(PUCHAR) "", // 0xB0
(PUCHAR) "", // 0x-1
(PUCHAR) "", // 0x-2
(PUCHAR) "", // 0x-3
(PUCHAR) "", // 0x-4
(PUCHAR) "SCSIOP_REQUEST_VOL_ELEMENT",  // 0xB5
(PUCHAR) "SCSIOP_SEND_VOLUME_TAG",  // 0xB6
(PUCHAR) "", // 0x-7
(PUCHAR) "SCSIOP_READ_ELEMENT_STATUS",  // 0xB8
(PUCHAR) "SCSIOP_READ_CD_MSF",  // 0xB9
(PUCHAR) "SCSIOP_SCAN_CD",  // 0xBA
(PUCHAR) "SCSIOP_PLAY_CD",  // 0xBC
(PUCHAR) "SCSIOP_MECHANISM_STATUS",  // 0xBD
(PUCHAR) "SCSIOP_READ_CD",  // 0xBE
(PUCHAR) "", // 0x-f
(PUCHAR) "", // 0xC0
(PUCHAR) "", // 0x-1
(PUCHAR) "", // 0x-2
(PUCHAR) "", // 0x-3
(PUCHAR) "", // 0x-4
(PUCHAR) "", // 0x-5
(PUCHAR) "", // 0x-6
(PUCHAR) "", // 0x-7
(PUCHAR) "", // 0x-8
(PUCHAR) "", // 0x-9
(PUCHAR) "", // 0x-a
(PUCHAR) "", // 0x-b
(PUCHAR) "", // 0x-c
(PUCHAR) "", // 0x-d
(PUCHAR) "", // 0x-e
(PUCHAR) "", // 0x-f
(PUCHAR) "", // 0xD0
(PUCHAR) "", // 0x-1
(PUCHAR) "", // 0x-2
(PUCHAR) "", // 0x-3
(PUCHAR) "", // 0x-4
(PUCHAR) "", // 0x-5
(PUCHAR) "", // 0x-6
(PUCHAR) "", // 0x-7
(PUCHAR) "", // 0x-8
(PUCHAR) "", // 0x-9
(PUCHAR) "", // 0x-a
(PUCHAR) "", // 0x-b
(PUCHAR) "", // 0x-c
(PUCHAR) "", // 0x-d
(PUCHAR) "", // 0x-e
(PUCHAR) "", // 0x-f
(PUCHAR) "", // 0xE0
(PUCHAR) "", // 0x-1
(PUCHAR) "", // 0x-2
(PUCHAR) "", // 0x-3
(PUCHAR) "", // 0x-4
(PUCHAR) "", // 0x-5
(PUCHAR) "", // 0x-6
(PUCHAR) "SCSIOP_INIT_ELEMENT_RANGE",  // 0xE7

};

PUCHAR SRB_STATUS_STRINGS[] = {

    (PUCHAR) "SRB_STATUS_PENDING",//                  0x00
    (PUCHAR) "SRB_STATUS_SUCCESS",//                  0x01
    (PUCHAR) "SRB_STATUS_ABORTED",//                  0x02
    (PUCHAR) "SRB_STATUS_ABORT_FAILED",//             0x03
    (PUCHAR) "SRB_STATUS_ERROR",//                    0x04
    (PUCHAR) "SRB_STATUS_BUSY",//                     0x05
    (PUCHAR) "SRB_STATUS_INVALID_REQUEST",//          0x06
    (PUCHAR) "SRB_STATUS_INVALID_PATH_ID",//          0x07
    (PUCHAR) "SRB_STATUS_NO_DEVICE",//                0x08
    (PUCHAR) "SRB_STATUS_TIMEOUT",//                  0x09
    (PUCHAR) "SRB_STATUS_SELECTION_TIMEOUT",//        0x0A
    (PUCHAR) "SRB_STATUS_COMMAND_TIMEOUT",//          0x0B
    (PUCHAR) "SRB_STATUS_MESSAGE_REJECTED",//         0x0D
    (PUCHAR) "SRB_STATUS_BUS_RESET",//                0x0E
    (PUCHAR) "SRB_STATUS_PARITY_ERROR",//             0x0F
    (PUCHAR) "SRB_STATUS_REQUEST_SENSE_FAILED",//     0x10
    (PUCHAR) "SRB_STATUS_NO_HBA",//                   0x11
    (PUCHAR) "SRB_STATUS_DATA_OVERRUN",//             0x12
    (PUCHAR) "SRB_STATUS_UNEXPECTED_BUS_FREE",//      0x13
    (PUCHAR) "SRB_STATUS_PHASE_SEQUENCE_FAILURE",//   0x14
    (PUCHAR) "SRB_STATUS_BAD_SRB_BLOCK_LENGTH",//     0x15
    (PUCHAR) "SRB_STATUS_REQUEST_FLUSHED",//          0x16
    (PUCHAR) "SRB_STATUS_INVALID_LUN",//              0x20
    (PUCHAR) "SRB_STATUS_INVALID_TARGET_ID",//        0x21
    (PUCHAR) "SRB_STATUS_BAD_FUNCTION",//             0x22
    (PUCHAR) "SRB_STATUS_ERROR_RECOVERY",//           0x23
    (PUCHAR) "SRB_STATUS_NOT_POWERED" //              0x24

};




///////////////////////////////////////////////////////////////////////////////
//
//  OsrSPCreateScsiDevice
//
//    This routine tells the OSR SP Driver that the user wants to create this
//	  new device.
//
//  INPUTS:
//
//      POSRGHandle - OSR SP Global Handle, received when Driver called OsrUserInitialize.
//
//  OUTPUTS:
//
//      None.
//
//  RETURNS:
//
//      TRUE if you can start, FALSE otherwise.
//
//  IRQL:
//
//    This routine is called at IRQL < DISPATCH_LEVEL.
//
//  NOTES:
//
//
///////////////////////////////////////////////////////////////////////////////
PVOID OsrSPCreateScsiDevice(IN PVOID POSRGHandle,IN ULONG PathId,
                            IN ULONG TargetId,IN ULONG Lun,
                            IN PVOID PUserLocalHandle,
                            IN BOOLEAN BReadOnlyDevice,
                            PINQUIRYDATA PInquiryData, ULONG ExtraStackLocations)
{
	POSR_DEVICE_EXTENSION	pDevExt = (POSR_DEVICE_EXTENSION) POSRGHandle;
	POSR_VM_DEVICE			pOsrDev = NULL;
	KIRQL		lockHandle;
	POSR_LU_EXTENSION		luExt;

	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,(__FUNCTION__": Entered\n"));

	OSR_DEVEXT_VALID(pDevExt);

	//
	//  Get the address of the logical unit for this device.
	//
    luExt = (POSR_LU_EXTENSION) StorPortGetLogicalUnit(pDevExt,
                                   (UCHAR) PathId,
                                   (UCHAR) TargetId,
                                   (UCHAR) Lun);

	if(luExt) {
		OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_PNP_INFO,
			(__FUNCTION__": LuExt %p exists for %d:%d:%d\n",luExt,PathId,TargetId,Lun));
		return NULL;
	}

	pOsrDev = (POSR_VM_DEVICE) ExAllocatePoolWithTag(NonPagedPool,sizeof(OSR_VM_DEVICE),'vRSO');

	if(!pOsrDev) {
		OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_PNP_INFO,
			(__FUNCTION__": Allocation failure for for %d:%d:%d\n",luExt,PathId,TargetId,Lun));
		return NULL;
	}

	OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_PNP_INFO,
		(__FUNCTION__": POsrDev %p for PUserLocalInformation %p Now exists for LuExt %p at %d:%d:%d\n",
		pOsrDev,PUserLocalHandle,luExt,PathId,TargetId,Lun));

	RtlZeroMemory(pOsrDev,sizeof(OSR_VM_DEVICE));
	pOsrDev->PUserLocalInformation = PUserLocalHandle;
	pOsrDev->PathId = PathId;
	pOsrDev->TargetId = TargetId;
	pOsrDev->Lun = Lun;
	pOsrDev->PInquiryData = PInquiryData;
	pOsrDev->BReadOnlyDevice = BReadOnlyDevice;
	pOsrDev->Missing = FALSE;
	pOsrDev->PDevExt = (PVOID) pDevExt;
	pOsrDev->MagicNumber = OSR_VM_DEVICE_MAGIC;

	OsrAcquireSpinLock(&pDevExt->DeviceListLock,&lockHandle);

	InsertTailList(&pDevExt->DeviceList,&pOsrDev->ListEntry);

	OsrReleaseSpinLock(&pDevExt->DeviceListLock,lockHandle);

	OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_PNP_INFO,
		(__FUNCTION__": LuExt %p at %d:%d:%d is now present\n",
		luExt,PathId,TargetId,Lun));


	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,(__FUNCTION__": Exit\n"));

	return pOsrDev;
}


///////////////////////////////////////////////////////////////////////////////
//
//  OsrSPSetDeviceRemovable
//
//    This routine tells the OSR SP Driver that the user wants this device to
//    really go away, i.e. be deleted.  This must be called before the user
//    announces departure.
//
//  INPUTS:
//
//      POSRGHandle - OSR SP Global Handle, received when Driver called OsrUserInitialize.
//
//  OUTPUTS:
//
//      None.
//
//  RETURNS:
//
//      TRUE if you can start, FALSE otherwise.
//
//  IRQL:
//
//    This routine is called at IRQL < DISPATCH_LEVEL.
//
//  NOTES:
//
//
///////////////////////////////////////////////////////////////////////////////
BOOLEAN OsrSPSetDeviceRemovable(IN PVOID POSRLHandle,BOOLEAN Force)
{
	POSR_VM_DEVICE			pOsrDevice = (POSR_VM_DEVICE) POSRLHandle;

	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,(__FUNCTION__": Enter\n"));

	OSR_VM_DEVICE_VALID(pOsrDevice);

	if(pOsrDevice->OutstandingIoCount && !Force) {
		return FALSE;
	}
	
	OSRASSERT(!pOsrDevice->OutstandingIoCount);

	pOsrDevice->Missing = TRUE;

	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,(__FUNCTION__": Exit\n"));

	return TRUE;
}


///////////////////////////////////////////////////////////////////////////////
//
//  OsrSPDecOutstandingIoCount
//
//      This routine decrements as it completes the request it receives
//
//  INPUTS:
//
//      POSRLHandle - address of user exception
//
//  OUTPUTS:
//
//      None.
//
//  RETURNS:
//
//      None.
//
//  IRQL:
//
//    This routine is called at IRQL <= DISPATCH_LEVEL.
//
//  NOTES:
//
//
///////////////////////////////////////////////////////////////////////////////
void OsrSPDecOutstandingIoCount(IN PVOID POSRLHandle)
{
	POSR_VM_DEVICE			pOsrDevice = (POSR_VM_DEVICE) POSRLHandle;

	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,(__FUNCTION__": Enter\n"));

	OSR_VM_DEVICE_VALID(pOsrDevice);

	InterlockedDecrement(&pOsrDevice->OutstandingIoCount);
	
	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,(__FUNCTION__": Exit\n"));
}


///////////////////////////////////////////////////////////////////////////////
//
//  OsrSpGetDeviceObject
//
//    This routine is called by the user to get the Device's Device Object structure.
//
//  INPUTS:
//
//      POSRLHandle - OSR SP Local Handle, received user called OsrSPCreateScsiDevice
//
//  OUTPUTS:
//
//      None.
//
//  RETURNS:
//
//      None.
//
//  IRQL:
//
//    This routine is called at any IRQL.
//
//  NOTES:
//
///////////////////////////////////////////////////////////////////////////////
PDEVICE_OBJECT OsrSpGetDeviceObject(IN PVOID POSRLHandle)
{
	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,(__FUNCTION__": Enter\n"));
	DbgBreakPoint();
	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,(__FUNCTION__": Exit\n"));
	return NULL;
}

///////////////////////////////////////////////////////////////////////////////
//
//  OsrSPGetDriverObject
//
//    This routine is called by the user to get the Driver's DriverObject structure.
//
//  INPUTS:
//
//      POSRGHandle - OSR SP Global Handle, received when Driver called OsrUserInitialize.
//
//  OUTPUTS:
//
//      None.
//
//  RETURNS:
//
//      None.
//
//  IRQL:
//
//    This routine is called at any IRQL.
//
//  NOTES:
//
///////////////////////////////////////////////////////////////////////////////
PDRIVER_OBJECT OsrSPGetDriverObject(IN PVOID POSRGHandle)
{
	POSR_DEVICE_EXTENSION	pDevExt = (POSR_DEVICE_EXTENSION) POSRGHandle;
	ULONG					storStatus;
	PDEVICE_OBJECT			ado,pdo,ldo;

	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,(__FUNCTION__": Entered\n"));

	OSR_DEVEXT_VALID(pDevExt);

	storStatus = StorPortGetDeviceObjects(POSRGHandle,(PVOID*)&ado,(PVOID*)&pdo,(PVOID*)&ldo);

	if(storStatus == STOR_STATUS_SUCCESS) {
		return ado->DriverObject;
	}
	
	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,(__FUNCTION__": Exit\n"));
	return NULL;
}


///////////////////////////////////////////////////////////////////////////////
//
//  OsrSPAnnounceArrival
//
//    This routine Announces the arrival of a disk to the PnP Manager.
//
//  INPUTS:
//
//      POSRGHandle - OSR SP Global Handle, received when Driver called OsrUserInitialize.
//
//  OUTPUTS:
//
//      None.
//
//  RETURNS:
//
//      None.
//
//  IRQL:
//
//    This routine is called at IRQL < DISPATCH_LEVEL.
//
//  NOTES:
//
///////////////////////////////////////////////////////////////////////////////
void OsrSPAnnounceArrival(IN PVOID POSRGHandle)
{
	POSR_DEVICE_EXTENSION	pDevExt = (POSR_DEVICE_EXTENSION) POSRGHandle;

	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,(__FUNCTION__": Entered\n"));

	OSR_DEVEXT_VALID(pDevExt);

	StorPortNotification(BusChangeDetected,POSRGHandle,0);

	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,(__FUNCTION__": Exit\n"));
}


///////////////////////////////////////////////////////////////////////////////
//
//  OsrSPAnnounceDeparture
//
//    This routine Announces the departure of a device.
//
//  INPUTS:
//
//      POSRGHandle - OSR SP Global Handle, received when Driver called OsrUserInitialize.
//
//  OUTPUTS:
//
//      None.
//
//  RETURNS:
//
//      None.
//
//  IRQL:
//
//    This routine is called at IRQL < DISPATCH_LEVEL.
//
//  NOTES:
//
//      This doesn't do any good if the user has not called OsrSPSetDeviceRemovable.
//
///////////////////////////////////////////////////////////////////////////////
void OsrSPAnnounceDeparture(IN PVOID POSRGHandle)
{
	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,(__FUNCTION__": Enter\n"));
	OsrSPAnnounceArrival(POSRGHandle);
	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,(__FUNCTION__": Exit\n"));
}


///////////////////////////////////////////////////////////////////////////////
//
//  OsrSpGetSrbDataAddress
//
//    This routine gets the address of the data buffer involved in the transfer.
//
//  INPUTS:
//
//      POSRLHandle - OSR SP Local Handle.
//		PSrb - address of SRB whose data buffer address we want...
//
//  OUTPUTS:
//
//      None.
//
//  RETURNS:
//
//      The buffer address or NULL.
//
//  IRQL:
//
//    This routine is called at IRQL < DISPATCH_LEVEL.
//
//  NOTES:
//
//		None.
//
///////////////////////////////////////////////////////////////////////////////
PVOID	OsrSpGetSrbDataAddress(IN PVOID POSRLHandle,PSCSI_REQUEST_BLOCK PSrb)
{
	POSR_VM_DEVICE			pOsrDevice = (POSR_VM_DEVICE) POSRLHandle;
	PVOID					pData = NULL;
	ULONG					storStatus;

	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,(__FUNCTION__": Enter\n"));

	OSR_VM_DEVICE_VALID(pOsrDevice);

	//
	//	Ask StorPort to do MmGetSystemAddressForMdlSafe, using the MDL that can 
	//	be found internally via the SRB.
	//
	storStatus =  StorPortGetSystemAddress(pOsrDevice->PDevExt,PSrb,&pData);

	if (storStatus != STOR_STATUS_SUCCESS) {                                      
		OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_USER,
			(__FUNCTION__": Failed to get system address for pSrb = 0x%p"
			", pSrb->DataBuffer=0x%p, status=0x%x\n", PSrb, PSrb->DataBuffer,storStatus));

		return NULL;   
	}

	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,(__FUNCTION__": Exit\n"));

	return pData;
}


///////////////////////////////////////////////////////////////////////////////
//
//  OsrSpGetSrbMdl
//
//    This routine gets the MDL address of the data buffer involved in the transfer.
//
//  INPUTS:
//
//      POSRLHandle - OSR SP Local Handle.
//		PSrb - address of SRB whose data buffer address we want...
//
//  OUTPUTS:
//
//      None.
//
//  RETURNS:
//
//      The MDL address or NULL.
//
//  IRQL:
//
//    This routine is called at IRQL < DISPATCH_LEVEL.
//
//  NOTES:
//
//		None.
//
///////////////////////////////////////////////////////////////////////////////
PMDL OsrSpGetSrbMdl(IN PVOID POSRLHandle,PSCSI_REQUEST_BLOCK PSrb)
{
	POSR_VM_DEVICE			pOsrDevice = (POSR_VM_DEVICE) POSRLHandle;
	PMDL					pMdl = NULL;
	ULONG					storStatus;

	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,(__FUNCTION__": Enter\n"));

	OSR_VM_DEVICE_VALID(pOsrDevice);

	//
	//	Ask StorPort to do MmGetSystemAddressForMdlSafe, using the MDL that can 
	//	be found internally via the SRB.
	//
	storStatus = StorPortGetOriginalMdl(pOsrDevice->PDevExt,PSrb,(PVOID*) &pMdl);

	if (storStatus != STOR_STATUS_SUCCESS) {                                      
		OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_USER,
			(__FUNCTION__": Failed to get Mdl address for pSrb = 0x%p"
			", pSrb->DataBuffer=0x%p, Status=0x%x\n", PSrb, PSrb->DataBuffer,storStatus));

		return NULL;   
	}

	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,(__FUNCTION__": Exit\n"));

	return pMdl;
}


///////////////////////////////////////////////////////////////////////////////
//
//  OsrSpCompleteSrb
//
//    This routine is called to notify StorPort that an SRB is complete.
//
//  INPUTS:
//
//      POSRLHandle - OSR SP Local Handle.
//		PSrb - address of SRB to complete
//
//  OUTPUTS:
//
//      None.
//
//  RETURNS:
//
//      None.
//
//  IRQL:
//
//    This routine is called at IRQL < DISPATCH_LEVEL.
//
//  NOTES:
//
//		None.
//
///////////////////////////////////////////////////////////////////////////////
void    OsrSpCompleteSrb(IN PVOID POSRLHandle,PSCSI_REQUEST_BLOCK PSrb)
{
	POSR_VM_DEVICE			pOsrDevice = (POSR_VM_DEVICE) POSRLHandle;

	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,(__FUNCTION__": Enter\n"));

	StorPortNotification(RequestComplete, pOsrDevice->PDevExt, PSrb);

	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,(__FUNCTION__": Exit\n"));
}


///////////////////////////////////////////////////////////////////////////////
//
//  OsrSPCanUserStart
//
//    This routine allows the user to ask the SCSI Port driver if the User can
//    start initializing after a IRP_MN_START_DEVICE was received by the SCSI Adapter.
//
//    NOTE:   If this routine returns true the Windows 2000 Network layers have been
//            initialized and are functioning.
//
//  INPUTS:
//
//      POSRLHandle - OSR SP Global Handle, received when Driver called OsrSPCreateScsiDevice
//
//  OUTPUTS:
//
//      None.
//
//  RETURNS:
//
//      None.
//
//  IRQL:
//
//    This routine is called at IRQL < DISPATCH_LEVEL.
//
//  NOTES:
//
//      The device had better be started and present.
//
///////////////////////////////////////////////////////////////////////////////
BOOLEAN OsrSPCanUserStart(IN PVOID POSRGHandle) 
{
	return TRUE;
}


///////////////////////////////////////////////////////////////////////////////
//
//  PrintSCSICDBOperation
//
//      This routine is called to decipher a SCSI CDB Operation.
//
//  INPUTS:
//
//      Operation - The Operation to decipher.
//
//  OUTPUTS:
//
//      None.
//
//  RETURNS:
//
//      The Deciphered String.
//
//  IRQL:
//
//      This routine is called at any IRQL.
//
//  NOTES:
//
//
///////////////////////////////////////////////////////////////////////////////
PUCHAR OsrSpPrintSCSICDBOperation(UCHAR Operation)
{
    if(Operation > sizeof(SCSI_FUNCTION_CODE_STRINGS)/sizeof(PUCHAR)) {
		OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_SRB,
			("SCSI Cdb Operation Out Of Range 0x%x.\n",Operation));
        return (PUCHAR) "SCSI Cdb Operation Out Of Range";
    }

    return SCSI_FUNCTION_CODE_STRINGS[Operation];
}

void OsrSpPrintCdb10(PCDB PCdb)
{
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("Printing Generic CDB10 Structure.\n"));

    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\tOperationCode = %s.\n",(CCHAR*) OsrSpPrintSCSICDBOperation(PCdb->CDB10.OperationCode)));

    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\tRelativeAddress = %x.\n",PCdb->CDB10.RelativeAddress & 0x00000001));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\tForceUnitAccess = %x.\n",(PCdb->CDB10.ForceUnitAccess & 0x000000004) >> 2));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\tDisablePageOut = %x.\n",(PCdb->CDB10.DisablePageOut & 0x00000008) >> 3));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,("\tLogicalUnitNumber = %x.\n",
		(PCdb->CDB10.LogicalUnitNumber & 0x00000070) >> 4));

    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\tLogicalBlockByte0 = %x.\n",PCdb->CDB10.LogicalBlockByte0));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\tLogicalBlockByte1 = %x.\n",PCdb->CDB10.LogicalBlockByte1));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\tLogicalBlockByte2 = %x.\n",PCdb->CDB10.LogicalBlockByte2));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\tLogicalBlockByte3 = %x.\n",PCdb->CDB10.LogicalBlockByte3));

    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\tTransferBlocksMsb = %x.\n",PCdb->CDB10.TransferBlocksMsb));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\tTransferBlocksLsb = %x.\n",PCdb->CDB10.TransferBlocksLsb));

    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\tControl = %x.\n",PCdb->CDB10.Control));

    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,("\n"));
}

VOID OsrSpPrintCdb12(PCDB PCdb)
{
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("Printing Generic CDB12 Structure.\n"));

    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\tOperationCode = %s.\n",(CCHAR*) OsrSpPrintSCSICDBOperation(PCdb->CDB12.OperationCode)));

    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\tRelativeAddress = %x.\n",PCdb->CDB12.RelativeAddress & 0x01));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\tForceUnitAccess = %x.\n",(PCdb->CDB12.ForceUnitAccess & 0x04) >> 2));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\tDisablePageOut = %x.\n",(PCdb->CDB12.DisablePageOut & 0x08) >> 3));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\tLogicalUnitNumber = %x.\n",(PCdb->CDB12.LogicalUnitNumber & 0x70) >> 4));

    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\tLogicalBlock 0 = %x.\n",PCdb->CDB12.LogicalBlock[0]));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\tLogicalBlock 1 = %x.\n",PCdb->CDB12.LogicalBlock[1]));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\tLogicalBlock 2 = %x.\n",PCdb->CDB12.LogicalBlock[2]));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\tLogicalBlock 3 = %x.\n",PCdb->CDB12.LogicalBlock[3]));

    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\tTransferLength 0 = %x.\n",PCdb->CDB12.TransferLength[0]));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\tTransferLength 1 = %x.\n",PCdb->CDB12.TransferLength[1]));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\tTransferLength 2 = %x.\n",PCdb->CDB12.TransferLength[2]));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\tTransferLength 3 = %x.\n",PCdb->CDB12.TransferLength[3]));

    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\tControl = %x.\n",PCdb->CDB10.Control));

    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,("\n"));
}

VOID OsrSpPrintModeSense(UCHAR Type,PCDB PCdb)
{

    switch(Type) {

        case SCSIOP_MODE_SENSE:

            OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
				("Printing SCSIOP_MODE_SENSE CDB6 Structure.\n"));

            OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
				("\tOperationCode = %s.\n",(CCHAR*) OsrSpPrintSCSICDBOperation(PCdb->MODE_SENSE.OperationCode)));
            OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
				("\tDdb = %x.\n",(PCdb->MODE_SENSE.Dbd & 0x08) >> 3));
            OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
				("\tLogicalUnitNumber = %x.\n",(PCdb->MODE_SENSE.LogicalUnitNumber & 0xE0) >> 5));
            OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
				("\tPageCode = %x\n",PCdb->MODE_SENSE.PageCode & 0x3F));
            OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
				("\tPc = %x\n",(PCdb->MODE_SENSE.Pc & 0xC0) >> 6));

            OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
				("\tAllocationLength = %x\n",PCdb->MODE_SENSE.AllocationLength));
            OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
				("\tControl = %x\n",PCdb->MODE_SENSE.Control));

            OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,("\n"));
            break;

        case SCSIOP_MODE_SENSE10:

            OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
				("Printing SCSIOP_MODE_SENSE10 CDB10 Structure.\n"));

            OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
				("\tOperationCode = %s.\n",(CCHAR*) OsrSpPrintSCSICDBOperation(PCdb->MODE_SENSE10.OperationCode)));
            OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
				("\tDdb = %x.\n",(PCdb->MODE_SENSE10.Dbd & 0x08) >> 3));
            OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
				("\tLogicalUnitNumber = %x.\n",(PCdb->MODE_SENSE10.LogicalUnitNumber & 0xE0) >> 5));
            OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
				("\tPageCode = %x\n",PCdb->MODE_SENSE10.PageCode & 0x3F));
            OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
				("\tPc = %x\n",(PCdb->MODE_SENSE10.Pc & 0xC0) >> 6));

            OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
				("\tAllocationLength 0 = %x\n",PCdb->MODE_SENSE10.AllocationLength[0]));
            OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
				("\tAllocationLength 1 = %x\n",PCdb->MODE_SENSE10.AllocationLength[1]));

            OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
				("\tControl = %x\n",PCdb->MODE_SENSE10.Control));

            OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,("\n"));
            break;

        default:
            break;

    }
}


///////////////////////////////////////////////////////////////////////////////
//
//  PrintScsiInquiryData
//
//    This routine prints SCSI Inquiry Information.
//
//  INPUTS:
//
//      Bus - Bus Number.
//      Target - Target Number.
//      Lun - Lun Number;
//      PInquiryData - Address of Inquiry Information.
//
//  OUTPUTS:
//
//      None.
//
//  RETURNS:
//
//      None.
//
//  IRQL:
//
//    This routine is called at any IRQL.
//
//  NOTES:
//
///////////////////////////////////////////////////////////////////////////////
VOID OsrSpPrintScsiInquiryData(UCHAR Bus,UCHAR Target,UCHAR Lun,PINQUIRYDATA PInquiryData)
{
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("InquiryData: Device At BUS %d, Target %d, Lun %d\n",Bus,Target,Lun));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\t\t DeviceType: %x\n",(PInquiryData->DeviceType & 0x1F)));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\t\t DeviceTypeQualifier: %x\n",(PInquiryData->DeviceTypeQualifier & 0x7)));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\t\t DeviceTypeModifier: %x\n",(PInquiryData->DeviceTypeModifier & 0x7F)));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\t\t RemovableMedia: %x\n",(PInquiryData->RemovableMedia & 0x1)));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\t\t Versions: %x\n",PInquiryData->Versions));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\t\t ResponseDataFormat: %x\n",(PInquiryData->ResponseDataFormat & 0xF)));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\t\t HiSupport: %x\n",(PInquiryData->HiSupport & 0x1)));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\t\t NormACA: %x\n",(PInquiryData->NormACA & 0x1)));
//    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,("\t\t ReservedBit: %x\n",(PInquiryData->ReservedBit & 0x1)));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\t\t AERC: %x\n",(PInquiryData->AERC & 0x1)));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\t\t AdditionalLength: %x\n",PInquiryData->AdditionalLength));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\t\t SoftReset: %x\n",(PInquiryData->SoftReset & 0x1)));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\t\t CommandQueue: %x\n",(PInquiryData->CommandQueue & 0x1)));
//    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,("\t\t Reserved2: %x\n",(PInquiryData->Reserved2 & 0x1)));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\t\t LinkedCommands: %x\n",(PInquiryData->LinkedCommands & 0x1)));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\t\t Synchronous: %x\n",(PInquiryData->Synchronous & 0x1)));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\t\t Wide16Bit: %x\n",(PInquiryData->Wide16Bit & 0x1)));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\t\t Wide32Bit: %x\n",(PInquiryData->Wide32Bit & 0x1)));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\t\t RelativeAddressing: %x\n",(PInquiryData->RelativeAddressing & 0x1)));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\t\t VendorId: %1.1c%1.1c%1.1c%1.1c%1.1c%1.1c%1.1c%1.1c\n",
                PInquiryData->VendorId[0],PInquiryData->VendorId[1],PInquiryData->VendorId[2],
                PInquiryData->VendorId[3],PInquiryData->VendorId[4],PInquiryData->VendorId[5],
                PInquiryData->VendorId[6],PInquiryData->VendorId[7]));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\t\t ProductId: %1.1c%1.1c%1.1c%1.1c%1.1c%1.1c%1.1c%1.1c%1.1c%1.1c%1.1c%1.1c%1.1c%1.1c%1.1c%1.1c\n",
                PInquiryData->ProductId[0],PInquiryData->ProductId[1],PInquiryData->ProductId[2],
                PInquiryData->ProductId[3],PInquiryData->ProductId[4],PInquiryData->ProductId[5],
                PInquiryData->ProductId[6],PInquiryData->ProductId[7],
                PInquiryData->ProductId[8],PInquiryData->ProductId[9],PInquiryData->ProductId[10],
                PInquiryData->ProductId[11],PInquiryData->ProductId[12],PInquiryData->ProductId[13],
                PInquiryData->ProductId[14],PInquiryData->ProductId[15]));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\t\t ProductRevisionLevel: %1.1x%1.1x%1.1x%1.1x%\n",
                PInquiryData->ProductRevisionLevel[0],PInquiryData->ProductRevisionLevel[1],
                PInquiryData->ProductRevisionLevel[2],PInquiryData->ProductRevisionLevel[3]));
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("\t\t ProductId: %1.1x%1.1x%1.1x%1.1x%1.1x%1.1x%1.1x%1.1x%1.1x%1.1x%1.1x%1.1x%1.1x%1.1x%1.1x%1.1x%1.1x%1.1x%1.1x%1.1x\n",
                PInquiryData->VendorSpecific[0],PInquiryData->VendorSpecific[1],PInquiryData->VendorSpecific[2],
                PInquiryData->VendorSpecific[3],PInquiryData->VendorSpecific[4],PInquiryData->VendorSpecific[5],
                PInquiryData->VendorSpecific[6],PInquiryData->VendorSpecific[7],
                PInquiryData->VendorSpecific[8],PInquiryData->VendorSpecific[9],PInquiryData->VendorSpecific[10],
                PInquiryData->VendorSpecific[11],PInquiryData->VendorSpecific[12],PInquiryData->VendorSpecific[13],
                PInquiryData->VendorSpecific[14],PInquiryData->VendorSpecific[15],
                PInquiryData->VendorSpecific[16],PInquiryData->VendorSpecific[17],PInquiryData->VendorSpecific[18],
                PInquiryData->VendorSpecific[19]));
}


///////////////////////////////////////////////////////////////////////////////
//
//  PrintSCSStatus
//
//      This routine is called to decipher a SCSI Status.
//
//  INPUTS:
//
//      Status - The status to decipher.
//
//  OUTPUTS:
//
//      None.
//
//  RETURNS:
//
//      The Deciphered String.
//
//  IRQL:
//
//      This routine is called at any IRQL.
//
//  NOTES:
//
//
///////////////////////////////////////////////////////////////////////////////
PUCHAR OsrSpPrintSCSStatus(USHORT Status)
{
    if(Status > sizeof(SCS_STATUS_STRINGS)/sizeof(PUCHAR)) {

        return (PUCHAR) "SCS Status Out Of Range";

    }

    return SCS_STATUS_STRINGS[Status];

    
}


///////////////////////////////////////////////////////////////////////////////
//
//  OsrSpPrintSRBStatus
//
//      This routine is called to decipher a SCSI Status.
//
//  INPUTS:
//
//      Status - The status to decipher.
//
//  OUTPUTS:
//
//      None.
//
//  RETURNS:
//
//      The Deciphered String.
//
//  IRQL:
//
//      This routine is called at any IRQL.
//
//  NOTES:
//
//
///////////////////////////////////////////////////////////////////////////////
PUCHAR OsrSpPrintSRBStatus(USHORT Status)
{
    if(Status > sizeof(SRB_STATUS_STRINGS)/sizeof(PUCHAR)) {

        return (PUCHAR) "SRB Status Out Of Range";

    }

    return SRB_STATUS_STRINGS[Status];

    
}


///////////////////////////////////////////////////////////////////////////////
//
//  OsrSpPrintSCSIStatus
//
//      This routine is called to decipher a SCSI Status.
//
//  INPUTS:
//
//      Status - The status to decipher.
//
//  OUTPUTS:
//
//      None.
//
//  RETURNS:
//
//      The Deciphered String.
//
//  IRQL:
//
//      This routine is called at any IRQL.
//
//  NOTES:
//
//
///////////////////////////////////////////////////////////////////////////////
PUCHAR OsrSpPrintSCSIStatus(USHORT Status)
{
    if((Status & 0x00FF) > sizeof(SCSI_STATUS_STRINGS)/sizeof(PUCHAR)) {

        return (PUCHAR) "SCSI Status Out Of Range";

    }

    return SCSI_STATUS_STRINGS[Status & 0x00FF];

}

