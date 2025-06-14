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
//        $File: osrstorpt\osrstorptdevice.cpp $
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
POSR_VM_DEVICE FindOsrVmDevice(IN POSR_LU_EXTENSION LuExt,
							   IN POSR_DEVICE_EXTENSION PDevExt,
							   IN UCHAR PathId,
							   IN UCHAR TargetId,
							   IN UCHAR Lun,
							   IN BOOLEAN ReturnMissing)
{
	KIRQL	lockHandle;
	POSR_VM_DEVICE pDevice = NULL;

	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,(__FUNCTION__": Entered\n"));

	OsrAcquireSpinLock(&PDevExt->DeviceListLock,&lockHandle);

	for(PLIST_ENTRY pEntry = PDevExt->DeviceList.Flink;
		pEntry != &PDevExt->DeviceList; pEntry = pEntry->Flink) {

		pDevice = (POSR_VM_DEVICE) CONTAINING_RECORD(pEntry,OSR_VM_DEVICE,ListEntry);

		OSR_VM_DEVICE_VALID(pDevice);

		if(pDevice->PathId == PathId && pDevice->TargetId == TargetId &&
			pDevice->Lun == Lun) {
			if(!pDevice->Missing) {
				if(LuExt && !LuExt->OsrVmDevice) {
					LuExt->OsrVmDevice = pDevice;
				}
			} else if(!ReturnMissing) {
				if(!pDevice->ReportedMissing) {
					OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_PNP_INFO,
						(__FUNCTION__": %p Reported Missing, signaling DeleteDevices Thread\n",
						pDevice));
					pDevice->ReportedMissing = TRUE;
					KeSetEvent(&PDevExt->DeleteDevicesThreadWorkEvent,8,FALSE);
				}
				pDevice = NULL;
			}
			break;
		}

		pDevice = NULL;
		
	}
	OsrReleaseSpinLock(&PDevExt->DeviceListLock,lockHandle);

	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,(__FUNCTION__": Exit\n"));

	return pDevice;
}