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
//        $File: osrstorpt\osrstorptuserintf.cpp $
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
//        $Revision: #5 $
//
///////////////////////////////////////////////////////////////////////////////
#include "ustorptimpl.h"


GLOBALS Globals = {0};


//
// Forward definitions.
//
VOID InitializeScsiIds();
BOOLEAN FindConnectionMatch(PUSER_GLOBAL_INFORMATION PGInfo, PCONNECT_IN PConnectInfo,
                            PCONNECTION_LIST_ENTRY* PPFoundEntry);
void UpdateConnectionListInRegistry(PUSER_GLOBAL_INFORMATION PGInfo);
void DeleteConnectionListInRegistry(PUSER_GLOBAL_INFORMATION PGInfo);




///////////////////////////////////////////////////////////////////////////////
//
//  OsrUserInitialize
//
//      Called by the OSR Virtual Miniport Driver to get the size of
//		a SRB extension that the user layer will need.
//
//  INPUTS:
//
//      None.
//
//  OUTPUTS:
//
//      None.
//
//  RETURNS:
//
//      Size of the extension needed
//
//  IRQL:
//
//      This routine is called at any IRQL == PASSIVE_LEVEL.
//
//  NOTES:
//
///////////////////////////////////////////////////////////////////////////////
ULONG OsrUserGetSrbExtensionSize(VOID)
{
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_USER,
		(__FUNCTION__": Entered.\n"));

    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_USER,
		(__FUNCTION__": Exit.\n"));

	return 0;	
}



///////////////////////////////////////////////////////////////////////////////
//
//  OsrUserInitialize
//
//      Called by the OSR Virtual Miniport Driver to initialize the
//		user component of the virtual miniport.
//
//  INPUTS:
//
//      OsrSpHandle - Address of miniport handle that is used to
//					  call back into the miniport control routines.
//		Pdo - address of SRB to process.
//
//  OUTPUTS:
//
//      PPUserGlobalInfoHandle - address of location to receive the
//			global handle allocated by the user portion of the miniport.
//		PNodeNumber - address of location to receive node number.
//
//
//  RETURNS:
//
//      STATUS_SUCCESS or another status to indicate an error.
//
//  IRQL:
//
//      This routine is called at any IRQL == PASSIVE_LEVEL.
//
//  NOTES:
//
///////////////////////////////////////////////////////////////////////////////
NTSTATUS OsrUserInitialize(PVOID OsrSpHandle,PDEVICE_OBJECT Pdo,
						   PVOID* PPUserGlobalInfoHandle,PULONG PNodeNumber)
{
    UNICODE_STRING subPath;

    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_USER,
		(__FUNCTION__": Entered.\n"));

    *PPUserGlobalInfoHandle = NULL;

    //
    // This is where you do any initialization that you need to do for 
    // your internal data structures.
    //

    PUSER_GLOBAL_INFORMATION pGInfo = (PUSER_GLOBAL_INFORMATION) 
										ExAllocatePoolWithTag(NonPagedPool,
											sizeof(USER_GLOBAL_INFORMATION),'ULGp');

    if(!pGInfo) {
		OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_USER,
			(__FUNCTION__": Error allocating pGInfo.\n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(pGInfo,sizeof(USER_GLOBAL_INFORMATION));

    pGInfo->MagicNumber = USER_GLOBAL_INFORMATION_MAGIC_NUMBER;
    pGInfo->OsrSPHandle = OsrSpHandle;

    OsrInitializeSpinLock(&pGInfo->ConnectionListLock);
    InitializeListHead(&pGInfo->ConnectionList);
    KeInitializeMutex(&pGInfo->ConnectionMutex,0);

    *PPUserGlobalInfoHandle = pGInfo;

    InitializeScsiIds();

    RtlZeroMemory(&Globals,sizeof(GLOBALS));

    //
    // Read Other Stored Information from Registry.
    //
    Globals.AddConnectionsToRegistry = FALSE;

    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_USER,
		(__FUNCTION__": Exit.\n"));

    return STATUS_SUCCESS;
}



///////////////////////////////////////////////////////////////////////////////
//
//  OsrUserDeleteGlobalInformation
//
//      Called by the OSR Virtual Miniport Driver to delete the
//		user component of the virtual miniport.
//
//  INPUTS:
//
//      PUserGlobalInfoHandle - Address of user structure to
//			remove.
//
//  OUTPUTS:
//
//		None.
//
//  RETURNS:
//
//      None.
//
//  IRQL:
//
//      This routine is called at any IRQL == PASSIVE_LEVEL.
//
//  NOTES:
//
///////////////////////////////////////////////////////////////////////////////
VOID OsrUserDeleteGlobalInformation(PVOID PUserGlobalInfo)
{
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_USER,
		(__FUNCTION__": Entered.\n"));

    PUSER_GLOBAL_INFORMATION pGInfo = (PUSER_GLOBAL_INFORMATION) PUserGlobalInfo;

    ExFreePool(pGInfo);

    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_USER,
		(__FUNCTION__": Exit.\n"));
}


///////////////////////////////////////////////////////////////////////////////
//
//  OsrUserAdapterStarted
//
//      Called by the OSR SCSI Port driver when the FDO for the device has
//      received and successfully processed a IRP_MN_START_DEVICE.
//
//  INPUTS:
//
//      PUserGlobalInfo - Address of user Global Data Block.
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
//      This routine is called at any IRQL < DISPATCH_LEVEL.
//
//  NOTES:
//
//      This is where the user can do any kind of additional operations
//      necessary.   If this routine fails, the IRP_MN_START_DEVICE will
//      fail also, and the device will not be operational.
//
///////////////////////////////////////////////////////////////////////////////
NTSTATUS OsrUserAdapterStarted(IN PVOID PUserGlobalHandle)
{
    NTSTATUS status = STATUS_SUCCESS;
    PUSER_GLOBAL_INFORMATION pGInfo = (PUSER_GLOBAL_INFORMATION) PUserGlobalHandle;

    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_USER,
		(__FUNCTION__": Entered.\n"));

    USER_GLOBAL_VALID(pGInfo);

    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_USER,
		(__FUNCTION__": Exit.\n"));

    return status;
}




