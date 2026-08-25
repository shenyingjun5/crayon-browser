//! Win32 FFI surface for PLT-W04.  Every `unsafe` block in the crate lives
//! here; each one carries a SAFETY comment and inputs are validated before
//! crossing the boundary.

use windows_sys::Win32::Foundation::LocalFree;
use windows_sys::Win32::Security::Cryptography::{
    CryptProtectData, CryptUnprotectData, CRYPT_INTEGER_BLOB,
};

/// Static description embedded in DPAPI-protected blobs.  Purely
/// diagnostic; carries no user data.
const DATA_DESCRIPTION: &[u16] = &[
    b'c' as u16,
    b'r' as u16,
    b'a' as u16,
    b'y' as u16,
    b'o' as u16,
    b'n' as u16,
];

fn blob(bytes: &[u8]) -> CRYPT_INTEGER_BLOB {
    CRYPT_INTEGER_BLOB {
        cbData: bytes.len().min(u32::MAX as usize) as u32,
        pbData: bytes.as_ptr().cast_mut(),
    }
}

/// Protects `plain` with DPAPI under the current user scope (no prompt,
/// no entropy).  Returns the protected bytes or fails closed.
pub(crate) fn protect(plain: &[u8]) -> Option<Vec<u8>> {
    if plain.len() > u32::MAX as usize {
        return None;
    }
    let input = blob(plain);
    let mut output = CRYPT_INTEGER_BLOB {
        cbData: 0,
        pbData: std::ptr::null_mut(),
    };
    // SAFETY: `input` points at `plain.len()` readable bytes for the call's
    // duration; `output` is an out-blob filled by DPAPI; the description
    // and optional-entropy/reserved/prompt arguments pass nulls or a
    // static wide string as the API permits.
    let ok = unsafe {
        CryptProtectData(
            &input as *const _ as *mut _,
            DATA_DESCRIPTION.as_ptr(),
            std::ptr::null(),
            std::ptr::null(),
            std::ptr::null(),
            0,
            &mut output,
        )
    };
    if ok == 0 || output.pbData.is_null() {
        return None;
    }
    // SAFETY: `output` is an initialized DPAPI out-blob from the call
    // above, not yet freed.
    let out = unsafe { take_blob(output) };
    Some(out)
}

/// Unprotects a DPAPI blob produced by [`protect`].  Returns `None` when
/// the ciphertext is corrupted or was protected for another user.
pub(crate) fn unprotect(cipher: &[u8]) -> Option<Vec<u8>> {
    if cipher.len() > u32::MAX as usize || cipher.is_empty() {
        return None;
    }
    let input = blob(cipher);
    let mut output = CRYPT_INTEGER_BLOB {
        cbData: 0,
        pbData: std::ptr::null_mut(),
    };
    // SAFETY: `input` points at `cipher.len()` readable bytes; `output`
    // receives the plaintext blob owned by the allocator on success.
    let ok = unsafe {
        CryptUnprotectData(
            &input as *const _ as *mut _,
            std::ptr::null_mut(),
            std::ptr::null(),
            std::ptr::null(),
            std::ptr::null(),
            0,
            &mut output,
        )
    };
    if ok == 0 || output.pbData.is_null() {
        return None;
    }
    // SAFETY: `output` is an initialized DPAPI out-blob from the call
    // above, not yet freed.
    Some(unsafe { take_blob(output) })
}

/// Copies a successful DPAPI out-blob into an owned vector and releases
/// the DPAPI-owned buffer.
///
/// # Safety
/// `blob` must be an initialized out parameter from `CryptProtectData` or
/// `CryptUnprotectData` whose `pbData` was allocated by DPAPI and has not
/// been freed yet.
unsafe fn take_blob(blob: CRYPT_INTEGER_BLOB) -> Vec<u8> {
    let bytes = std::slice::from_raw_parts(blob.pbData, blob.cbData as usize).to_vec();
    // SAFETY: DPAPI allocates `pbData` with LocalAlloc semantics and the
    // documented ownership contract requires exactly one LocalFree here;
    // the slice above was copied first.
    unsafe {
        LocalFree(blob.pbData.cast());
    }
    bytes
}

// ---------------------------------------------------------------------------
// SID helpers

use windows_sys::Win32::Security::{CopySid, EqualSid, GetLengthSid};

/// Returns the byte length of a valid SID, or 0.
///
/// # Safety
/// `sid` must point at a readable SID.
pub(crate) unsafe fn get_length_sid(sid: windows_sys::Win32::Security::PSID) -> usize {
    // SAFETY: caller guarantees SID validity.
    unsafe { GetLengthSid(sid) as usize }
}

/// Copies a SID into a caller-provided buffer.
///
/// # Safety
/// `destination` must be writable for `length` bytes and u32-aligned.
pub(crate) unsafe fn copy_sid(
    destination: windows_sys::Win32::Security::PSID,
    length: usize,
    source: windows_sys::Win32::Security::PSID,
) -> i32 {
    // SAFETY: caller guarantees buffer size/alignment and source validity.
    unsafe { CopySid(length as u32, destination, source) }
}

/// Compares two SIDs for equality.
///
/// # Safety
/// Both arguments must reference valid SIDs.
pub(crate) unsafe fn equal_sid(
    left: windows_sys::Win32::Security::PSID,
    right: windows_sys::Win32::Security::PSID,
) -> i32 {
    // SAFETY: caller guarantees both SIDs are valid.
    unsafe { EqualSid(left, right) }
}

/// Releases an OS-allocated local memory block.
///
/// # Safety
/// `handle` must originate from an OS allocation using local-memory
/// ownership (DPAPI blobs, converted security descriptors).
pub(crate) unsafe fn local_free(handle: *mut core::ffi::c_void) {
    // SAFETY: single free of an OS-owned allocation.
    unsafe { LocalFree(handle.cast()) };
}
