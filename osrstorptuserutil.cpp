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
//        $File: osrstorpt\osrstorptuserutil.cpp $
//
//    ABSTRACT:
//
//      This contains the Osr Virtual Miniport Driver user interface code
//		which interfaces the user implementation of scsi devices to
//		the OSR Virtual Miniport which implements the adapter.
//
//    AUTHOR:
//
//        OSR Open Systems Resources, Inc.
// 
//    REVISION:   
//
//        $Revision: #3 $
//
///////////////////////////////////////////////////////////////////////////////
#include "ustorptimpl.h"

extern "C" {
  #include <initguid.h>
  #include <wdmguid.h>
  #include <ioevent.h>
};


ULONG AssignedScsiIds[((SCSI_MAXIMUM_TARGETS_PER_BUS/8)/sizeof(ULONG))*SCSI_MAXIMUM_BUSES];
extern RTL_BITMAP      ScsiBitMapHeader = {0};
static ULONG LunId    = 0;

void Get_CRC_CheckSum(PVOID pBuffer, ULONG ulSize, PULONG pulSeed);
void DoClose(PUSER_GLOBAL_INFORMATION PGInfo,PCONNECTION_LIST_ENTRY PEntry);

WORKER_THREAD_ROUTINE DoWorkThreadStart;
IO_COMPLETION_ROUTINE DiscardIrpCompletion;

NTSTATUS DoRead(PCONNECTION_LIST_ENTRY PEntry,
            PMDL BufferMdl,
            ULARGE_INTEGER ByteOffset,
            ULONG Length,
            PULONG PBytesRead,
            BOOLEAN BNonCached);
NTSTATUS DoWrite(PCONNECTION_LIST_ENTRY PEntry,
                            PMDL BufferMdl,
                            ULARGE_INTEGER ByteOffset,
                            ULONG Length,
                            PULONG PBytesWritten,
                            BOOLEAN BNonCached);

typedef struct _REMOVE_WORK_ITEM {
    PIO_WORKITEM PItem;
    PUSER_INSTANCE_INFORMATION PLocalInfo;
} REMOVE_WORK_ITEM, *PREMOVE_WORK_ITEM;


//
// CRC 16 Information.
//

USHORT wCRC16a[16]={
    0000000,    0140301,    0140601,    0000500,
    0141401,    0001700,    0001200,    0141101,
    0143001,    0003300,    0003600,    0143501,
    0002400,    0142701,    0142201,    0002100,
};
USHORT wCRC16b[16]={
    0000000,    0146001,    0154001,    0012000,
    0170001,    0036000,    0024000,    0162001,
    0120001,    0066000,    0074000,    0132001,
    0050000,    0116001,    0104001,    0043000,
};

extern INQUIRY_DEVICE_TYPE DeviceTypeInfo[] = {
    {"Disk",        "GenDisk",      L"DiskPeripheral",              TRUE},
    {"Sequential",  "",             L"TapePeripheral",              TRUE},
    {"Printer",     "GenPrinter",   L"PrinterPeripheral",           FALSE},
    {"Processor",   "",             L"OtherPeripheral",             FALSE},
    {"Worm",        "GenWorm",      L"WormPeripheral",              TRUE},
    {"CdRom",       "GenCdRom",     L"CdRomPeripheral",             TRUE},
    {"Scanner",     "GenScanner",   L"ScannerPeripheral",           FALSE},
    {"Optical",     "GenOptical",   L"OpticalDiskPeripheral",       TRUE},
    {"Changer",     "ScsiChanger",  L"MediumChangerPeripheral",     TRUE},
    {"Net",         "ScsiNet",      L"CommunicationsPeripheral",    FALSE},
    {"Other",       "ScsiOther",    L"OtherPeripheral",             FALSE}
};

#define NUM_DEVICE_TYPE_INFO_ENTRIES sizeof(DeviceTypeInfo)/sizeof(INQUIRY_DEVICE_TYPE) 


///////////////////////////////////////////////////////////////////////////////
//
//  CreateConnection
//
//      Creates a connection to the specified volume, if it does not already
//      exists.
//
//  INPUTS:
//
//      PGInfo - Pointer to the Global Information BLock.
//
//      PConnectInfo - Pointer to the connection information to create
//
//  OUTPUTS:
//
//      None.
//
//  RETURNS:
//
//      STATUS_SUCCESS if okay, an error otherwise.
//
//  IRQL:
//
//    This routine is called at any IRQL PASSIVE_LEVEL.
//
//  NOTES:
//
///////////////////////////////////////////////////////////////////////////////
NTSTATUS CreateConnection(PUSER_GLOBAL_INFORMATION PGInfo, PCONNECT_IN PConnectInfo)
{
    NTSTATUS            status = STATUS_UNSUCCESSFUL;
    IO_STATUS_BLOCK     ioStatus;
    BOOLEAN             bInserted = FALSE;
	OBJECT_ATTRIBUTES   objectAttributes;
    UNICODE_STRING      uString;
    KIRQL               oldIrql;
    GUID                tmpGuid;
    ULONG               bytesReturned;

	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,(__FUNCTION__": Enter\n"));

	//
	// See if we already have a connection that matches this.
	//
    if(FindConnectionMatch(PGInfo,PConnectInfo,NULL)) {
        return STATUS_OBJECT_NAME_COLLISION;
    }

    RtlZeroMemory(&tmpGuid,sizeof(GUID));
    
    //
    // Add the connection to the list.
    //
    PCONNECTION_LIST_ENTRY pEntry = (PCONNECTION_LIST_ENTRY) 
		ExAllocatePoolWithTag(NonPagedPool,sizeof(CONNECTION_LIST_ENTRY),'pCLE');

    if(!pEntry) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(pEntry,sizeof(CONNECTION_LIST_ENTRY));

    RtlCopyMemory(&pEntry->ConnectionInfo,PConnectInfo,sizeof(CONNECT_IN));

    OsrAcquireSpinLock(&PGInfo->ConnectionListLock,&oldIrql);

    InsertTailList(&PGInfo->ConnectionList,&pEntry->ListEntry);

    OsrReleaseSpinLock(&PGInfo->ConnectionListLock,oldIrql);
    bInserted = TRUE;

    //
    // Now we have to allocate memory to represent the disk.   The caller has
    // supplied the size in MegaBytes of the disk. So we'll allocate it here.
    //
    pEntry->DiskSize = (ULONG) ((ULONG) PConnectInfo->DiskSizeMB) * 1024 * 1024;
    pEntry->DiskBaseAddress = ExAllocatePoolWithTag(NonPagedPool,pEntry->DiskSize,'ksiD');

    if(!pEntry->DiskBaseAddress) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto cleanupAfterError;
    }

    RtlZeroMemory(pEntry->DiskBaseAddress,pEntry->DiskSize);

    //
    // For our Virtual Disks, it comes from the Disk Header.
    //
    status = ExUuidCreate(&tmpGuid);

    if(!NT_SUCCESS(status)) {
        goto cleanupAfterError;
    }

    //
    // We now have the information about the file that this disk, which we are about to
    // create, represents.   We need to build some SCSI inquiry information about the
    // disk, so that the Disk Class Driver knows about us.
    //