///////////////////////////////////////////////////////////////////////////////
//
//  OsrUserProcessIoCtl
//
//      Called by the OSR Virtual Miniport Driver to delete the
//		user component of the virtual miniport.
//
//  INPUTS:
//
//      PUserGlobalInfoHandle - Address of user structure global handle.
//		PIrp - Address of user IRP to process.
//
//  OUTPUTS:
//
//		STATUS_SUCCESS if successful an error otherwise....
//
//  RETURNS:
//
//      None.
//
//  IRQL:
//
//      This routine is called at any IRQL == PASSIVE_LEVEL.
//
//  NOTES:
//
///////////////////////////////////////////////////////////////////////////////
NTSTATUS OsrUserProcessIoCtl(IN PVOID PUserGlobalHandle,IN PIRP PIrp)
{
    PIO_STACK_LOCATION			irpStack;
    NTSTATUS					status;
    ULONG						inlen, outlen;
    PVOID						buffer;
    PUSER_GLOBAL_INFORMATION	pGInfo = (PUSER_GLOBAL_INFORMATION) PUserGlobalHandle;

    USER_GLOBAL_VALID(pGInfo);
    
    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_USER,
			("OsrUserProcessIoCtl Entered.\n"));

    irpStack = IoGetCurrentIrpStackLocation(PIrp);

    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_USER,
		(__FUNCTION__": IOCTL = 0x%x.\n",irpStack->Parameters.DeviceIoControl.IoControlCode));

    switch (irpStack->Parameters.DeviceIoControl.IoControlCode) {

		case IOCTL_MINIPORT_PROCESS_SERVICE_IRP: {
			PCOMMAND_IN command = (PCOMMAND_IN) PIrp->AssociatedIrp.SystemBuffer;

			if(!PIrp->AssociatedIrp.SystemBuffer || 
				irpStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(COMMAND_IN)) {
				OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_USER,
					(__FUNCTION__": IOCTL = 0x%x. SystemBuffer Error\n",
					irpStack->Parameters.DeviceIoControl.IoControlCode));
				return STATUS_INVALID_PARAMETER;
			}
			switch(command->IoControlCode) {

				case IOCTL_OSRVMPORT_SCSIPORT: {
					status = STATUS_SUCCESS;
				}
				break;


				case IOCTL_OSRVMPORT_CONNECT: {

					PCONNECT_IN pConnectInfo = (PCONNECT_IN) PIrp->AssociatedIrp.SystemBuffer;

					if(!pConnectInfo || 
						irpStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(CONNECT_IN)) {
						OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_USER,
							(__FUNCTION__": IOCTL = 0x%x. SystemBuffer Error\n",
							irpStack->Parameters.DeviceIoControl.IoControlCode));
						status = STATUS_INVALID_PARAMETER;
						break;

					}

					if(!wcslen((PWSTR) &pConnectInfo->InstanceName)) {
						OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_USER,
							(__FUNCTION__": IOCTL = 0x%x. Pathname Error\n",
							irpStack->Parameters.DeviceIoControl.IoControlCode));
						status = STATUS_INVALID_PARAMETER;
						break;
					}

					KeWaitForMutexObject(
						&pGInfo->ConnectionMutex,
						UserRequest,
						KernelMode,
						FALSE,
						NULL
						);

					//
					// See if we already have a connection with this 
					// information.   If we do, then we will not
					// create a new connection.

					if(FindConnectionMatch(pGInfo,pConnectInfo,NULL)) {

						//
						// We already have a connection to this, so
						// we indicate that point and exit.
						//
		                
						KeReleaseMutex(&pGInfo->ConnectionMutex,FALSE);
						status = STATUS_FILES_OPEN;
						OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_USER,
							(__FUNCTION__": IOCTL = 0x%x. Connection Exists Error\n",
							irpStack->Parameters.DeviceIoControl.IoControlCode));
						break;

					}

					//
					// This looks like a unique connection, Attempt to create a 
					// connection to the Filer.
					//

					status = CreateConnection(pGInfo,pConnectInfo);

					if(NT_SUCCESS(status)) {

						//
						// Add connection to Registry List.
						//

						UpdateConnectionListInRegistry(pGInfo);

					}

					KeReleaseMutex(&pGInfo->ConnectionMutex,FALSE);

					}
					break;

				case IOCTL_OSRVMPORT_DISCONNECT: {

					PCONNECT_IN pDisconnectInfo = (PCONNECT_IN) PIrp->AssociatedIrp.SystemBuffer;

					if(!pDisconnectInfo || 
						irpStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(CONNECT_IN)) {
						OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_USER,
							(__FUNCTION__": IOCTL = 0x%x. SystemBuffer Error\n",
							irpStack->Parameters.DeviceIoControl.IoControlCode));
						status = STATUS_INVALID_PARAMETER;
						break;

					}

					//
					// Validate the connection Information before passing it on.
					//
					if(!wcslen((PWSTR) &pDisconnectInfo->InstanceName)) {
						OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_USER,
							(__FUNCTION__": IOCTL = 0x%x. SystemBuffer Error\n",
							irpStack->Parameters.DeviceIoControl.IoControlCode));
						status = STATUS_INVALID_PARAMETER;
						break;
					}

					KeWaitForMutexObject(
						&pGInfo->ConnectionMutex,
						UserRequest,
						KernelMode,
						FALSE,
						NULL
						);

					//
					// See if we already have a connection with this 
					// information.   If we do, then we will not
					// create a new connection.

					if(!FindConnectionMatch(pGInfo,pDisconnectInfo,NULL)) {

						//
						// We already have a connection to this, so
						// we indicate that point and exit.
						//

						KeReleaseMutex(&pGInfo->ConnectionMutex,FALSE);
						status = STATUS_OBJECT_NAME_NOT_FOUND;
						OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_USER,
							(__FUNCTION__": IOCTL = 0x%x. No Connection Match Error\n",
							irpStack->Parameters.DeviceIoControl.IoControlCode));
						break;

					}

					//
					// This looks like a unique connection, Attempt to delete a 
					// connection to the Filer.
					//

					status = DeleteConnection(pGInfo,pDisconnectInfo);


					if(NT_SUCCESS(status)) {

						//
						// Delete connection from Registry List.
						//

						UpdateConnectionListInRegistry(pGInfo);

					}

					KeReleaseMutex(&pGInfo->ConnectionMutex,FALSE);

					}
					break;

				case IOCTL_OSRVMPORT_GETACTIVELIST: {

					if(!PIrp->AssociatedIrp.SystemBuffer || 
						(irpStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(GETACTIVELIST_OUT))) {
						OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_USER,
							(__FUNCTION__": IOCTL = 0x%x. System Buffer Error\n",
							irpStack->Parameters.DeviceIoControl.IoControlCode));
						status = STATUS_INVALID_PARAMETER;
						break;

					}

					KeWaitForMutexObject(
						&pGInfo->ConnectionMutex,
						UserRequest,
						KernelMode,
						FALSE,
						NULL
						);

					//
					// Enumerate the active connections.
					//

					status = EnumerateActiveConnections(pGInfo,PIrp);

					KeReleaseMutex(&pGInfo->ConnectionMutex,FALSE);
		            
					}
					break;

				default:
					OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_IOCTL_INFO,
						("ScsiPortDeviceControl: Unsupported IOCTL (%x)\n",
							 irpStack->Parameters.DeviceIoControl.IoControlCode));
					status = STATUS_INVALID_DEVICE_REQUEST;
					break;
				}
			}
			break;
        //
        // Default Processing.
        //

        default:

            OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_IOCTL_INFO,
				("ScsiPortDeviceControl: Unsupported IOCTL (%x)\n",
                     irpStack->Parameters.DeviceIoControl.IoControlCode));

            status = STATUS_INVALID_DEVICE_REQUEST;

            break;

    }

    return status;
}


