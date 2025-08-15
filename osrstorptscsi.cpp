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
//        $File: osrstorpt\osrstorptscsi.cpp $
//
//    ABSTRACT:
//
//      This contains the Osr Virtual Miniport Driver Scsi processing
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
#include "osrstorpt.h"



///////////////////////////////////////////////////////////////////////////////
//
//	OsrVmExecuteScsi
//	
//		This routine is called to handle an input SRB_FUNCTION_EXECUTE_SCSI
//		request.   This function must process the SCSI OPCODE in the SRB
//		and do the correct processing.
//
//	INPUTS:
//
//		PDevExt - pointer to our non-paged storage area
//		PSCSI_REQUEST_BLOCK - adapter-control operation
//		Parameters - parameters related to control type
//
//	OUTPUTS:
//
//		PComplete - set to TRUE if SRB operations is completed
//			in this function.  Otherwise it is assumed that the
//			operation is not complete and will complete later.
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
UCHAR OsrVmExecuteScsi(IN POSR_DEVICE_EXTENSION PDevExt,
					   IN PSCSI_REQUEST_BLOCK PSrb,
					   IN PBOOLEAN PComplete)
{
    POSR_LU_EXTENSION    luExt;
    UCHAR               srbStatus = SRB_STATUS_INVALID_REQUEST;
	NTSTATUS			status;
    PCDB				pCdb = (PCDB) &PSrb->Cdb;
	POSR_VM_DEVICE		pOsrDevice;

	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,
			("OsrVmExecuteScsi Entered\n"));

   *PComplete = TRUE;

    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		(__FUNCTION__": Receiving %s command. pSrb = 0x%p, CDB = 0x%x Path: %x TID: %x Lun: %x\n",
		(const char*) OsrSpPrintSCSICDBOperation(pCdb->CDB6READWRITE.OperationCode),
		PSrb, PSrb->Cdb[0], PSrb->PathId, PSrb->TargetId, PSrb->Lun));

    luExt = (POSR_LU_EXTENSION) StorPortGetLogicalUnit(PDevExt,
                                   PSrb->PathId,
                                   PSrb->TargetId,
                                   PSrb->Lun );

	if(!luExt) {
        OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_SRB,
			(__FUNCTION__": Unable to get LUN extension for device %d:%d:%d\n",
                   PSrb->PathId, PSrb->TargetId, PSrb->Lun));
        return SRB_STATUS_NO_DEVICE;
	}

    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB,
		("OsrUserHandleSrb: Processing %s command.\n",
		(const char*) OsrSpPrintSCSICDBOperation(pCdb->CDB6READWRITE.OperationCode)));


	pOsrDevice = FindOsrVmDevice(luExt,PDevExt,PSrb->PathId, PSrb->TargetId, PSrb->Lun,FALSE);

	if(pOsrDevice && pOsrDevice->PUserLocalInformation) {

		InterlockedIncrement(&pOsrDevice->OutstandingIoCount);

		status = OsrUserHandleSrb(pOsrDevice->PUserLocalInformation,PSrb);

		if(status == STATUS_PENDING) {
			*PComplete = FALSE;
			srbStatus = SRB_STATUS_PENDING;
		} else {
			InterlockedDecrement(&pOsrDevice->OutstandingIoCount);
			srbStatus = PSrb->SrbStatus;
		}

	} else {
        OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_SRB,
			("No PUserLocalInformation extension for device %d:%d:%d\n",
                   PSrb->PathId, PSrb->TargetId, PSrb->Lun));
		srbStatus = SRB_STATUS_NO_DEVICE;
	}

	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,
			("OsrVmExecuteScsi Exit\n"));

    return srbStatus;
}
