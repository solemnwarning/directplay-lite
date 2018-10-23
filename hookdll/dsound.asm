.586
.MODEL FLAT, C

EXTERN find_sym@8: PROC

.DATA

dll_name db "dsound.dll", 0

.DATA

DirectSoundCreate_sym  db "DirectSoundCreate", 0
DirectSoundCreate_addr dd 0

.CODE

PUBLIC DirectSoundCreate
DirectSoundCreate PROC
	cmp   DirectSoundCreate_addr, 0
	jne   DirectSoundCreate_jmp
	push  offset DirectSoundCreate_sym
	push  offset dll_name
	call  find_sym@8
	mov   DirectSoundCreate_addr, eax
	DirectSoundCreate_jmp:
	jmp [DirectSoundCreate_addr]
DirectSoundCreate ENDP

.DATA

DirectSoundEnumerateA_sym  db "DirectSoundEnumerateA", 0
DirectSoundEnumerateA_addr dd 0

.CODE

PUBLIC DirectSoundEnumerateA
DirectSoundEnumerateA PROC
	cmp   DirectSoundEnumerateA_addr, 0
	jne   DirectSoundEnumerateA_jmp
	push  offset DirectSoundEnumerateA_sym
	push  offset dll_name
	call  find_sym@8
	mov   DirectSoundEnumerateA_addr, eax
	DirectSoundEnumerateA_jmp:
	jmp [DirectSoundEnumerateA_addr]
DirectSoundEnumerateA ENDP

.DATA

DirectSoundEnumerateW_sym  db "DirectSoundEnumerateW", 0
DirectSoundEnumerateW_addr dd 0

.CODE

PUBLIC DirectSoundEnumerateW
DirectSoundEnumerateW PROC
	cmp   DirectSoundEnumerateW_addr, 0
	jne   DirectSoundEnumerateW_jmp
	push  offset DirectSoundEnumerateW_sym
	push  offset dll_name
	call  find_sym@8
	mov   DirectSoundEnumerateW_addr, eax
	DirectSoundEnumerateW_jmp:
	jmp [DirectSoundEnumerateW_addr]
DirectSoundEnumerateW ENDP

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

DirectSoundCaptureCreate_sym  db "DirectSoundCaptureCreate", 0
DirectSoundCaptureCreate_addr dd 0

.CODE

PUBLIC DirectSoundCaptureCreate
DirectSoundCaptureCreate PROC
	cmp   DirectSoundCaptureCreate_addr, 0
	jne   DirectSoundCaptureCreate_jmp
	push  offset DirectSoundCaptureCreate_sym
	push  offset dll_name
	call  find_sym@8
	mov   DirectSoundCaptureCreate_addr, eax
	DirectSoundCaptureCreate_jmp:
	jmp [DirectSoundCaptureCreate_addr]
DirectSoundCaptureCreate ENDP

.DATA

DirectSoundCaptureEnumerateA_sym  db "DirectSoundCaptureEnumerateA", 0
DirectSoundCaptureEnumerateA_addr dd 0

.CODE

PUBLIC DirectSoundCaptureEnumerateA
DirectSoundCaptureEnumerateA PROC
	cmp   DirectSoundCaptureEnumerateA_addr, 0
	jne   DirectSoundCaptureEnumerateA_jmp
	push  offset DirectSoundCaptureEnumerateA_sym
	push  offset dll_name
	call  find_sym@8
	mov   DirectSoundCaptureEnumerateA_addr, eax
	DirectSoundCaptureEnumerateA_jmp:
	jmp [DirectSoundCaptureEnumerateA_addr]
DirectSoundCaptureEnumerateA ENDP

.DATA

DirectSoundCaptureEnumerateW_sym  db "DirectSoundCaptureEnumerateW", 0
DirectSoundCaptureEnumerateW_addr dd 0

.CODE

PUBLIC DirectSoundCaptureEnumerateW
DirectSoundCaptureEnumerateW PROC
	cmp   DirectSoundCaptureEnumerateW_addr, 0
	jne   DirectSoundCaptureEnumerateW_jmp
	push  offset DirectSoundCaptureEnumerateW_sym
	push  offset dll_name
	call  find_sym@8
	mov   DirectSoundCaptureEnumerateW_addr, eax
	DirectSoundCaptureEnumerateW_jmp:
	jmp [DirectSoundCaptureEnumerateW_addr]
DirectSoundCaptureEnumerateW ENDP

.DATA

GetDeviceID_sym  db "GetDeviceID", 0
GetDeviceID_addr dd 0

.CODE

PUBLIC GetDeviceID
GetDeviceID PROC
	cmp   GetDeviceID_addr, 0
	jne   GetDeviceID_jmp
	push  offset GetDeviceID_sym
	push  offset dll_name
	call  find_sym@8
	mov   GetDeviceID_addr, eax
	GetDeviceID_jmp:
	jmp [GetDeviceID_addr]
GetDeviceID ENDP

.DATA

DirectSoundFullDuplexCreate_sym  db "DirectSoundFullDuplexCreate", 0
DirectSoundFullDuplexCreate_addr dd 0

.CODE

PUBLIC DirectSoundFullDuplexCreate
DirectSoundFullDuplexCreate PROC
	cmp   DirectSoundFullDuplexCreate_addr, 0
	jne   DirectSoundFullDuplexCreate_jmp
	push  offset DirectSoundFullDuplexCreate_sym
	push  offset dll_name
	call  find_sym@8
	mov   DirectSoundFullDuplexCreate_addr, eax
	DirectSoundFullDuplexCreate_jmp:
	jmp [DirectSoundFullDuplexCreate_addr]
DirectSoundFullDuplexCreate ENDP

.DATA

DirectSoundCreate8_sym  db "DirectSoundCreate8", 0
DirectSoundCreate8_addr dd 0

.CODE

PUBLIC DirectSoundCreate8
DirectSoundCreate8 PROC
	cmp   DirectSoundCreate8_addr, 0
	jne   DirectSoundCreate8_jmp
	push  offset DirectSoundCreate8_sym
	push  offset dll_name
	call  find_sym@8
	mov   DirectSoundCreate8_addr, eax
	DirectSoundCreate8_jmp:
	jmp [DirectSoundCreate8_addr]
DirectSoundCreate8 ENDP

.DATA

DirectSoundCaptureCreate8_sym  db "DirectSoundCaptureCreate8", 0
DirectSoundCaptureCreate8_addr dd 0

.CODE

PUBLIC DirectSoundCaptureCreate8
DirectSoundCaptureCreate8 PROC
	cmp   DirectSoundCaptureCreate8_addr, 0
	jne   DirectSoundCaptureCreate8_jmp
	push  offset DirectSoundCaptureCreate8_sym
	push  offset dll_name
	call  find_sym@8
	mov   DirectSoundCaptureCreate8_addr, eax
	DirectSoundCaptureCreate8_jmp:
	jmp [DirectSoundCaptureCreate8_addr]
DirectSoundCaptureCreate8 ENDP

END