#pragma prefast(suppress:28197,"This memory is not leaked")
    PINQUIRYDATA pInquiryData = (PINQUIRYDATA) ExAllocatePoolWithTag(NonPagedPool,
													sizeof(INQUIRYDATA),
                                                    'diSO');

    if(pInquiryData) {

        // typedef struct _INQUIRYDATA {
        //     UCHAR DeviceType : 5;
        //     UCHAR DeviceTypeQualifier : 3;
        //     UCHAR DeviceTypeModifier : 7;
        //     UCHAR RemovableMedia : 1;
        //     UCHAR Versions;
        //     UCHAR ResponseDataFormat : 4;
        //     UCHAR HiSupport : 1;
        //     UCHAR NormACA : 1;
        //     UCHAR ReservedBit : 1;
        //     UCHAR AERC : 1;
        //     UCHAR AdditionalLength;
        //     UCHAR Reserved[2];
        //     UCHAR SoftReset : 1;
        //     UCHAR CommandQueue : 1;
        //     UCHAR Reserved2 : 1;
        //     UCHAR LinkedCommands : 1;
        //     UCHAR Synchronous : 1;
        //     UCHAR Wide16Bit : 1;
        //     UCHAR Wide32Bit : 1;
        //     UCHAR RelativeAddressing : 1;
        //     UCHAR VendorId[8];
        //     UCHAR ProductId[16];
        //     UCHAR ProductRevisionLevel[4];
        //     UCHAR VendorSpecific[20];
        //     UCHAR Reserved3[40];
        // } INQUIRYDATA, *PINQUIRYDATA;

        RtlZeroMemory(pInquiryData,sizeof(INQUIRYDATA));

        //
        // The media is now either an OSR Disk or a regular disk, either way
        // we return the same information.
        //
        pInquiryData->DeviceType = DIRECT_ACCESS_DEVICE;
        pInquiryData->DeviceTypeQualifier = DEVICE_CONNECTED;
        pInquiryData->DeviceTypeModifier = 0;
        pInquiryData->RemovableMedia = TRUE;
        pInquiryData->Versions = 2;             // SCSI-2 support
        pInquiryData->ResponseDataFormat = 2;   // Same as Version?? according to SCSI book
        pInquiryData->Wide32Bit = TRUE;         // 32 bit wide transfers
        pInquiryData->Synchronous = TRUE;       // Synchronous commands
        pInquiryData->CommandQueue = FALSE;     // Does not support tagged commands
        pInquiryData->AdditionalLength = INQUIRYDATABUFFERSIZE-5;  // Amount of data we are returning
        pInquiryData->LinkedCommands = FALSE;   // No Linked Commands
        RtlCopyMemory((PUCHAR) &pInquiryData->VendorId[0],OSR_INQUIRY_VENDOR_ID,
									strlen(OSR_INQUIRY_VENDOR_ID));
        RtlCopyMemory((PUCHAR) &pInquiryData->ProductId[0],OSR_INQUIRY_PRODUCT_ID,
									strlen(OSR_INQUIRY_PRODUCT_ID));
        RtlCopyMemory((PUCHAR) &pInquiryData->ProductRevisionLevel[0],OSR_INQUIRY_PRODUCT_REVISION,
                            strlen(OSR_INQUIRY_PRODUCT_REVISION));
        RtlCopyMemory((PUCHAR) &pInquiryData->VendorSpecific[0],OSR_INQUIRY_VENDOR_SPECIFIC,
                            strlen(OSR_INQUIRY_VENDOR_SPECIFIC));


        ULONG bitNumber = RtlFindClearBitsAndSet(&ScsiBitMapHeader,1,0);

        if(bitNumber == 0xFFFFFFFF) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            DoClose(PGInfo,pEntry);
            goto cleanupAfterError;
        }

        ULONG targetId = bitNumber % SCSI_MAXIMUM_TARGETS_PER_BUS;
        ULONG BusId = bitNumber / SCSI_MAXIMUM_BUSES;

#pragma prefast(suppress:28197,"This memory is not leaked")
        PUSER_INSTANCE_INFORMATION pLocalInfo = (PUSER_INSTANCE_INFORMATION) 
													ExAllocatePoolWithTag(NonPagedPool,
                                                    sizeof(USER_INSTANCE_INFORMATION),
                                                    'DLUp');

        if(!pLocalInfo) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            DoClose(PGInfo,pEntry);
            goto cleanupAfterError;
        }

        RtlZeroMemory(pLocalInfo,sizeof(USER_INSTANCE_INFORMATION));
        pLocalInfo->MagicNumber = USER_INSTANCE_INFORMATION_MAGIC_NUMBER;
        pLocalInfo->PInquiryData = pInquiryData;

        //
        // Create a PDO for this new disk.
        //

        pLocalInfo->OsrSPLocalHandle = OsrSPCreateScsiDevice(PGInfo->OsrSPHandle,
                                             BusId /*IN ULONG BusIndex*/,
                                             targetId /*IN ULONG TargetIndex*/, 
                                             LunId /*IN ULONG LunIndex*/,
                                             pLocalInfo, /* Our local Data for Device */
                                             FALSE,
                                             pInquiryData,
                                             1);

        //
        // Okay, we've got a PDO, we can now invalidate relations and see what happens.
        //

        if(pLocalInfo) {

            static ULONG indexNumber = 0x08051958;

            pLocalInfo->PGInfo = PGInfo;

			pLocalInfo->StorageType = OsrDisk;

            //
            // Get the infor for the unique ID.
            //
            GUID* pUniqueId = &tmpGuid;

            RtlCopyMemory(&pLocalInfo->UniqueID.UniqueID,pUniqueId,sizeof(GUID));
            pLocalInfo->UniqueID.FileId = (ULONGLONG) InterlockedIncrement((volatile LONG*) &indexNumber);

            //
            // Store away some other useful information.
            //
            pLocalInfo->ConnectionInformation = pEntry;
            pLocalInfo->TargetIndex = targetId;
            pLocalInfo->BusIndex = BusId;
            pLocalInfo->LunIndex = LunId;
			if(STATUS_SUCCESS != RtlStringCbPrintfA(&pLocalInfo->AsciiSignature[0],
                sizeof(pLocalInfo->AsciiSignature),
				"%08x%04x%04x%2x%2x%02x%02x%02x%02x%02x%02x%0I64x", 
                pUniqueId->Data1,pUniqueId->Data2,pUniqueId->Data3,
                pUniqueId->Data4[0],pUniqueId->Data4[1],pUniqueId->Data4[2],pUniqueId->Data4[3],
                pUniqueId->Data4[5],pUniqueId->Data4[5],pUniqueId->Data4[6],pUniqueId->Data4[7],
                pLocalInfo->UniqueID.FileId)) {
				status = STATUS_INSUFFICIENT_RESOURCES;
				DoClose(PGInfo,pEntry);
				goto cleanupAfterError;
			}

            pEntry->PIInfo = pLocalInfo;
            pEntry->BusIndex = BusId;
            pEntry->TargetIndex = targetId;
            pEntry->LunIndex = LunId;

            InterlockedIncrement(&PGInfo->ConnectionCount);

            targetId++;

            //
            // Tell the OSR SP that our bus has changed.
            //
            OsrSPAnnounceArrival(PGInfo->OsrSPHandle);

			pEntry->Connected = TRUE;

            status = STATUS_SUCCESS;

        }

	    OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,(__FUNCTION__": Exit\n"));

	    return status;
    } else {
        status = STATUS_INSUFFICIENT_RESOURCES;
    }

cleanupAfterError:

    if(bInserted) {

        DeleteConnectionEntry(PGInfo,pEntry,PConnectInfo);

    }

    if(pEntry) {

        ExFreePool(pEntry);

    }

	OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_FUNCTRACE,(__FUNCTION__": Exit\n"));

    return status;
}