///////////////////////////////////////////////////////////////////////////////
//
//  OsrUserGetScsiCapabilities
//
//      Called by the OSR Virtual Miniport Driver when it needs the SCSI
//		capabilities of this virtual adapter.
//
//  INPUTS:
//
//      PUserGlobalInfo - Address of user Global Data Block.
//
//  OUTPUTS:
//
//      PCapabilities - address of capabilities structure.
//
//  RETURNS:
//
//      None.
//
//  IRQL:
//
//      This routine is called at any IRQL == PASSIVE_LEVEL.
//
//  NOTES:
//
///////////////////////////////////////////////////////////////////////////////
VOID OsrUserGetScsiCapabilities(IN PVOID PUserGlobalHandle,
								PIO_SCSI_CAPABILITIES PCapabilities)
{
    PUSER_GLOBAL_INFORMATION pGInfo = (PUSER_GLOBAL_INFORMATION) PUserGlobalHandle;

    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_USER,
		(__FUNCTION__": Entered.\n"));

    USER_GLOBAL_VALID(pGInfo);

    PCapabilities->MaximumTransferLength = OSRSCSI_MAXIMUM_TRANSFER_SIZE;
    PCapabilities->MaximumPhysicalPages = (OSRSCSI_MAXIMUM_TRANSFER_SIZE/PAGE_SIZE) + 1;

    PCapabilities->SupportedAsynchronousEvents = FALSE;
    PCapabilities->AlignmentMask = 0x3;  // LONGWORD alignment
    PCapabilities->TaggedQueuing = FALSE; // TRUE;
    PCapabilities->AdapterScansDown = FALSE;
    PCapabilities->AdapterUsesPio = FALSE;

    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_USER,
		(__FUNCTION__": Exit.\n"));
}


///////////////////////////////////////////////////////////////////////////////
//
//  ProcessScsiCommandError
//
//      This routine is called by the SCSIOP processing code in order to
//      return a SCSISTAT_CHECK_CONDITION error in the event we get an input
//      sense buffer.
//
//  INPUTS:
//
//      PSrb - Address of user Global Data Block.
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
//      This routine is called at any IRQL < DISPATCH_LEVEL.
//
//  NOTES:
//
///////////////////////////////////////////////////////////////////////////////
NTSTATUS ProcessScsiCommandError(PSCSI_REQUEST_BLOCK PSrb) 
{
    NTSTATUS status = STATUS_SUCCESS;

    if(PSrb->SenseInfoBuffer && PSrb->SenseInfoBufferLength) {
        PSENSE_DATA pSense = (PSENSE_DATA) PSrb->SenseInfoBuffer;
        RtlZeroMemory(pSense,PSrb->SenseInfoBufferLength);
        pSense->ErrorCode = 0x70;
        pSense->Valid = 0;
        pSense->SenseKey = SCSI_SENSE_ILLEGAL_REQUEST;
        pSense->AdditionalSenseLength = 0x15;
        pSense->AdditionalSenseCode = SCSI_ADSENSE_ILLEGAL_COMMAND;
        pSense->AdditionalSenseCodeQualifier = 0;
        PSrb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
        PSrb->SrbStatus = SRB_STATUS_AUTOSENSE_VALID | SRB_STATUS_ERROR;
    } else {
        status = STATUS_UNSUCCESSFUL;
        PSrb->SrbStatus = SRB_STATUS_ERROR;
    }
    //
    // Set this to 0 to indicate that no data was transfered because
    // of the error.
    //
    PSrb->DataTransferLength = 0;

    return status;
}


