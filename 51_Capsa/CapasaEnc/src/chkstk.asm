; chkstk.asm - stack probe helper for freestanding /NODEFAULTLIB builds (x64).
; MSVC calls __chkstk with RAX = number of bytes the caller is about to
; allocate on the stack. We probe one page at a time so the OS grows the
; stack properly instead of skipping over the guard page.
.code
__chkstk proc
    push    rcx
    push    rax
    mov     rcx, rsp
    add     rcx, 8              ; rcx = top of the area the caller will use
probe:
    sub     rcx, 1000h
    sub     rax, 1000h
    test    [rcx], eax          ; touch the page (expands guard page if hit)
    cmp     rax, 1000h
    jae     probe
    pop     rax
    pop     rcx
    ret
__chkstk endp
end