///////////////////////////////////////////////////////////////////////////////
//
//  EnumerateActiveConnections
//
//      Enumerates active connections
//
//  INPUTS:
//
//      DeviceObject - Pointer to the device object.
//
//      Irp          - Pointer to the irp.
//
//  OUTPUTS:
//
//      None.
//
//  RETURNS:
//
//      STATUS_SUCCESS if okay, an error otherwise.
//
//  IRQL:
//
//    This routine is called at any IRQL PASSIVE_LEVEL.
//
//  NOTES:
//
///////////////////////////////////////////////////////////////////////////////
NTSTATUS EnumerateActiveConnections(PUSER_GLOBAL_INFORMATION PGInfo,PIRP Irp)
{
    KIRQL		lockHandle;
    PCONNECTION_LIST_ENTRY  pEntry;
    PIO_STACK_LOCATION      irpStack = IoGetCurrentIrpStackLocation(Irp);
    PGETACTIVELIST_OUT      pActiveList = (PGETACTIVELIST_OUT) Irp->AssociatedIrp.SystemBuffer;
    PACTIVELIST_ENTRY_OUT   pNextEntry = &pActiveList->ActiveEntry[0];
    ULONG                   count = 0;
    ULONG                   countRemaining;
    NTSTATUS                status = STATUS_BUFFER_OVERFLOW;

    pActiveList->ActiveListCount = 0;

    //
    // Determine how many entries can be kept in the list.
    //

    countRemaining = (irpStack->Parameters.DeviceIoControl.OutputBufferLength - 
        sizeof(ULONG))/sizeof(ACTIVELIST_ENTRY_OUT);


    //
    // Loop through the list of connections already established and see if we have
    // already connected to this file.
    //

    OsrAcquireSpinLock(&PGInfo->ConnectionListLock,&lockHandle);

    pEntry = (PCONNECTION_LIST_ENTRY) PGInfo->ConnectionList.Flink;

    while((pEntry != (PCONNECTION_LIST_ENTRY) &PGInfo->ConnectionList.Flink) && countRemaining) {

        pNextEntry = &pActiveList->ActiveEntry[pActiveList->ActiveListCount];
		RtlZeroMemory(pNextEntry,sizeof(ACTIVELIST_ENTRY_OUT));
        RtlCopyMemory(pNextEntry,&pEntry->ConnectionInfo,sizeof(CONNECT_IN));

        pNextEntry->BusNumber = (USHORT) pEntry->BusIndex;
        pNextEntry->TargetId = (USHORT) pEntry->TargetIndex;
        pNextEntry->Lun = (USHORT) pEntry->LunIndex;
        pNextEntry->Connected = pEntry->Connected;
        pNextEntry->DiskSizeMB = pEntry->ConnectionInfo.DiskSizeMB;

		OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_USER_CONNECTION,
			(__FUNCTION__": %d:%d:%d Disk:%d Connected:%d\n",
			pEntry->BusIndex,pEntry->TargetIndex,pEntry->LunIndex,
			TRUE,
			pNextEntry->Connected));

        pActiveList->ActiveListCount++;
        countRemaining--;

        //
        // Move to the next and continue matching
        //

        pEntry = (PCONNECTION_LIST_ENTRY) pEntry->ListEntry.Flink;

    }

    //
    // Determine if we reached the end of the list.   If we did not, then we
    // must have overflowed the buffer.
    //

    if(pEntry == (PCONNECTION_LIST_ENTRY) &PGInfo->ConnectionList.Flink) {

        status = STATUS_SUCCESS;

    }

    Irp->IoStatus.Information = (pActiveList->ActiveListCount * sizeof(ACTIVELIST_ENTRY_OUT)) + sizeof(ULONG);

    OsrReleaseSpinLock(&PGInfo->ConnectionListLock,lockHandle);

	return status;
}


///////////////////////////////////////////////////////////////////////////////
//
//  FindConnectionMatch
//
//      Seaches the active connection list to see if a connection to the
//      specified volume already exists....
//
//  INPUTS:
//
//      DeviceObject - Pointer to the device object.
//
//      PConnectInfo - Pointer to the connection information to match
//
//  OUTPUTS:
//
//      PPFoundEntry - if specified, the found entry is returned.
//
//  RETURNS:
//
//      STATUS_SUCCESS if okay, an error otherwise.
//
//  IRQL:
//
//    This routine is called at any IRQL PASSIVE_LEVEL.
//
//  NOTES:
//
///////////////////////////////////////////////////////////////////////////////
BOOLEAN FindConnectionMatch(PUSER_GLOBAL_INFORMATION PGInfo, PCONNECT_IN PConnectInfo,
                            PCONNECTION_LIST_ENTRY* PPFoundEntry)
{
    KIRQL		lockHandle;
    BOOLEAN                 found = FALSE;
    PCONNECTION_LIST_ENTRY  pEntry;

    pEntry = (PCONNECTION_LIST_ENTRY) PGInfo->ConnectionList.Flink;

    //
    // Loop through the list of connections already established and see if we have
    // already connected to this file.
    //

    OsrAcquireSpinLock(&PGInfo->ConnectionListLock,&lockHandle);

    while(pEntry != (PCONNECTION_LIST_ENTRY) &PGInfo->ConnectionList.Flink) {

		//
		// See if we are looking for service.   If we are, then only process
		// connnections that are services.
		//
		if((!wcscmp((const PWCHAR) &pEntry->ConnectionInfo.InstanceName,
            (const PWCHAR) &PConnectInfo->InstanceName))) {

			//
			// We have found the entry, notify the caller and exit the loop.
			//

			if(PPFoundEntry) {

				*PPFoundEntry = pEntry;

			}

			found = TRUE;
			break;

		} 
        //
        // This entry did not match, move to the next and continue matching
        //

        pEntry = (PCONNECTION_LIST_ENTRY) pEntry->ListEntry.Flink;
        continue;

    }

    OsrReleaseSpinLock(&PGInfo->ConnectionListLock,lockHandle);

	return found;
}


///////////////////////////////////////////////////////////////////////////////
//
//  DeleteConnectionEntry
//
//      Seaches the active connection list to see if a connection to the
//      specified volume already exists and then deletes it.
//
//  INPUTS:
//
//      DeviceObject - Pointer to the device object.
//		PServiceEntry - if not null the service entry
//      PConnectInfo - Pointer to the connection information to match
//
//  OUTPUTS:
//
//      None.
//
//  RETURNS:
//
//      STATUS_SUCCESS if okay, an error otherwise.
//
//  IRQL:
//
//    This routine is called at any IRQL PASSIVE_LEVEL.
//
//  NOTES:
//
///////////////////////////////////////////////////////////////////////////////
NTSTATUS DeleteConnectionEntry(PUSER_GLOBAL_INFORMATION PGInfo,PCONNECTION_LIST_ENTRY PCle,
							   PCONNECT_IN PConnectInfo)
{
    KIRQL		lockHandle;
    NTSTATUS                status = STATUS_OBJECT_NAME_NOT_FOUND;
    BOOLEAN                 found = FALSE;
    PCONNECTION_LIST_ENTRY  pEntry;

    OsrAcquireSpinLock(&PGInfo->ConnectionListLock,&lockHandle);

    RemoveEntryList(&PCle->ListEntry);
    status = STATUS_SUCCESS;

    OsrReleaseSpinLock(&PGInfo->ConnectionListLock,lockHandle);

	return status;
}


///////////////////////////////////////////////////////////////////////////////
//
//  InitializeScsiIds
//
//      Initialize the Table used to assign SCSI Ids to devices.
//
//  INPUTS:
//
//      NONE.
//
//  OUTPUTS:
//
//      NONE.
//
//  RETURNS:
//
//      NONE.
//
//  IRQL:
//
//    This routine is called at PASSIVE_LEVEL.
//
//  NOTES:
//
///////////////////////////////////////////////////////////////////////////////
VOID InitializeScsiIds()
{
    RtlZeroMemory(AssignedScsiIds,sizeof(AssignedScsiIds));
    RtlInitializeBitMap(&ScsiBitMapHeader,AssignedScsiIds,SCSI_MAXIMUM_TARGETS_PER_BUS*SCSI_MAXIMUM_BUSES);
}


