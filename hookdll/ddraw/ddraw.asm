.586
.MODEL FLAT, C

EXTERN find_sym@8: PROC

.DATA

dll_name db "ddraw.dll", 0

.DATA

AcquireDDThreadLock_sym  db "AcquireDDThreadLock", 0
AcquireDDThreadLock_addr dd 0

.CODE

PUBLIC AcquireDDThreadLock
AcquireDDThreadLock PROC
	cmp   AcquireDDThreadLock_addr, 0
	jne   AcquireDDThreadLock_jmp
	push  offset AcquireDDThreadLock_sym
	push  offset dll_name
	call  find_sym@8
	mov   AcquireDDThreadLock_addr, eax
	AcquireDDThreadLock_jmp:
	jmp [AcquireDDThreadLock_addr]
AcquireDDThreadLock ENDP

.DATA

CompleteCreateSysmemSurface_sym  db "CompleteCreateSysmemSurface", 0
CompleteCreateSysmemSurface_addr dd 0

.CODE

PUBLIC CompleteCreateSysmemSurface
CompleteCreateSysmemSurface PROC
	cmp   CompleteCreateSysmemSurface_addr, 0
	jne   CompleteCreateSysmemSurface_jmp
	push  offset CompleteCreateSysmemSurface_sym
	push  offset dll_name
	call  find_sym@8
	mov   CompleteCreateSysmemSurface_addr, eax
	CompleteCreateSysmemSurface_jmp:
	jmp [CompleteCreateSysmemSurface_addr]
CompleteCreateSysmemSurface ENDP

.DATA

D3DParseUnknownCommand_sym  db "D3DParseUnknownCommand", 0
D3DParseUnknownCommand_addr dd 0

.CODE

PUBLIC D3DParseUnknownCommand
D3DParseUnknownCommand PROC
	cmp   D3DParseUnknownCommand_addr, 0
	jne   D3DParseUnknownCommand_jmp
	push  offset D3DParseUnknownCommand_sym
	push  offset dll_name
	call  find_sym@8
	mov   D3DParseUnknownCommand_addr, eax
	D3DParseUnknownCommand_jmp:
	jmp [D3DParseUnknownCommand_addr]
D3DParseUnknownCommand ENDP

.DATA

DDGetAttachedSurfaceLcl_sym  db "DDGetAttachedSurfaceLcl", 0
DDGetAttachedSurfaceLcl_addr dd 0

.CODE

PUBLIC DDGetAttachedSurfaceLcl
DDGetAttachedSurfaceLcl PROC
	cmp   DDGetAttachedSurfaceLcl_addr, 0
	jne   DDGetAttachedSurfaceLcl_jmp
	push  offset DDGetAttachedSurfaceLcl_sym
	push  offset dll_name
	call  find_sym@8
	mov   DDGetAttachedSurfaceLcl_addr, eax
	DDGetAttachedSurfaceLcl_jmp:
	jmp [DDGetAttachedSurfaceLcl_addr]
DDGetAttachedSurfaceLcl ENDP

.DATA

DDInternalLock_sym  db "DDInternalLock", 0
DDInternalLock_addr dd 0

.CODE

PUBLIC DDInternalLock
DDInternalLock PROC
	cmp   DDInternalLock_addr, 0
	jne   DDInternalLock_jmp
	push  offset DDInternalLock_sym
	push  offset dll_name
	call  find_sym@8
	mov   DDInternalLock_addr, eax
	DDInternalLock_jmp:
	jmp [DDInternalLock_addr]
DDInternalLock ENDP

.DATA

DDInternalUnlock_sym  db "DDInternalUnlock", 0
DDInternalUnlock_addr dd 0

.CODE

PUBLIC DDInternalUnlock
DDInternalUnlock PROC
	cmp   DDInternalUnlock_addr, 0
	jne   DDInternalUnlock_jmp
	push  offset DDInternalUnlock_sym
	push  offset dll_name
	call  find_sym@8
	mov   DDInternalUnlock_addr, eax
	DDInternalUnlock_jmp:
	jmp [DDInternalUnlock_addr]
DDInternalUnlock ENDP

.DATA

DirectDrawCreate_sym  db "DirectDrawCreate", 0
DirectDrawCreate_addr dd 0

.CODE

PUBLIC DirectDrawCreate
DirectDrawCreate PROC
	cmp   DirectDrawCreate_addr, 0
	jne   DirectDrawCreate_jmp
	push  offset DirectDrawCreate_sym
	push  offset dll_name
	call  find_sym@8
	mov   DirectDrawCreate_addr, eax
	DirectDrawCreate_jmp:
	jmp [DirectDrawCreate_addr]
DirectDrawCreate ENDP

.DATA