///////////////////////////////////////////////////////////////////////////////
//
//  OsrUserHandleSrb
//
//      Called by the OSR Virtual Miniport Driver when it receives a 
//		SRB_FUNCTION_EXECUTE_SCSI function.
//
//  INPUTS:
//
//      UserLocalInfoHandle - Address of user Local Data Block.
//		PSrb - address of SRB to process.
//
//  OUTPUTS:
//
//      None.
//
//  RETURNS:
//
//      STATUS_PENDING or any other status to indicate that the request is done.
//
//  IRQL:
//
//      This routine is called at any IRQL == PASSIVE_LEVEL.
//
//  NOTES:
//
///////////////////////////////////////////////////////////////////////////////
NTSTATUS OsrUserHandleSrb(IN PVOID UserLocalInfoHandle,PSCSI_REQUEST_BLOCK PSrb)
{
    PCDB        pCdb = (PCDB) &PSrb->Cdb;
    NTSTATUS    status = STATUS_UNSUCCESSFUL;
    PUSER_INSTANCE_INFORMATION  pIInfo = (PUSER_INSTANCE_INFORMATION) UserLocalInfoHandle;

    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB_USER,
		("OsrUserHandleSrb: Processing %s command.\n",
		(const char*) OsrSpPrintSCSICDBOperation(pCdb->CDB6READWRITE.OperationCode)));


    USER_INSTANCE_VALID(pIInfo);

    switch(pCdb->CDB6READWRITE.OperationCode) {

        case SCSIOP_READ                : // 0x28
            {
            //
            // We have received a read request.   Process the read.
            //
            ULARGE_INTEGER  startingLbn = {0,0};
            ULONG           readLength = 0;
            PFOUR_BYTE      pTmp;
            PCDB            pReadCdb = (PCDB) &PSrb->Cdb[0];
            ULONG           bytesRead;
            ULONG           numBlocks;
            ULONG           bytesPerBlock;

            __try {

                OsrUserGetDiskCapacity(pIInfo,&numBlocks,&bytesPerBlock);

                //
                // Convert the starting LBN back to little endian.
                // Convert the LBN to a byte offset instead of a block offset.
                //

                REVERSE_BYTES(&startingLbn.LowPart,&pReadCdb->CDB10.LogicalBlockByte0)
                startingLbn.QuadPart *= bytesPerBlock;

                //
                // Convert the read length back to little endian
                //

                REVERSE_2BYTES(&readLength,&pReadCdb->CDB10.TransferBlocksMsb);
                readLength *= bytesPerBlock;

                //
                // Make sure the buffer length input can hold all the data
                // that the caller says to read.....
                //
                if(readLength < PSrb->DataTransferLength) {
                    OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_SRB_USER,
				        ("SCSIOP_WRITE: readLength 0x%x< PSrb->DataTransferLength 0x%x\n",
                        readLength,PSrb->DataTransferLength));
                    PSrb->SrbStatus = SRB_STATUS_ABORTED;
                    status = STATUS_BUFFER_TOO_SMALL;
                } else {
                    //
                    // Issue the read to ScsiPortUser.
                    //
				    PMDL readMdl = OsrSpGetSrbMdl(pIInfo->OsrSPLocalHandle,PSrb);
				    if(!readMdl) {
					    status = STATUS_INSUFFICIENT_RESOURCES;
					    PSrb->SrbStatus = SRB_STATUS_ABORTED;
				    } else {
					    status = OsrUserReadData(pIInfo,PSrb,readMdl,startingLbn,readLength,&bytesRead);
				    }
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {

                status = GetExceptionCode();
                OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_SRB_USER,
					("SCSIOP_READ: Read Failure %x\n",status));

            }

            //
            // Oh, the user did not want to complete the request, but pended it.  We'll
            // honor it and get out of here.  It is up to the user to complete the request
            // later on.
            //

            if(status == STATUS_PENDING) {

                return status;

            }


            if(!NT_SUCCESS(status)) {
                OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_SRB_USER,
				    ("SCSIOP_READ: Read Failure %x\n",status));
            }

            }
            goto completeRequest;

        case SCSIOP_WRITE               : // 0x2A
            {
            //
            // We have received a write request.   Process the read.
            //
            ULARGE_INTEGER   startingLbn = {0,0};
            ULONG           writeLength = 0;
            PFOUR_BYTE      pTmp;
            PCDB            pReadCdb = (PCDB) &PSrb->Cdb[0];
            ULONG           bytesWritten;
            ULONG           numBlocks;
            ULONG           bytesPerBlock;

            __try {

                OsrUserGetDiskCapacity(pIInfo,&numBlocks,&bytesPerBlock);


                //
                // Convert the starting LBN back to little endian.
                // Convert the LBN to a byte offset instead of a block offset.
                //

                REVERSE_BYTES(&startingLbn.LowPart,&pReadCdb->CDB10.LogicalBlockByte0)
                startingLbn.QuadPart *= bytesPerBlock;
            
                //
                // Convert the read length back to little endian
                //

                REVERSE_2BYTES(&writeLength,&pReadCdb->CDB10.TransferBlocksMsb);
                writeLength *= bytesPerBlock;

                if(writeLength < PSrb->DataTransferLength) {
                    OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_SRB_USER,
				        ("SCSIOP_WRITE: writeLength 0x%x< PSrb->DataTransferLength 0x%x\n",writeLength,PSrb->DataTransferLength));
                    PSrb->SrbStatus = SRB_STATUS_ABORTED;
                    status = STATUS_BUFFER_TOO_SMALL;
                } else {
                    //
                    // Issue the write to ScsiPortUser.
                    //
				    PMDL writeMdl = OsrSpGetSrbMdl(pIInfo->OsrSPLocalHandle,PSrb);
				    if(!writeMdl) {
					    status = STATUS_INSUFFICIENT_RESOURCES;
					    PSrb->SrbStatus = SRB_STATUS_ABORTED;
				    } else {
					    status = OsrUserWriteData(pIInfo,PSrb,writeMdl,startingLbn,writeLength,&bytesWritten);
				    }
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                status = GetExceptionCode();
            }

            //
            // Oh, the user did not want to complete the request, but pended it.  We'll
            // honor it and get out of here.  It is up to the user to complete the request
            // later on.
            //

            if(status == STATUS_PENDING) {
                return status;
            }

            if(!NT_SUCCESS(status)) {
                OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_SRB_USER,
				    ("SCSIOP_WRITE: Write Failure %x\n",status));

                }
            }
            goto completeRequest;

        case SCSIOP_TEST_UNIT_READY     : // 0x00
            {
            //
            // If any asks us if we're ready, we always say
            // yes, because we're nice guys... Really..
            //
            PSrb->SrbStatus = SRB_STATUS_SUCCESS;
            status = STATUS_SUCCESS;
            }
            goto completeRequest;

        //case SCSIOP_REZERO_UNIT         : // 0x01
        case SCSIOP_REWIND              : // 0x01
            status = ProcessScsiCommandError(PSrb);
            goto completeRequest;

        case SCSIOP_REQUEST_BLOCK_ADDR  : // 0x02
            status = ProcessScsiCommandError(PSrb);
            goto completeRequest;

        case SCSIOP_REQUEST_SENSE       : // 0x03
            status = ProcessScsiCommandError(PSrb);
            goto completeRequest;

        case SCSIOP_FORMAT_UNIT         : // 0x04
            {
            OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB_USER,
				("SCSIOP_FORMAT_UNIT: Success\n"));
            PSrb->SrbStatus = SRB_STATUS_SUCCESS;
            status = STATUS_SUCCESS;
            }
            goto completeRequest;

        case SCSIOP_READ_BLOCK_LIMITS   : // 0x05
            status = ProcessScsiCommandError(PSrb);
            goto completeRequest;

        case SCSIOP_REASSIGN_BLOCKS     : // 0x07
        //case SCSIOP_INIT_ELEMENT_STATUS : // 0x07
            status = ProcessScsiCommandError(PSrb);
            goto completeRequest;

        //case SCSIOP_READ6               : // 0x08
        case SCSIOP_RECEIVE             : // 0x08
            status = ProcessScsiCommandError(PSrb);
            goto completeRequest;

        //case SCSIOP_WRITE6              : // 0x0A
        //case SCSIOP_PRINT               : // 0x0A
        case SCSIOP_SEND                : // 0x0A
            status = ProcessScsiCommandError(PSrb);
            goto completeRequest;

        //case SCSIOP_SEEK6               : // 0x0B
        case SCSIOP_TRACK_SELECT        : // 0x0B
        //case SCSIOP_SLEW_PRINT          : // 0x0B
            status = ProcessScsiCommandError(PSrb);
            goto completeRequest;

        case SCSIOP_SEEK_BLOCK          : // 0x0C
        case SCSIOP_PARTITION           : // 0x0D
        case SCSIOP_READ_REVERSE        : // 0x0F
            status = ProcessScsiCommandError(PSrb);
            goto completeRequest;

        //case SCSIOP_WRITE_FILEMARKS     : // 0x10
        case SCSIOP_FLUSH_BUFFER        : // 0x10
            status = ProcessScsiCommandError(PSrb);
            goto completeRequest;

        case SCSIOP_INQUIRY             : {// 0x12
            PCDB                    pCdb = (PCDB) &PSrb->Cdb;
            PUCHAR                  pBuffer = (PUCHAR) OsrSpGetSrbDataAddress(pIInfo->OsrSPLocalHandle,PSrb);
            PINQUIRYDATA            pInquiryData;

            if(!pBuffer || PSrb->DataTransferLength < INQUIRYDATABUFFERSIZE) {
                status = STATUS_INSUFFICIENT_RESOURCES;
                PSrb->SrbStatus = SRB_STATUS_ERROR;
                goto completeRequest;
            }

            //
            // We don't support this, so abort the request.
            //
            if(pCdb->CDB6INQUIRY3.EnableVitalProductData) {
                OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_SRB_USER,
				    ("SCSIOP_INQUIRY: EVPD bit set, not supported.\n"));
                status = ProcessScsiCommandError(PSrb);
                goto completeRequest;
            }

            //
            // If the evpd bit is not set, PageCode better be 0.
            //
            if(pCdb->CDB6INQUIRY3.PageCode) {
                OsrTracePrint(TRACE_LEVEL_ERROR,OSRVMINIPT_DEBUG_SRB_USER,
				    ("SCSIOP_INQUIRY: EVPD bit not set, PageCode not 0. Error.\n"));
                status = ProcessScsiCommandError(PSrb);
                goto completeRequest;
            }

            pInquiryData = (PINQUIRYDATA) pBuffer;

            //
            // Zero out the buffer before filling it in.
            //
            RtlZeroMemory(pInquiryData,PSrb->DataTransferLength);
        
            //
            // The media is a regular disk, return the correct information.
            //
			ASSERT(pIInfo->StorageType == OsrDisk);

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
			//
			// INQUIRYDATABUFFERSIZE does not account for us sending back this, so we won't
			// if you do, you'll overrun the buffer and get a verifier crash...
			//