///////////////////////////////////////////////////////////////////////////////
//
//  UpdateConnectionListInRegistry
//
//      Updates the Connection Information In Registry
//
//  INPUTS:
//
//      DeviceObject - Pointer to the device object.
//
//      pDisconnectInfo - information about connection to delete.
//
//  OUTPUTS:
//
//      None.
//
//  RETURNS:
//
//      STATUS_SUCCESS if okay, an error otherwise.
//
//  IRQL:
//
//    This routine is called at any IRQL <= DISPATCH_LEVEL.
//
//  NOTES:
//
///////////////////////////////////////////////////////////////////////////////
void UpdateConnectionListInRegistry(PUSER_GLOBAL_INFORMATION PGInfo)
{
    PCONNECT_IN             pConnectIn = NULL;
    KIRQL		lockHandle;
    BOOLEAN                 found = FALSE;
    PCONNECTION_LIST_ENTRY  pEntry;
    ULONG                   offset = 0;
    UNICODE_STRING          subPath;
	ULONG					nonServiceCount = 0;

    if(!Globals.AddConnectionsToRegistry) {

        return;

    }

    RtlInitUnicodeString(&subPath,L"\\Parameters");

    //
    // See if there are connections to add to the registry.
    //

    if(!PGInfo->ConnectionCount) {
		goto NoConnections;
	}

    OsrAcquireSpinLock(&PGInfo->ConnectionListLock,&lockHandle);
    pEntry = (PCONNECTION_LIST_ENTRY) PGInfo->ConnectionList.Flink;
    while(pEntry != (PCONNECTION_LIST_ENTRY) &PGInfo->ConnectionList.Flink) {
		nonServiceCount++;
        pEntry = (PCONNECTION_LIST_ENTRY) pEntry->ListEntry.Flink;
    }
    OsrReleaseSpinLock(&PGInfo->ConnectionListLock,lockHandle);

	if(nonServiceCount) {

        ULONG   size = nonServiceCount * sizeof(CONNECT_IN);

        pConnectIn = (PCONNECT_IN) ExAllocatePoolWithTag(NonPagedPool,size,'rbdg');

        if(!pConnectIn) {
            goto NoConnections;
        }

        RtlInitUnicodeString(&subPath,L"\\Parameters");

        //
        // Loop through the list of connections already established and see if we have
        // already connected to this file.
        //

        OsrAcquireSpinLock(&PGInfo->ConnectionListLock,&lockHandle);

        pEntry = (PCONNECTION_LIST_ENTRY) PGInfo->ConnectionList.Flink;

        while(pEntry != (PCONNECTION_LIST_ENTRY) &PGInfo->ConnectionList.Flink) {
            RtlCopyMemory(&pConnectIn[offset],&pEntry->ConnectionInfo,sizeof(CONNECT_IN));
            offset++;
            pEntry = (PCONNECTION_LIST_ENTRY) pEntry->ListEntry.Flink;
            continue;

        }

        OsrReleaseSpinLock(&PGInfo->ConnectionListLock,lockHandle);

        BOOLEAN exceeded = RegistryWriteSubValue(&OsrRegistryPath,&subPath,L"ConnectInfo", 
                                      REG_BINARY,(PUCHAR) pConnectIn,size);

        ExFreePool(pConnectIn);

        OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_USER_CONNECTION,
			("UpdateConnectionListInRegistry updated =  %x.\n",exceeded));
		

		return;

    } 

NoConnections:

    ULONG  empty = 0;

    BOOLEAN exceeded = RegistryWriteSubValue(&OsrRegistryPath,&subPath,L"ConnectInfo", 
                                  REG_BINARY,(PUCHAR) &empty,sizeof(empty));

}

void DeleteConnectionListInRegistry(PUSER_GLOBAL_INFORMATION PGInfo)
{
    ULONG           empty = 0;
    UNICODE_STRING  subPath;

    RtlInitUnicodeString(&subPath,L"\\Parameters");

    if(Globals.AddConnectionsToRegistry) {
        return;
    }

    BOOLEAN exceeded = RegistryWriteSubValue(&OsrRegistryPath,&subPath,L"ConnectInfo", 
                                      REG_BINARY,(PUCHAR) &empty,sizeof(empty));

    empty = (ULONG) Globals.AddConnectionsToRegistry;

    exceeded = RegistryWriteSubValue(&OsrRegistryPath,&subPath,L"ConnectionsInRegistry",
                                    REG_DWORD,(PUCHAR) (PUCHAR) &empty,sizeof(empty));
}


///////////////////////////////////////////////////////////////////////////////
//
//  DeleteConnection
//
//      Deletes a connection. 
//
//  INPUTS:
//
//      DeviceObject - Pointer to the device object.
//
//      pDisconnectInfo - information about connection to delete.
//
//  OUTPUTS:
//
//      None.
//
//  RETURNS:
//
//      STATUS_SUCCESS if okay, an error otherwise.
//
//  IRQL:
//
//    This routine is called at any IRQL <= DISPATCH_LEVEL.
//
//  NOTES:
//
///////////////////////////////////////////////////////////////////////////////
NTSTATUS DeleteConnection(PUSER_GLOBAL_INFORMATION PGInfo,PCONNECT_IN PDisconnectInfo)
{
    NTSTATUS                status = STATUS_UNSUCCESSFUL;
    PCONNECTION_LIST_ENTRY  pEntryToDelete;
    BOOLEAN                 bVolumeServerBeingUsed = FALSE;
    PUSER_INSTANCE_INFORMATION PIInfo;
    ULONG                   targetId = 0;
    ULONG                   busId = 0;

    OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,("DeleteConnection Entered\n"));

    if(!FindConnectionMatch(PGInfo,PDisconnectInfo,&pEntryToDelete)) {

        OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_USER_CONNECTION,
			("DeleteConnection: Could not find connection to delete\n"));
        return STATUS_OBJECT_NAME_NOT_FOUND;

    }

    PIInfo = pEntryToDelete->PIInfo;

    OSRASSERT(PIInfo);

    if(PIInfo) {

        targetId = PIInfo->TargetIndex;
        busId = PIInfo->BusIndex;

        if(!PIInfo->ConnectionInformation->ContainingMediaRemoved) {

            if(!OsrSPSetDeviceRemovable(PIInfo->OsrSPLocalHandle,FALSE)) {

                OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_USER_CONNECTION,
					("DeleteConnection: Could not delete media because it is still in use.\n"));
                return STATUS_UNABLE_TO_UNLOAD_MEDIA;

            }

            //
            // Tell the OSR SP that a Device has gone away.
            //

            OsrSPAnnounceDeparture(PGInfo->OsrSPHandle);

        } else {
            OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_USER_CONNECTION,
				("Underlying Media was forcably removed.\n"));
        }


    } else {

        OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_USER_CONNECTION,
			("DeleteConnection: PDO not found????\n"));
        KdBreakPoint();
        return STATUS_OBJECT_NAME_NOT_FOUND;

    }


    //
    // Release the handle to the fake volume file.
    //

    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_USER_CONNECTION,(
		"DeleteConnection: Releasing FIle Handle\n"));

	DoClose(PGInfo,pEntryToDelete);

    status = DeleteConnectionEntry(PGInfo,pEntryToDelete,PDisconnectInfo);

    if(!NT_SUCCESS(status)) {

        KdBreakPoint();
        OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_USER_CONNECTION,
			("DeleteConnection: Could Not Delete Connection Entry %x\n",status));

    }

    RtlClearBits(&ScsiBitMapHeader,targetId+(busId*SCSI_MAXIMUM_TARGETS_PER_BUS),1);

	InterlockedDecrement(&PGInfo->ConnectionCount);

    OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,("DeleteConnection Exit\n"));

    return status;
}