DirectDrawCreateClipper_sym  db "DirectDrawCreateClipper", 0
DirectDrawCreateClipper_addr dd 0

.CODE

PUBLIC DirectDrawCreateClipper
DirectDrawCreateClipper PROC
	cmp   DirectDrawCreateClipper_addr, 0
	jne   DirectDrawCreateClipper_jmp
	push  offset DirectDrawCreateClipper_sym
	push  offset dll_name
	call  find_sym@8
	mov   DirectDrawCreateClipper_addr, eax
	DirectDrawCreateClipper_jmp:
	jmp [DirectDrawCreateClipper_addr]
DirectDrawCreateClipper ENDP

.DATA

DirectDrawCreateEx_sym  db "DirectDrawCreateEx", 0
DirectDrawCreateEx_addr dd 0

.CODE

PUBLIC DirectDrawCreateEx
DirectDrawCreateEx PROC
	cmp   DirectDrawCreateEx_addr, 0
	jne   DirectDrawCreateEx_jmp
	push  offset DirectDrawCreateEx_sym
	push  offset dll_name
	call  find_sym@8
	mov   DirectDrawCreateEx_addr, eax
	DirectDrawCreateEx_jmp:
	jmp [DirectDrawCreateEx_addr]
DirectDrawCreateEx ENDP

.DATA

DirectDrawEnumerateA_sym  db "DirectDrawEnumerateA", 0
DirectDrawEnumerateA_addr dd 0

.CODE

PUBLIC DirectDrawEnumerateA
DirectDrawEnumerateA PROC
	cmp   DirectDrawEnumerateA_addr, 0
	jne   DirectDrawEnumerateA_jmp
	push  offset DirectDrawEnumerateA_sym
	push  offset dll_name
	call  find_sym@8
	mov   DirectDrawEnumerateA_addr, eax
	DirectDrawEnumerateA_jmp:
	jmp [DirectDrawEnumerateA_addr]
DirectDrawEnumerateA ENDP

.DATA

DirectDrawEnumerateExA_sym  db "DirectDrawEnumerateExA", 0
DirectDrawEnumerateExA_addr dd 0

.CODE

PUBLIC DirectDrawEnumerateExA
DirectDrawEnumerateExA PROC
	cmp   DirectDrawEnumerateExA_addr, 0
	jne   DirectDrawEnumerateExA_jmp
	push  offset DirectDrawEnumerateExA_sym
	push  offset dll_name
	call  find_sym@8
	mov   DirectDrawEnumerateExA_addr, eax
	DirectDrawEnumerateExA_jmp:
	jmp [DirectDrawEnumerateExA_addr]
DirectDrawEnumerateExA ENDP

.DATA

DirectDrawEnumerateExW_sym  db "DirectDrawEnumerateExW", 0
DirectDrawEnumerateExW_addr dd 0

.CODE

PUBLIC DirectDrawEnumerateExW
DirectDrawEnumerateExW PROC
	cmp   DirectDrawEnumerateExW_addr, 0
	jne   DirectDrawEnumerateExW_jmp
	push  offset DirectDrawEnumerateExW_sym
	push  offset dll_name
	call  find_sym@8
	mov   DirectDrawEnumerateExW_addr, eax
	DirectDrawEnumerateExW_jmp:
	jmp [DirectDrawEnumerateExW_addr]
DirectDrawEnumerateExW ENDP

.DATA

DirectDrawEnumerateW_sym  db "DirectDrawEnumerateW", 0
DirectDrawEnumerateW_addr dd 0

.CODE

PUBLIC DirectDrawEnumerateW
DirectDrawEnumerateW PROC
	cmp   DirectDrawEnumerateW_addr, 0
	jne   DirectDrawEnumerateW_jmp
	push  offset DirectDrawEnumerateW_sym
	push  offset dll_name
	call  find_sym@8
	mov   DirectDrawEnumerateW_addr, eax
	DirectDrawEnumerateW_jmp:
	jmp [DirectDrawEnumerateW_addr]
DirectDrawEnumerateW ENDP

.DATA

DllCanUnloadNow_sym  db "DllCanUnloadNow", 0
DllCanUnloadNow_addr dd 0

.CODE

PUBLIC DllCanUnloadNow
DllCanUnloadNow PROC
	cmp   DllCanUnloadNow_addr, 0
	jne   DllCanUnloadNow_jmp
	push  offset DllCanUnloadNow_sym
	push  offset dll_name
	call  find_sym@8
	mov   DllCanUnloadNow_addr, eax
	DllCanUnloadNow_jmp:
	jmp [DllCanUnloadNow_addr]
DllCanUnloadNow ENDP

.DATA

DllGetClassObject_sym  db "DllGetClassObject", 0
DllGetClassObject_addr dd 0