#if 0
            RtlCopyMemory((PUCHAR) &pInquiryData->VendorSpecific[0],OSR_INQUIRY_VENDOR_SPECIFIC,
                                strlen(OSR_INQUIRY_VENDOR_SPECIFIC));
#endif

            status = STATUS_SUCCESS;
            //Irp->IoStatus.Information = sizeof(INQUIRYDATA);
            PSrb->SrbStatus = SRB_STATUS_SUCCESS;
            PSrb->DataTransferLength = INQUIRYDATABUFFERSIZE;

            goto completeRequest;
            }

        case SCSIOP_SPACE               : // 0x11
        case SCSIOP_VERIFY6             : // 0x13
        case SCSIOP_RECOVER_BUF_DATA    : // 0x14
        case SCSIOP_MODE_SELECT         : // 0x15
            status = ProcessScsiCommandError(PSrb);
            goto completeRequest;

        case SCSIOP_RESERVE_UNIT        : // 0x16
            DbgPrint("Received SCSIOP_RESERVE_UNIT.\n");
            status = ProcessScsiCommandError(PSrb);
            goto completeRequest;

        case SCSIOP_RELEASE_UNIT        : // 0x17
            status = ProcessScsiCommandError(PSrb);
            goto completeRequest;

        case SCSIOP_COPY                : // 0x18
        case SCSIOP_ERASE               : // 0x19
            status = ProcessScsiCommandError(PSrb);
            goto completeRequest;

        case SCSIOP_MODE_SENSE          : // 0x1A
            {
                // 
                // We have received a MODE_SENSE command.  We need to 
                // jury rig something up here....  We're returning
                // the bare MINIMUM (as I know it now) information 
                // required.  If we need more we'll add it here.
                //

                PCDB                    pCdb = (PCDB) &PSrb->Cdb;
                PMODE_PARAMETER_HEADER  pModeHeader;
                PUCHAR                  pBuffer = (PUCHAR) 
											OsrSpGetSrbDataAddress(pIInfo->OsrSPLocalHandle,PSrb);

                if(!pBuffer) {
                    status = STATUS_INSUFFICIENT_RESOURCES;
                    PSrb->SrbStatus = SRB_STATUS_ERROR;
                    goto completeRequest;
                }

                pModeHeader = (PMODE_PARAMETER_HEADER) pBuffer;
//              PrintModeSense(SCSIOP_MODE_SENSE,(PCDB) &PSrb->Cdb);

                RtlZeroMemory(pModeHeader,PSrb->DataTransferLength);

                switch(pCdb->MODE_SENSE.PageCode) {

                    case MODE_SENSE_CURRENT_VALUES:
                        {

                        pModeHeader->ModeDataLength = sizeof(MODE_PARAMETER_HEADER) + 
							sizeof(MODE_PARAMETER_BLOCK);
                        pModeHeader->MediumType = 0;

                        __try {
                            if(OsrUserIsDeviceReadOnly(pIInfo)) {
                                pModeHeader->DeviceSpecificParameter = MODE_DSP_WRITE_PROTECT; // readonly device
                            } else {
                                pModeHeader->DeviceSpecificParameter = 0;  // Writeable Device
                            }
                        } __except(EXCEPTION_EXECUTE_HANDLER) {
                            status = GetExceptionCode();
                            //Irp->IoStatus.Information = 0;
                            PSrb->SrbStatus = SRB_STATUS_ERROR;
                            goto completeRequest;
                        }

                        pModeHeader->BlockDescriptorLength = sizeof(MODE_PARAMETER_BLOCK);

                        PMODE_PARAMETER_BLOCK pModeBlock = (PMODE_PARAMETER_BLOCK) 
							(pBuffer + sizeof(MODE_PARAMETER_HEADER));

                        RtlZeroMemory(pModeBlock,sizeof(MODE_PARAMETER_BLOCK));

                        }
                        break;

                    case MODE_PAGE_CAPABILITIES: 
                    default:
                        OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB_USER,
							("MODE_SENSE: Received request for unhandled"
							" mode page %d.\n",pCdb->MODE_SENSE.PageCode));
                        pModeHeader->ModeDataLength = sizeof(MODE_PARAMETER_HEADER)-
                            sizeof(pModeHeader->ModeDataLength);
                        pModeHeader->MediumType = 0;

                        __try {
                            if(OsrUserIsDeviceReadOnly(pIInfo)) {
                                pModeHeader->DeviceSpecificParameter = MODE_DSP_WRITE_PROTECT; // readonly device
                            } else {
                                pModeHeader->DeviceSpecificParameter = 0;  // Writeable Device
                            }
                        } __except(EXCEPTION_EXECUTE_HANDLER) {
                            NTSTATUS status = GetExceptionCode();
                            //Irp->IoStatus.Information = 0;
                            PSrb->SrbStatus = SRB_STATUS_ERROR;
                            goto completeRequest;
                        }
                        pModeHeader->BlockDescriptorLength = 0;
                        break;

                }

                status = STATUS_SUCCESS;
                PSrb->SrbStatus = SRB_STATUS_SUCCESS;
                goto completeRequest;
            }
            break;

        case SCSIOP_START_STOP_UNIT     : // 0x1B
        //case SCSIOP_STOP_PRINT          : // 0x1B
        //case SCSIOP_LOAD_UNLOAD         : // 0x1B
            status = ProcessScsiCommandError(PSrb);
            goto completeRequest;

        case SCSIOP_RECEIVE_DIAGNOSTIC  : // 0x1C
        case SCSIOP_SEND_DIAGNOSTIC     : // 0x1D
            status = ProcessScsiCommandError(PSrb);
            goto completeRequest;

        case SCSIOP_MEDIUM_REMOVAL      : // 0x1E
            status = ProcessScsiCommandError(PSrb);
            goto completeRequest;
            
        case SCSIOP_READ_FORMATTED_CAPACITY : // 0x23
            status = ProcessScsiCommandError(PSrb);
            goto completeRequest;

        case SCSIOP_READ_CAPACITY       : // 0x25
            {
                ULONG numBlocks;
                ULONG bytesPerBlock;

                //
                // Someone has asked us to read the disk capacity of the device,
                // so here we need to return to the caller the information about
                // the SPECIAL disk we represent.   Sooo here we go.
                //

                PREAD_CAPACITY_DATA pCapacityData = (PREAD_CAPACITY_DATA) PSrb->DataBuffer;

                RtlZeroMemory(pCapacityData,PSrb->DataTransferLength);

                __try {
                    OsrUserGetDiskCapacity(pIInfo,&numBlocks,&bytesPerBlock);
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    status = GetExceptionCode();
                    PSrb->SrbStatus = SRB_STATUS_ERROR;
                    goto completeRequest;
                }

                REVERSE_BYTES(&pCapacityData->LogicalBlockAddress,&numBlocks);
                
                REVERSE_BYTES(&pCapacityData->BytesPerBlock,&bytesPerBlock);

                OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_SRB_USER,
					("SCSIOP_READ_CAPACITY: Logical Block Address %d. Bytes Per Block %d\n",
                                numBlocks,bytesPerBlock));
                
                //
                // Set status in Irp and in the SRB to indicate that the function was successful.
                //

                status = STATUS_SUCCESS;
                PSrb->SrbStatus = SRB_STATUS_SUCCESS;

                //
                // Complete request at raised IRQ.
                //

                goto completeRequest;

            }
            break;

        case SCSIOP_SEEK                : // 0x2B
        //case SCSIOP_LOCATE              : // 0x2B
        //case SCSIOP_POSITION_TO_ELEMENT : // 0x2B
            status = ProcessScsiCommandError(PSrb);
            goto completeRequest;

        case SCSIOP_WRITE_VERIFY        : // 0x2E
            status = ProcessScsiCommandError(PSrb);
            goto completeRequest;

        case SCSIOP_VERIFY              : // 0x2F
            {
            //
            // If any asks us if we're ready, we always say
            // yes, because we're nice guys... Really..
            //

            PSrb->SrbStatus = SRB_STATUS_SUCCESS;
            status = STATUS_SUCCESS;
            goto completeRequest;
            }
            break;

        case SCSIOP_SEARCH_DATA_HIGH    : // 0x30
        case SCSIOP_SEARCH_DATA_EQUAL   : // 0x31
        case SCSIOP_SEARCH_DATA_LOW     : // 0x32
        case SCSIOP_SET_LIMITS          : // 0x33
        case SCSIOP_READ_POSITION       : // 0x34
        case SCSIOP_SYNCHRONIZE_CACHE   : // 0x35
        case SCSIOP_COMPARE             : // 0x39
        case SCSIOP_COPY_COMPARE        : // 0x3A
        case SCSIOP_WRITE_DATA_BUFF     : // 0x3B
        case SCSIOP_READ_DATA_BUFF      : // 0x3C
        case SCSIOP_CHANGE_DEFINITION   : // 0x40
        case SCSIOP_READ_SUB_CHANNEL    : // 0x42
            status = ProcessScsiCommandError(PSrb);
            goto completeRequest;

        case SCSIOP_READ_TOC:             // 0x43
        case SCSIOP_READ_HEADER         : // 0x44
        case SCSIOP_PLAY_AUDIO          : // 0x45
        case SCSIOP_PLAY_AUDIO_MSF      : // 0x47
        case SCSIOP_PLAY_TRACK_INDEX    : // 0x48
        case SCSIOP_PLAY_TRACK_RELATIVE : // 0x49
        case SCSIOP_PAUSE_RESUME        : // 0x4B
        case SCSIOP_LOG_SELECT          : // 0x4C
        case SCSIOP_LOG_SENSE           : // 0x4D
        case SCSIOP_STOP_PLAY_SCAN      : // 0x4E
        case SCSIOP_READ_DISK_INFORMATION : // 0x51
        case SCSIOP_READ_TRACK_INFORMATION : // 0x52
        case SCSIOP_MODE_SELECT10       : // 0x55
            status = ProcessScsiCommandError(PSrb);
            goto completeRequest;

        case SCSIOP_MODE_SENSE10        : // 0x5A
            status = ProcessScsiCommandError(PSrb);
            goto completeRequest;

		case SCSIOP_REPORT_LUNS         : // 0xA0
            status = ProcessScsiCommandError(PSrb);
			goto completeRequest;

        case SCSIOP_SEND_KEY            : // 0xA3
        case SCSIOP_REPORT_KEY          : // 0xA4
        case SCSIOP_MOVE_MEDIUM         : // 0xA5
            status = ProcessScsiCommandError(PSrb);
            goto completeRequest;

        //case SCSIOP_LOAD_UNLOAD_SLOT    : // 0xA6
        case SCSIOP_EXCHANGE_MEDIUM     : // 0xA6
            status = ProcessScsiCommandError(PSrb);
            goto completeRequest;

        case SCSIOP_SET_READ_AHEAD      : // 0xA7
        case SCSIOP_READ_DVD_STRUCTURE  : // 0xAD
        case SCSIOP_REQUEST_VOL_ELEMENT : // 0xB5
        case SCSIOP_SEND_VOLUME_TAG     : // 0xB6
        case SCSIOP_READ_ELEMENT_STATUS : // 0xB8
        case SCSIOP_READ_CD_MSF         : // 0xB9
        case SCSIOP_SCAN_CD             : // 0xBA
        case SCSIOP_PLAY_CD             : // 0xBC
        case SCSIOP_MECHANISM_STATUS    : // 0xBD
        case SCSIOP_READ_CD             : // 0xBE
        case SCSIOP_INIT_ELEMENT_RANGE  : // 0xE7
            status = ProcessScsiCommandError(PSrb);
            goto completeRequest;

        default:
            status = ProcessScsiCommandError(PSrb);
            goto completeRequest;
        };