///////////////////////////////////////////////////////////////////////////////
//
//  RegistryReadSubValue
//
//       Read a DWORD from the registry, using a registry path, and
//       a subpath.
//
//  INPUTS:
//
//      RegistryPath - Pointer to a UNICODE String representing the 
//                     path to the key to be interrogated.  For example,
//					
//          "HKEY_LOCAL_MACHINE\\System\\CurrentControlSet\\Services\\Disk".
//
//      SubKey - Pointer to a UNICODE String representing the sub path to be
//               interrogated. For example, "\\Parameters".
//
//      Key - Pointer to a string representing the DWORD value to be read.  
//            For Example, "Count".
//
//      Type - Type of data to be read.  The size of the value must be <= REG_DWORD.
//
//      Value - address of a location to receive the read value.
//
//  OUTPUTS:
//
//      Value - if successful, Value will contain the contents of the read registry
//              key.
//
//  RETURNS:
//
//      TRUE, if read, FALSE otherwise.
//
//  IRQL REQUIREMENTS:
//
//      IRQL < DISPATCH_LEVEL
//  
//  NOTES:
//
///////////////////////////////////////////////////////////////////////////////
BOOLEAN RegistryReadSubValue(PUNICODE_STRING RegistryPath, PUNICODE_STRING SubPath, 
                                PWSTR Key, ULONG Type, PVOID Value)
{
    UNICODE_STRING path;
    BOOLEAN code;

    //
    // Initialize our path string and copy the input data into it.
    //

    path.MaximumLength = RegistryPath->Length + SubPath->Length;
    path.Length = 0;
    path.Buffer = (PWSTR) ExAllocatePoolWithTag(NonPagedPool,path.MaximumLength,'htaP');

    if(!path.Buffer) {

        return FALSE;

    }


    RtlMoveMemory(path.Buffer,RegistryPath->Buffer,RegistryPath->Length);
    path.Length = RegistryPath->Length;
    RtlMoveMemory(path.Buffer+(RegistryPath->Length/sizeof(WCHAR)), SubPath->Buffer, SubPath->Length);
    path.Length = path.Length + SubPath->Length;

    //
    // Read the Value from the registry.
    //

    code = RegistryReadValue(&path, Key, Type, Value);

    //
    // Delete our internally generated string.
    //

    ExFreePool(path.Buffer);

    //
    // Tell them the results of our efforts.
    //

    return (code); 
}


