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
//        $File: osrstorpt\osrstorptioCtrl.cpp $
//
//    ABSTRACT:
//
//      This contains the Osr Virtual Miniport Driver SRB IOCTL processing
//		routines.
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
#include "Osrstorpt.h"



///////////////////////////////////////////////////////////////////////////////
//
//	OsrVmIoControl
//	
//		This routine is called to handle an input SRB_FUNCTION_IO_CONTROL
//		request.   This function must process the IOCTL
//		and do the correct processing.
//
//	INPUTS:
//
//		PDevExt - pointer to our non-paged storage area
//		PSrb - SRB to process
//
//	OUTPUTS:
//
//		None.
//
//	RETURNS:
//
//		TRUE. 
//
//	IRQL REQUIREMENTS:
//
//		IRQL == DISPATCH_LEVEL
//
//	NOTES:
//
///////////////////////////////////////////////////////////////////////////////
UCHAR OsrVmIoControl(IN POSR_DEVICE_EXTENSION PDevExt,
				     IN PSCSI_REQUEST_BLOCK  PSrb)
{
    POSR_LU_EXTENSION luExt;
    PSRB_IO_CONTROL  srbIoControl = (PSRB_IO_CONTROL) PSrb->DataBuffer;
    UCHAR            srbStatus = SRB_STATUS_INVALID_REQUEST;

	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,
			("OsrVmIoControl Entered\n"));

	OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_IOCTL_INFO,
		(__FUNCTION__" HeaderLength:0x%d \n\tSignature:%c%c%c%c%c%c%c%c\n\t"
		"Timeout:0x%x\n\tControlCode:0x%x\n\tReturnCode:0x%x\n\tLength:0x%x\n",
		srbIoControl->HeaderLength,srbIoControl->Signature[0],srbIoControl->Signature[1],
		srbIoControl->Signature[2],srbIoControl->Signature[3],srbIoControl->Signature[4],
		srbIoControl->Signature[5],srbIoControl->Signature[6],srbIoControl->Signature[7],
		srbIoControl->Timeout,srbIoControl->ControlCode,srbIoControl->ReturnCode,
		srbIoControl->Length));

    luExt = (POSR_LU_EXTENSION) StorPortGetLogicalUnit(PDevExt,
                                   PSrb->PathId,
                                   PSrb->TargetId,
                                   PSrb->Lun );

    if ((NULL==luExt) ) {
        OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_SRB,
			("OsrVmIoControl: Unable to get LU extension for device %d:%d:%d\n",
                    PSrb->PathId, PSrb->TargetId, PSrb->Lun));
        return SRB_STATUS_NO_DEVICE;
    }

	
	OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
	  ("OsrVmIoControl unknown command: 0x%x, pSrb = 0x%p\n", 
	  srbIoControl->ControlCode, PSrb));


	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,
			("OsrVmIoControl Exit\n"));

    return srbStatus;
}