completeRequest:

    return status;

}


///////////////////////////////////////////////////////////////////////////////
//
//  OsrUserIsDeviceReadOnly
//
//      Query to see if disk is read only
//
//  INPUTS:
//
//      UserLocalInfo - User Local Data Pointer.
//
//  OUTPUTS:
//
//      None.
//
//  RETURNS:
//
//      TRUE, if read-only, FALSE otherwise.
//
//  IRQL:
//
//      This routine is called at any IRQL < DISPATCH_LEVEL.
//
//  NOTES:
//
///////////////////////////////////////////////////////////////////////////////
BOOLEAN  OsrUserIsDeviceReadOnly(IN PVOID UserLocalInfoHandle)
{
    PUSER_INSTANCE_INFORMATION pIInfo = (PUSER_INSTANCE_INFORMATION) UserLocalInfoHandle;

    USER_INSTANCE_VALID(pIInfo);

    PCONNECTION_LIST_ENTRY  pConnectionInformation = pIInfo->ConnectionInformation;

    return FALSE;
}


///////////////////////////////////////////////////////////////////////////////
//
//  OsrUserGetDiskCapacity
//
//      Returns the capacity of the disk.
//
//  INPUTS:
//
//      UserLocalInfo - User Local Data Pointer.
//
//  OUTPUTS:
//
//      PNumberOfBlocks - address of location to return number of blocks on this disk.
//      PBlockSize - address of location to return size of each block.
//
//  RETURNS:
//
//      None.
//
//  IRQL:
//
//      This routine is called at any IRQL < DISPATCH_LEVEL.
//
//  NOTES:
//
///////////////////////////////////////////////////////////////////////////////
VOID OsrUserGetDiskCapacity(IN PVOID UserLocalInfoHandle,ULONG* PNumberOfBlocks,ULONG* PBlockSize)
{
    PUSER_INSTANCE_INFORMATION pIInfo = (PUSER_INSTANCE_INFORMATION) UserLocalInfoHandle;
    PCONNECTION_LIST_ENTRY  pConnectionInformation = pIInfo->ConnectionInformation;
    ULONG length;
    ULONG size;

    USER_INSTANCE_VALID(pIInfo);

	ASSERT(pIInfo->StorageType == OsrDisk);
	length = (ULONG) ((pIInfo->ConnectionInformation->DiskSize
										/LOGICAL_BLOCK_SIZE)-1) & 0xFFFFFF00;
	size = LOGICAL_BLOCK_SIZE;

    *PNumberOfBlocks = length;
    *PBlockSize = size;

}