///////////////////////////////////////////////////////////////////////////////
//
//  RegistryReadValue
//
//      Read a DWORD from the registry, using an absolute path.  Never
//      a default with this call. 
//
//  INPUTS:
//
//      RegistryPath - Pointer to a UNICODE String representing the 
//                     absolute path to the key to be interrogated.  For example,
//					
//          "HKEY_LOCAL_MACHINE\\System\\CurrentControlSet\\Services\\Disk".
//
//      Key - Pointer to a string representing the DWORD value to be read.  
//            For Example, "Start".
//
//      Type - Type of data to be read.  The size of the value must be <= REG_DWORD.
//
//      Value - address of a location to receive the read value.
//
//  OUTPUTS:
//
//      Value - if successful, Value will contain the contents of the read registry
//              key.
//
//  RETURNS:
//
//      TRUE, if read, FALSE otherwise.
//
//  IRQL REQUIREMENTS:
//
//      IRQL < DISPATCH_LEVEL
//  
//  NOTES:
//
///////////////////////////////////////////////////////////////////////////////
BOOLEAN RegistryReadValue(PUNICODE_STRING RegistryPath, PWSTR Key, ULONG Type, PVOID Value)
{
    NTSTATUS code;
    RTL_QUERY_REGISTRY_TABLE paramTable[2];
    PWSTR path;

    //
    // Allocate memory for the expanded string, include enough space
    // for trailing NULL!
    //

    path = (PWSTR) ExAllocatePoolWithTag(NonPagedPool,RegistryPath->Length+sizeof(WCHAR),'htaP');

    if (!path) {

        return(FALSE);

    }

    RtlZeroMemory(paramTable,sizeof(paramTable));
    RtlZeroMemory(path,RegistryPath->Length+sizeof(WCHAR));
    RtlCopyMemory(path,RegistryPath->Buffer,RegistryPath->Length);

    paramTable[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
    paramTable[0].Name = Key;
    paramTable[0].EntryContext = Value;
    paramTable[0].DefaultType = Type;

    __try {

        code = RtlQueryRegistryValues(
            RTL_REGISTRY_ABSOLUTE|RTL_REGISTRY_OPTIONAL,
            path,
            &paramTable[0],
            0,
            0);

    } __except (EXCEPTION_EXECUTE_HANDLER) {

        //
        // Oops.
        //

        code = STATUS_ACCESS_VIOLATION;

    }

    ExFreePool(path);

    return(NT_SUCCESS(code) ? TRUE : FALSE);

}


///////////////////////////////////////////////////////////////////////////////
//
//  RegistryReadBinarySubValue
//
//      read a BINARY from the registry using an absolute path
//
//  INPUTS:
//
//      RegistryPath - Pointer to a UNICODE String representing the 
//                     path to the key to be interrogated.  For example,
//					
//          "HKEY_LOCAL_MACHINE\\System\\CurrentControlSet\\Services\\Disk".
//
//      SubKey - Pointer to a UNICODE String representing the sub path to be
//               interrogated. For example, "\\Parameters".
//
//      Key - Pointer to a string representing the DWORD value to be read.  
//            For Example, "Count".
//
//      PBuffer - address of a location to receive the address of the allocated
//                memory.
//
//      Size - size of the allocated memory
// 
//  OUTPUTS:
//
//      If Successful, PBuffer will contain the read registry value and Size will
//      contain the amount of data in PBuffer.
//
//  RETURNS:
//
//      TRUE, if successful, FALSE otherwise.
//
//  IRQL REQUIREMENTS:
//
//      IRQL < DISPATCH_LEVEL
//  
//  NOTES:
//
//      It is the callers responsibility to delete the allocated buffer, if the
//      call to this routine was successful.
//
///////////////////////////////////////////////////////////////////////////////
BOOLEAN RegistryReadBinarySubValue(PUNICODE_STRING RegistryPath, PUNICODE_STRING SubPath, PWSTR Key, 
                                      PUCHAR* PBuffer, PULONG Size)
{
    NTSTATUS code;
    OBJECT_ATTRIBUTES parameterAttributes;
    HANDLE handle;
    UNICODE_STRING parametersKey;
    PWSTR   path;
    PUCHAR  pBinaryData;
    ULONG   binarySize;
    UNICODE_STRING valueName;

    //
    // Allocate memory for the expanded string, include enough space
    // for trailing NULL!
    //

    path = (PWSTR) ExAllocatePoolWithTag(NonPagedPool,RegistryPath->Length+SubPath->Length+sizeof(WCHAR), 'tsOS');

    if (!path) {

        return(FALSE);

    }

    parametersKey.Length = RegistryPath->Length;
    parametersKey.MaximumLength = RegistryPath->Length+SubPath->Length+sizeof(WCHAR);
    parametersKey.Buffer = path;

    RtlCopyMemory(path,RegistryPath->Buffer,RegistryPath->Length);

    RtlAppendUnicodeStringToString(&parametersKey,SubPath);

    InitializeObjectAttributes(&parameterAttributes,
        &parametersKey,
        OBJ_CASE_INSENSITIVE,
        0,
        0);

    code = ZwOpenKey(&handle, KEY_READ, &parameterAttributes);

    ExFreePool(path);

    if (!NT_SUCCESS(code)) {

        //
        // Bail out!
        //

        return FALSE;
    }

    valueName.Length = (USHORT) (wcslen(Key) * sizeof(WCHAR));
    valueName.MaximumLength = (USHORT) (valueName.Length + sizeof(WCHAR));
    valueName.Buffer = Key;

    //
    //  Figure out how much memory we need to allocate.
    //

    code = ZwQueryValueKey(
                           handle, //  IN HANDLE KeyHandle,
                           &valueName, // IN PUNICODE_STRING ValueName,
                           KeyValueFullInformation, // IN KEY_VALUE_INFORMATION_CLASS 
                           NULL, // OUT PVOID KeyValueInformation,
                           0,    // IN ULONG Length,
                           &binarySize); // OUT PULONG ResultLength

    //
    // If the return status was STATUS_BUFFER_TOO_SMALL, then binarySize will contain
    // the amount of memory we need to allocate.
    //

    if(code == STATUS_BUFFER_TOO_SMALL) {

        pBinaryData = (PUCHAR) ExAllocatePoolWithTag(NonPagedPool,binarySize+(4*sizeof(ULONG)), 'DBOS');

        if (!pBinaryData) {

            return(FALSE);

        }

        //
        // Now that we have allocated the memory, read the value again.
        //

        code = ZwQueryValueKey(
                               handle, //  IN HANDLE KeyHandle,
                               &valueName, // IN PUNICODE_STRING ValueName,
                               KeyValuePartialInformation, // IN KEY_VALUE_INFORMATION_CLASS 
                               pBinaryData, // OUT PVOID KeyValueInformation,
                               binarySize+(4*sizeof(ULONG)),    // IN ULONG Length,
                               &binarySize); // OUT PULONG ResultLength

        if(NT_SUCCESS(code)) {

            //
            // We have successfully read the data.  Copy it to the allocated buffer.
            //

            PKEY_VALUE_PARTIAL_INFORMATION pKeyPI = (PKEY_VALUE_PARTIAL_INFORMATION) pBinaryData;
            PUCHAR pData;

            code = STATUS_INSUFFICIENT_RESOURCES;

            if(pKeyPI->DataLength) {

                pData = (PUCHAR) ExAllocatePoolWithTag(NonPagedPool,pKeyPI->DataLength,'1BOS');

                if(pData) {

                    RtlCopyMemory(pData,pKeyPI->Data,pKeyPI->DataLength);
                    *PBuffer = pData;
                    *Size = pKeyPI->DataLength;
                    code = STATUS_SUCCESS;
                    
                }

            }

            //
            //  Clean up after ourselves.
            //

            ExFreePool(pBinaryData);

        } else {

            //
            // We failed, clean up after ourselves.
            //

            ExFreePool(pBinaryData);

        }


    }

    //
    // Tell the whether we succeeded or failed.
    //

    return(NT_SUCCESS(code) ? TRUE : FALSE);
}


///////////////////////////////////////////////////////////////////////////////
//
//  RegistryWriteSubValue
//
//      writes a value to the registry using an absolute path
//
//  INPUTS:
//
//      RegistryPath - Pointer to a UNICODE String representing the 
//                     path to the key to be interrogated.  For example,
//					
//          "HKEY_LOCAL_MACHINE\\System\\CurrentControlSet\\Services\\Disk".
//
//      SubKey - Pointer to a UNICODE String representing the sub path to be
//               interrogated. For example, "\\Parameters".
//
//      Key - Pointer to a string representing the DWORD value to be read.  
//            For Example, "Count".
//
//      PBuffer - address of a location to receive the address of the allocated
//                memory.
//
//      Size - size of the allocated memory
// 
//  OUTPUTS:
//
//      If Successful, PBuffer will contain the read registry value and Size will
//      contain the amount of data in PBuffer.
//
//  RETURNS:
//
//      TRUE, if successful, FALSE otherwise.
//
//  IRQL REQUIREMENTS:
//
//      IRQL < DISPATCH_LEVEL
//  
//  NOTES:
//
//      It is the callers responsibility to delete the allocated buffer, if the
//      call to this routine was successful.
//
///////////////////////////////////////////////////////////////////////////////
BOOLEAN RegistryWriteSubValue(PUNICODE_STRING RegistryPath, PUNICODE_STRING SubPath, PWSTR Key, 
                                      ULONG Type, PUCHAR PBuffer, ULONG Size)
{
    NTSTATUS code;
    OBJECT_ATTRIBUTES parameterAttributes;
    HANDLE handle;
    UNICODE_STRING parametersKey;
    PWSTR   path;
    UNICODE_STRING valueName;

    //
    // Allocate memory for the expanded string, include enough space
    // for trailing NULL!
    //

    path = (PWSTR) ExAllocatePoolWithTag(NonPagedPool,RegistryPath->Length+SubPath->Length+sizeof(WCHAR), 'tsOS');

    if (!path) {

        return(FALSE);

    }

    parametersKey.Length = RegistryPath->Length;
    parametersKey.MaximumLength = RegistryPath->Length+SubPath->Length+sizeof(WCHAR);
    parametersKey.Buffer = path;

    RtlCopyMemory(path,RegistryPath->Buffer,RegistryPath->Length);

    RtlAppendUnicodeStringToString(&parametersKey,SubPath);

    InitializeObjectAttributes(&parameterAttributes,
        &parametersKey,
        OBJ_CASE_INSENSITIVE,
        0,
        0);

    code = ZwOpenKey(&handle, KEY_WRITE, &parameterAttributes);

    ExFreePool(path);

    if (!NT_SUCCESS(code)) {

        ULONG keyDisposition;

        if(code == STATUS_INVALID_HANDLE) {

            code = ZwCreateKey(&handle,KEY_WRITE,&parameterAttributes,
                                 0,
                                 NULL,
                                 REG_OPTION_NON_VOLATILE,
                                 &keyDisposition);

            if(NT_SUCCESS(code)) {

                ZwClose(handle);
                return RegistryWriteSubValue(RegistryPath,SubPath,Key,Type,PBuffer,Size);

            }
        }

        return FALSE;
    }

    valueName.Length = (USHORT) (wcslen(Key) * sizeof(WCHAR));
    valueName.MaximumLength = (USHORT) (valueName.Length + sizeof(WCHAR));
    valueName.Buffer = Key;

    //
    //  Figure out how much memory we need to allocate.
    //

    code = ZwSetValueKey(handle, //  IN HANDLE KeyHandle,
                         &valueName, // IN PUNICODE_STRING ValueName,
                         NULL,       // IN KEY_VALUE_INFORMATION_CLASS 
                         Type,
                         PBuffer,    
                         Size);

    //
    // Tell the whether we succeeded or failed.
    //

    ZwClose(handle);

    return(NT_SUCCESS(code) ? TRUE : FALSE);
}


///////////////////////////////////////////////////////////////////////////////
//
//  GetDeviceTypeInfo
//
//    This routine returns a PINQUIRY_DEVICE_TYPE for a input Device Type.
//
//  INPUTS:
//
//      DeviceType - Device Type
//
//  OUTPUTS:
//
//      None.
//
//  RETURNS:
//
//      INQUIRY_DEVICE_TYPE information pointer.
//
//  IRQL:
//
//    This routine is called at PASSIVE_LEVEL.
//
//  NOTES:
//
///////////////////////////////////////////////////////////////////////////////
PINQUIRY_DEVICE_TYPE GetDeviceTypeInfo(IN UCHAR DeviceType)
{
    if(DeviceType >= NUM_DEVICE_TYPE_INFO_ENTRIES) {

        return &(DeviceTypeInfo[NUM_DEVICE_TYPE_INFO_ENTRIES - 1]);

    } else {

        return &(DeviceTypeInfo[DeviceType]);

    }
};


///////////////////////////////////////////////////////////////////////////////
//
//  Get_CRC_CheckSum
//
//      Calculate CheckSum for block
//
//  INPUTS:
//
//      pBuffer - buffer to checksum.
//
//      ulSize  - number of bytes to checksum
//
//  OUTPUTS:
//
//      pulSeed - location to receive calculated checksum.
//
//  RETURNS:
//
//      NONE.
//
//  IRQL:
//
//    This routine is called at any IRQL PASSIVE_LEVEL.
//
//  NOTES:
//
///////////////////////////////////////////////////////////////////////////////
void Get_CRC_CheckSum(PVOID pBuffer, ULONG ulSize, PULONG pulSeed)
{
    PUCHAR    pb;
    UCHAR    bTmp;

    for (pb=(UCHAR *)pBuffer; ulSize; ulSize--, pb++) {

        bTmp=(UCHAR)(((USHORT)*pb)^((USHORT)*pulSeed));    // Xor CRC with new char
        *pulSeed=((*pulSeed)>>8) ^ wCRC16a[bTmp&0x0F] ^ wCRC16b[bTmp>>4];

    }
}


///////////////////////////////////////////////////////////////////////////////
//
//  DoClose
//
//      Closes the file handle created in system context by the DO_CREATE
//      command.
//
//  INPUTS:
//
//      PGInfo - Pointer to the Global Information BLock.
//
//		PEntry - Connection List Entry
//
//      FileHandle - handle of file to close.
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
//    This routine is called at any IRQL <= DISPATCH_LEVEL.
//
//  NOTES:
//
///////////////////////////////////////////////////////////////////////////////
void DoClose(PUSER_GLOBAL_INFORMATION PGInfo,PCONNECTION_LIST_ENTRY PEntry)
{
}


///////////////////////////////////////////////////////////////////////////////
//
// DiscardIrpCompletion
//
//      This routine is used to discard an IRP from the completion routine.
//
// Inputs:
//      DeviceObject - not used
//      Irp - the I/O operation being completed
//      Context - If nonzero, it is the MDL address that we should free.
//            If zero, we don't free the MDL.
//
// Outputs:
//      None.
//
// Returns:
//      STATUS_MORE_PROCESSING_REQUIRED - terminates I/O completion processing.
//
// Notes:
//      The purpose of this routine is to do "cleanup" on I/O operations
//      so we don't queue an APC for the I/O completion...
//
///////////////////////////////////////////////////////////////////////////////
NTSTATUS DiscardIrpCompletion(PDEVICE_OBJECT DeviceObject,
                                  PIRP Irp,
                                  PVOID Context)
{
    //
    // We don't use these parameters, so we supress warning messages.
    //
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Context);

    //
    // Copy the status information back into the "user" IOSB.
    //
    *Irp->UserIosb = Irp->IoStatus;
    
    //
    // Set the user event - wakes up the mainline code doing this.
    //
    KeSetEvent(Irp->UserEvent, IO_SOUND_INCREMENT, FALSE);


    Irp->UserEvent = NULL;

    //
    // Clear it up for completeness.
    //

    Irp->MdlAddress = NULL;
    
    //
    // Free the IRP now that we are done with it.
    //
    IoFreeIrp(Irp);
    
    //
    // We return STATUS_MORE_PROCESSING_REQUIRED because this "magic" return value
    // tells the I/O Manager that additional processing will be done by this driver
    // to the IRP - in fact, it might (as it is in this case) already BE done - and
    // the IRP cannot be completed.
    //
    return STATUS_MORE_PROCESSING_REQUIRED;
}


