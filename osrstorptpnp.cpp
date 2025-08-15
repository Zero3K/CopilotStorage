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
//        $File OsrVmPnp.cpp $
//
//    ABSTRACT:
//
//      This contains the Osr Virtual Miniport Driver SRB PNP processing
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


NTSTATUS OsrVmHandleRemoveDevice(IN POSR_DEVICE_EXTENSION PDevExt,
                     IN PSCSI_PNP_REQUEST_BLOCK  PSrb)
{
    NTSTATUS                  status = STATUS_SUCCESS;
    PSTOR_DEVICE_CAPABILITIES pStorageCapabilities = (PSTOR_DEVICE_CAPABILITIES)PSrb->DataBuffer;

    PSrb->SrbStatus = SRB_STATUS_SUCCESS;

    return status;
}

NTSTATUS OsrVmHandleQueryCapabilities(IN POSR_DEVICE_EXTENSION PDevExt,
									  IN PSCSI_PNP_REQUEST_BLOCK PSrb)
{
    NTSTATUS                  status = STATUS_SUCCESS;
    PSTOR_DEVICE_CAPABILITIES pStorageCapabilities = (PSTOR_DEVICE_CAPABILITIES)PSrb->DataBuffer;

    RtlZeroMemory(pStorageCapabilities, PSrb->DataTransferLength);

    // The next statements are taken from iScsiPrt's RaUnitQueryCapabilitiesIrp .

    pStorageCapabilities->Removable = FALSE;
    pStorageCapabilities->SurpriseRemovalOK = FALSE;

    PSrb->SrbStatus = SRB_STATUS_SUCCESS;

    return status;
}


///////////////////////////////////////////////////////////////////////////////
//
//	OsrVmHwHandlePnP
//	
//		This routine is called to handle an input SRB Pnp operation
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
NTSTATUS OsrVmHwHandlePnP(IN POSR_DEVICE_EXTENSION PDevExt,
						  IN PSCSI_PNP_REQUEST_BLOCK PSrb)
{
    NTSTATUS status = STATUS_SUCCESS;

	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,
			("OsrVmHwHandlePnP Entered\n"));

    switch(PSrb->PnPAction) {

		case StorStartDevice:
			OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_PNP_INFO,
				("OsrVmHwHandlePnP StorStartDevice\n"));
			PSrb->SrbStatus = SRB_STATUS_BAD_FUNCTION;
			status = STATUS_UNSUCCESSFUL;
			break;

		case StorRemoveDevice:
			OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_PNP_INFO,
				("OsrVmHwHandlePnP StorRemoveDevice\n"));
			status = OsrVmHandleRemoveDevice(PDevExt, PSrb);
			break;

		case StorStopDevice:
			OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_PNP_INFO,
				("OsrVmHwHandlePnP StorStopDevice\n"));
			PSrb->SrbStatus = SRB_STATUS_BAD_FUNCTION;
			status = STATUS_UNSUCCESSFUL;
			break;

		case StorQueryCapabilities:
			OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_PNP_INFO,
				("OsrVmHwHandlePnP StorQueryCapabilities\n"));
			status = OsrVmHandleQueryCapabilities(PDevExt, PSrb);
			break;

		case StorFilterResourceRequirements:
			OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_PNP_INFO,
				("OsrVmHwHandlePnP StorFilterResourceRequirements\n"));
			PSrb->SrbStatus = SRB_STATUS_BAD_FUNCTION;
			status = STATUS_UNSUCCESSFUL;
			break;

		default:
			OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_PNP_INFO,
				("OsrVmHwHandlePnP unknown Pnp Event 0x%x\n",PSrb->PnPAction));
			PSrb->SrbStatus = SRB_STATUS_BAD_FUNCTION;
			status = STATUS_UNSUCCESSFUL;
			break;
    }

    if(!NT_SUCCESS(status)) {
		OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_PNP_INFO,
			("OsrVmHwHandlePnP Pnp Error status:0x%x\n",status));
		ASSERT(FALSE);
    }

	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,
			("OsrVmHwHandlePnP Exit\n"));

    return status;
}