///////////////////////////////////////////////////////////////////////////////
//
//  OsrUserLocalShutdownNotification
//
//      Called by the OSR SCSI Port driver when the PDO for the device has
//      received a shutdown notification....
//
//  INPUTS:
//
//      UserLocalInfo - Address of user Local Data Block.
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
//      This routine is called at any IRQL < DISPATCH_LEVEL.
//
//  NOTES:
//
//      This is where the user can do any kind of additional operations
//      necessary to make shutdown work.   
//
///////////////////////////////////////////////////////////////////////////////
VOID OsrUserLocalShutdownNotification(PVOID UserLocalInfo)
{
    PUSER_INSTANCE_INFORMATION  pIInfo = (PUSER_INSTANCE_INFORMATION) UserLocalInfo;

    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_USER,("OsrUserLocalShutdownNotification Entered.\n"));

    USER_INSTANCE_VALID(pIInfo);

    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_USER,("OsrUserLocalShutdownNotification Exited.\n"));

}


///////////////////////////////////////////////////////////////////////////////
//
//  OsrUserDeleteLocalInformation
//
//      This routine is called when the PDO for the device in question is being
//      deleted.   This gives the user the opportunity to delete the User Local
//      data that was created by the user and passed into OsrSPCreateScsiDevice.
//
//  INPUTS:
//
//      UserLocalInfo - User Local Data Pointer.
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
//    This routine is called at PASSIVE_LEVEL.
//
//  NOTES:
//
//      The memory should have been allocated out of NonPagedPool.
//
///////////////////////////////////////////////////////////////////////////////
VOID  OsrUserDeleteLocalInformation(PVOID UserLocalInfo)
{
    PUSER_INSTANCE_INFORMATION pIInfo = (PUSER_INSTANCE_INFORMATION) UserLocalInfo;

    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_USER,(__FUNCTION__": Entered\n"));

    USER_INSTANCE_VALID(pIInfo);

	if(pIInfo->PInquiryData) {
		ExFreePool(pIInfo->PInquiryData);
		pIInfo->PInquiryData = NULL;
	}

    if(pIInfo->ConnectionInformation) {
        ExFreePool(pIInfo->ConnectionInformation->DiskBaseAddress);
        pIInfo->ConnectionInformation->DiskBaseAddress = NULL;
        ExFreePool(pIInfo->ConnectionInformation);
        pIInfo->ConnectionInformation = NULL;
    }

    ExFreePool(pIInfo);

    OsrTracePrint(TRACE_LEVEL_INFORMATION,OSRVMINIPT_DEBUG_USER,(__FUNCTION__": Exit\n"));
}