///////////////////////////////////////////////////////////////////////////////
//
// DoRead
//
//  This routine reads data from a given volume
//
// Inputs:
//
//      PEntry - Entry describing who read is targeted to.
//      BufferMdl - the data buffer
//      ByteOffset - the byte offset to read
//      Length - the length of data to read
//
// Outputs:
//      PBytesRead - Address to tell number of bytes read
//
// Returns:
//      STATUS_SUCCESS if successful, an error otherwise.
//
// Notes:
//
//   The caller is responsible for ensuring the buffer is large enough; otherwise
//   the call will fail.
//
///////////////////////////////////////////////////////////////////////////////
NTSTATUS DoRead(PCONNECTION_LIST_ENTRY PEntry,
            PMDL BufferMdl,
            ULARGE_INTEGER ByteOffset,
            ULONG Length,
            PULONG PBytesRead,
            BOOLEAN BNonCached)
{
    ULONG readLength = Length;

    //
    // We have been requested to do a read.   So we need to read from
    // our in memory disk into the memory described by the MDL.
    //
    PVOID pBuffer = MmGetSystemAddressForMdlSafe(BufferMdl,NormalPagePriority);

    if(!pBuffer) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // Make sure that the read does not go off the end off the end of the in
    // memory buffer.   If it does, we crash...
    //
    if(ByteOffset.QuadPart > (ULONGLONG) PEntry->DiskSize) {
        return STATUS_INVALID_PARAMETER;
    }

    if(ByteOffset.QuadPart+(ULONGLONG)Length > (ULONGLONG) PEntry->DiskSize) {
        readLength = (ULONG) ((ULONGLONG) PEntry->DiskSize - ByteOffset.QuadPart);
    }

    PUCHAR pSrcBuf = (PUCHAR) PEntry->DiskBaseAddress;
    RtlCopyMemory(pBuffer,&pSrcBuf[ByteOffset.QuadPart],readLength);

    *PBytesRead = readLength;

    return STATUS_SUCCESS;
}


///////////////////////////////////////////////////////////////////////////////
//
// DoWrite
//
//  This routine writes data to a given volume
//
// Inputs:
//
//      PEntry - Entry describing who Write is targeted to.
//      BufferMdl - the data buffer
//      ByteOffset - the byte offset to write
//      Length - the length of data to write
//
// Outputs:
//      PBytesWritten - Address to tell number of bytes written
//
// Returns:
//      STATUS_SUCCESS if successful, an error otherwise.
//
// Notes:
//
//   The caller is responsible for ensuring the buffer is large enough; otherwise
//   the call will fail.
//
///////////////////////////////////////////////////////////////////////////////
NTSTATUS DoWrite(PCONNECTION_LIST_ENTRY PEntry,
                            PMDL BufferMdl,
                            ULARGE_INTEGER ByteOffset,
                            ULONG Length,
                            PULONG PBytesWritten,
                            BOOLEAN BNonCached)
{
    ULONG writeLength = Length;

    //
    // We have been requested to do a read.   So we need to read from
    // our in memory disk into the memory described by the MDL.
    //
    PVOID pBuffer = MmGetSystemAddressForMdlSafe(BufferMdl,NormalPagePriority);

    if(!pBuffer) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // Make sure that the read does not go off the end off the end of the in
    // memory buffer.   If it does, we crash...
    //
    if(ByteOffset.QuadPart > (ULONGLONG) PEntry->DiskSize) {
        return STATUS_INVALID_PARAMETER;
    }

    if(ByteOffset.QuadPart+(ULONGLONG)Length > (ULONGLONG) PEntry->DiskSize) {
        writeLength = (ULONG) ((ULONGLONG) PEntry->DiskSize - ByteOffset.QuadPart);
    }

    PUCHAR pDestBuf = (PUCHAR) PEntry->DiskBaseAddress;
    RtlCopyMemory(&pDestBuf[ByteOffset.QuadPart],pBuffer,writeLength);

    *PBytesWritten = writeLength;

    return STATUS_SUCCESS;
}


///////////////////////////////////////////////////////////////////////////////
//
//  CreateMultiSZ
//
//      This function will that a null terminated string and merge them
//      into a unicode multi sz block.
//
//  INPUTS:
//
//      MultiString - a UNICODE_STRING structure into which the multi string will
//                  be built.
//
//      StringArray - a NULL terminated list of narrow strings which will be combined
//                  together.  This list may not be empty.
//
//  OUTPUTS:
//
//      None.
//
//  RETURNS:
//
//      STATUS_SUCCESS if successful, an error otherwise
//
//  IRQL:
//
//    This routine is called at any IRQL PASSIVE_LEVEL.
//
//  NOTES:
//
//      This routine allocates memory for the string buffer, it is the callers
//      responsibility to free the memory.
//
///////////////////////////////////////////////////////////////////////////////
NTSTATUS CreateMultiSZ(PUNICODE_STRING MultiString,PCSTR StringArray[])