.CODE

PUBLIC DllGetClassObject
DllGetClassObject PROC
	cmp   DllGetClassObject_addr, 0
	jne   DllGetClassObject_jmp
	push  offset DllGetClassObject_sym
	push  offset dll_name
	call  find_sym@8
	mov   DllGetClassObject_addr, eax
	DllGetClassObject_jmp:
	jmp [DllGetClassObject_addr]
DllGetClassObject ENDP

.DATA

DSoundHelp_sym  db "DSoundHelp", 0
DSoundHelp_addr dd 0

.CODE

PUBLIC DSoundHelp
DSoundHelp PROC
	cmp   DSoundHelp_addr, 0
	jne   DSoundHelp_jmp
	push  offset DSoundHelp_sym
	push  offset dll_name
	call  find_sym@8
	mov   DSoundHelp_addr, eax
	DSoundHelp_jmp:
	jmp [DSoundHelp_addr]
DSoundHelp ENDP

.DATA

GetDDSurfaceLocal_sym  db "GetDDSurfaceLocal", 0
GetDDSurfaceLocal_addr dd 0

.CODE

PUBLIC GetDDSurfaceLocal
GetDDSurfaceLocal PROC
	cmp   GetDDSurfaceLocal_addr, 0
	jne   GetDDSurfaceLocal_jmp
	push  offset GetDDSurfaceLocal_sym
	push  offset dll_name
	call  find_sym@8
	mov   GetDDSurfaceLocal_addr, eax
	GetDDSurfaceLocal_jmp:
	jmp [GetDDSurfaceLocal_addr]
GetDDSurfaceLocal ENDP

.DATA

GetOLEThunkData_sym  db "GetOLEThunkData", 0
GetOLEThunkData_addr dd 0

.CODE

PUBLIC GetOLEThunkData
GetOLEThunkData PROC
	cmp   GetOLEThunkData_addr, 0
	jne   GetOLEThunkData_jmp
	push  offset GetOLEThunkData_sym
	push  offset dll_name
	call  find_sym@8
	mov   GetOLEThunkData_addr, eax
	GetOLEThunkData_jmp:
	jmp [GetOLEThunkData_addr]
GetOLEThunkData ENDP

.DATA

GetSurfaceFromDC_sym  db "GetSurfaceFromDC", 0
GetSurfaceFromDC_addr dd 0

.CODE

PUBLIC GetSurfaceFromDC
GetSurfaceFromDC PROC
	cmp   GetSurfaceFromDC_addr, 0
	jne   GetSurfaceFromDC_jmp
	push  offset GetSurfaceFromDC_sym
	push  offset dll_name
	call  find_sym@8
	mov   GetSurfaceFromDC_addr, eax
	GetSurfaceFromDC_jmp:
	jmp [GetSurfaceFromDC_addr]
GetSurfaceFromDC ENDP

.DATA

RegisterSpecialCase_sym  db "RegisterSpecialCase", 0
RegisterSpecialCase_addr dd 0

.CODE

PUBLIC RegisterSpecialCase
RegisterSpecialCase PROC
	cmp   RegisterSpecialCase_addr, 0
	jne   RegisterSpecialCase_jmp
	push  offset RegisterSpecialCase_sym
	push  offset dll_name
	call  find_sym@8
	mov   RegisterSpecialCase_addr, eax
	RegisterSpecialCase_jmp:
	jmp [RegisterSpecialCase_addr]
RegisterSpecialCase ENDP

.DATA

ReleaseDDThreadLock_sym  db "ReleaseDDThreadLock", 0
ReleaseDDThreadLock_addr dd 0

.CODE

PUBLIC ReleaseDDThreadLock
ReleaseDDThreadLock PROC
	cmp   ReleaseDDThreadLock_addr, 0
	jne   ReleaseDDThreadLock_jmp
	push  offset ReleaseDDThreadLock_sym
	push  offset dll_name
	call  find_sym@8
	mov   ReleaseDDThreadLock_addr, eax
	ReleaseDDThreadLock_jmp:
	jmp [ReleaseDDThreadLock_addr]
ReleaseDDThreadLock ENDP

.DATA

SetAppCompatData_sym  db "SetAppCompatData", 0
SetAppCompatData_addr dd 0

.CODE

PUBLIC SetAppCompatData
SetAppCompatData PROC
	cmp   SetAppCompatData_addr, 0
	jne   SetAppCompatData_jmp
	push  offset SetAppCompatData_sym
	push  offset dll_name
	call  find_sym@8
	mov   SetAppCompatData_addr, eax
	SetAppCompatData_jmp:
	jmp [SetAppCompatData_addr]
SetAppCompatData ENDP

END