{
    ANSI_STRING ansiEntry;
    UNICODE_STRING unicodeEntry;
    UCHAR i;
    NTSTATUS status;

    //
    // Make sure we aren't going to leak any memory
    //

    OSRASSERT(MultiString->Buffer == NULL);

    RtlInitUnicodeString(MultiString, NULL);

    for(i = 0; StringArray[i] != NULL; i++) {

        RtlInitAnsiString(&ansiEntry, StringArray[i]);

        MultiString->Length = MultiString->Length + (USHORT) RtlAnsiStringToUnicodeSize(&ansiEntry);
    }

    OSRASSERT(MultiString->Length != 0);

    MultiString->MaximumLength = MultiString->Length + sizeof(UNICODE_NULL);

    MultiString->Buffer = (PWSTR) ExAllocatePoolWithTag(NonPagedPool,
                                                MultiString->MaximumLength,
                                                'diOS');

    if(MultiString->Buffer == NULL) {

        return STATUS_INSUFFICIENT_RESOURCES;

    }

    RtlZeroMemory(MultiString->Buffer, MultiString->MaximumLength);

    unicodeEntry = *MultiString;

    for(i = 0; StringArray[i] != NULL; i++) {

        RtlInitAnsiString(&ansiEntry, StringArray[i]);

        status = RtlAnsiStringToUnicodeString(
                    &unicodeEntry,
                    &ansiEntry,
                    FALSE);

        //
        // Since we're not allocating any memory the only failure possible
        // is if this function is bad
        //

        OSRASSERT(NT_SUCCESS(status));

        //
        // Push the buffer location up and reduce the maximum count
        //

        unicodeEntry.Buffer = (PWSTR) ((PSTR) unicodeEntry.Buffer) + 
			unicodeEntry.Length + sizeof(WCHAR);
        unicodeEntry.MaximumLength -= unicodeEntry.Length + sizeof(WCHAR);

    };

    //
    // Stick the final NUL on the end of the multisz
    //

//    RtlZeroMemory(unicodeEntry.Buffer, unicodeEntry.MaximumLength);

    return STATUS_SUCCESS;
}


///////////////////////////////////////////////////////////////////////////////
//
//  OsrUserReadData
//
//      Read data from the user device.
//
//  INPUTS:
//
//      UserLocalInfo - User Local Data Pointer.
//      Irp - Address of the Irp that describes this request.
//      MDLAddress - MDL for read data.
//      StartingLbn - byteoffset on disk to read data from.
//      ReadLength - number of bytes to read.
//
//  OUTPUTS:
//
//      PBytesRead - address of location to return number of bytes read from disk
//
//  RETURNS:
//
//      STATUS_SUCCESS if okay, error otherwise.   If the user elects to, it can return
//      STATUS_PENDING indicating that he is holding onto the request.   It is his responsibility
//      to complete the Irp later on.
//
//  IRQL:
//
//      This routine is called at any IRQL < DISPATCH_LEVEL.
//
//  NOTES:
//
///////////////////////////////////////////////////////////////////////////////
NTSTATUS OsrUserReadData(IN PVOID UserLocalInfoHandle,PSCSI_REQUEST_BLOCK PSrb,
						 PMDL MdlAddress,
						 ULARGE_INTEGER StartingLbn,
                         ULONG ReadLength,PULONG PBytesRead)
{
    PUSER_INSTANCE_INFORMATION pIInfo = (PUSER_INSTANCE_INFORMATION) UserLocalInfoHandle;
    KIRQL               oldIrql;

	OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,(__FUNCTION__": Enter\n"));

    USER_INSTANCE_VALID(pIInfo);

    PCONNECTION_LIST_ENTRY  pConnectionInformation = pIInfo->ConnectionInformation;

    //
    // Sector 0 Read??
    //

    if(StartingLbn.QuadPart == 0) {

        OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB_USER,
			("OsrUserReadData: Read to Sector 0, decrypting. Length = %d\n",ReadLength));

    }

    //
    // Figure out the offset based on what we are trying to emulate.
    //
	StartingLbn.QuadPart += 0;

    ULONG readLength = ReadLength;

    //
    // We have been requested to do a read.   So we need to read from
    // our in memory disk into the memory described by the MDL.
    //
    PVOID pBuffer = MmGetSystemAddressForMdlSafe(MdlAddress,NormalPagePriority);

    if(!pBuffer) {
        PSrb->SrbStatus = SRB_STATUS_TIMEOUT;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // Make sure that the read does not go off the end off the end of the in
    // memory buffer.   If it does, we crash...
    //
    if(StartingLbn.QuadPart > (ULONGLONG) pConnectionInformation->DiskSize) {
        PSrb->SrbStatus = SRB_STATUS_BAD_SRB_BLOCK_LENGTH;
        return STATUS_INVALID_PARAMETER;
    }

    if(StartingLbn.QuadPart+(ULONGLONG)ReadLength > (ULONGLONG) pConnectionInformation->DiskSize) {
        readLength = (ULONG) ((ULONGLONG) pConnectionInformation->DiskSize - StartingLbn.QuadPart);
    }

    PUCHAR pSrcBuf = (PUCHAR) pConnectionInformation->DiskBaseAddress;
    RtlCopyMemory(pBuffer,&pSrcBuf[StartingLbn.QuadPart],readLength);

    *PBytesRead = readLength;

    PSrb->SrbStatus = SRB_STATUS_SUCCESS;
    PSrb->DataTransferLength = readLength;

    OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,(__FUNCTION__": Exit\n"));

    return STATUS_SUCCESS;
}


///////////////////////////////////////////////////////////////////////////////
//
//  OsrUserWriteData
//
//      Write data to the user device.
//
//  INPUTS:
//
//      UserLocalInfo - User Local Data Pointer.
//      Irp - Address of the Irp that describes this request.
//      MDLAddress - MDL for write data.
//      StartingLbn - byte offset on disk to write data to.
//      WriteLength - number of bytes to write.
//
//  OUTPUTS:
//
//      PBytesWritten - address of location to return number of bytes written to disk
//
//  RETURNS:
//
//      STATUS_SUCCESS if okay, error otherwise.
//
//  IRQL:
//
//      This routine is called at any IRQL < DISPATCH_LEVEL.
//
//  NOTES:
//
///////////////////////////////////////////////////////////////////////////////
NTSTATUS OsrUserWriteData(IN PVOID UserLocalInfoHandle,PSCSI_REQUEST_BLOCK PSrb,
						  PMDL MdlAddress,
						  ULARGE_INTEGER StartingLbn,
                          ULONG WriteLength,PULONG PBytesWritten)
{
    PUSER_INSTANCE_INFORMATION pIInfo = (PUSER_INSTANCE_INFORMATION) UserLocalInfoHandle;

    OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,(__FUNCTION__": Enter\n"));

    USER_INSTANCE_VALID(pIInfo);

    PCONNECTION_LIST_ENTRY  pConnectionInformation = pIInfo->ConnectionInformation;

    //
    // Write to Sector 0??
    //

    if(StartingLbn.QuadPart == 0) {

        OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB_USER,
			("OsrUserWriteData: WRITE to Sector 0, Length = %d.\n",WriteLength));

    }

    //
    // Figure out the offset based on what we are trying to emulate.
    //

	StartingLbn.QuadPart += 0;

    ULONG writeLength = WriteLength;

    //
    // We have been requested to do a read.   So we need to read from
    // our in memory disk into the memory described by the MDL.
    //
    PVOID pBuffer = MmGetSystemAddressForMdlSafe(MdlAddress,NormalPagePriority);

    if(!pBuffer) {
        PSrb->SrbStatus = SRB_STATUS_TIMEOUT;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // Make sure that the read does not go off the end off the end of the in
    // memory buffer.   If it does, we crash...
    //
    if(StartingLbn.QuadPart > (ULONGLONG) pConnectionInformation->DiskSize) {
        PSrb->SrbStatus = SRB_STATUS_BAD_SRB_BLOCK_LENGTH;
        return STATUS_INVALID_PARAMETER;
    }

    if(StartingLbn.QuadPart+(ULONGLONG)WriteLength > (ULONGLONG) pConnectionInformation->DiskSize) {
        writeLength = (ULONG) ((ULONGLONG) pConnectionInformation->DiskSize - StartingLbn.QuadPart);
    }

    PUCHAR pDestBuf = (PUCHAR) pConnectionInformation->DiskBaseAddress;
    RtlCopyMemory(&pDestBuf[StartingLbn.QuadPart],pBuffer,writeLength);

    *PBytesWritten = writeLength;

    PSrb->SrbStatus = SRB_STATUS_SUCCESS;
    PSrb->DataTransferLength = writeLength;

    OsrTracePrint(TRACE_LEVEL_VERBOSE,OSRVMINIPT_DEBUG_FUNCTRACE,(__FUNCTION__": Exit\n"));

    return STATUS_SUCCESS;
}
