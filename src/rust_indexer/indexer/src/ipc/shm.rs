// Raw shared-memory + mutex channel that mirrors IpcSharedMemory.cpp.
//
// Wire protocol:
//   - SHM region holds raw FlatBuffers bytes written directly (no extra framing).
//   - "Empty" is indicated by the first 4 bytes all being zero.
//   - A named mutex (srctrl_ipc_mtx_<name>) serialises all access.
//
// Name truncation: C++ truncates the logical name to 18 characters before
// prepending the prefixes, so we do the same.

use std::io;

use libipc::{IpcMutex, ShmHandle, ShmOpenMode};

const MEM_PREFIX: &str = "srctrl_ipc_mem_";
const MTX_PREFIX: &str = "srctrl_ipc_mtx_";
const NAME_MAX: usize = 18;

fn truncate(name: &str) -> &str {
    let end = name
        .char_indices()
        .nth(NAME_MAX)
        .map(|(i, _)| i)
        .unwrap_or(name.len());
    &name[..end]
}

pub struct IpcShm {
    shm: ShmHandle,
    mtx: IpcMutex,
}

impl IpcShm {
    pub fn open(name: &str, size: usize) -> io::Result<Self> {
        let short = truncate(name);
        let mem_name = format!("{MEM_PREFIX}{short}");
        let mtx_name = format!("{MTX_PREFIX}{short}");

        let shm = ShmHandle::acquire(&mem_name, size, ShmOpenMode::CreateOrOpen)?;
        let mtx = IpcMutex::open(&mtx_name)?;

        Ok(Self { shm, mtx })
    }

    /// Call `f` with the raw SHM bytes under the mutex.
    pub fn read_locked<F, R>(&self, f: F) -> io::Result<R>
    where
        F: FnOnce(&[u8]) -> R,
    {
        self.mtx.lock()?;
        let result = {
            let data =
                unsafe { std::slice::from_raw_parts(self.shm.as_ptr(), self.shm.user_size()) };
            f(data)
        };
        self.mtx.unlock()?;
        Ok(result)
    }

    /// Write `buf` into the SHM region under the mutex.
    pub fn write_locked(&self, buf: &[u8]) -> io::Result<()> {
        if buf.len() > self.shm.user_size() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!(
                    "IpcShm::write: {} bytes > shm size {}",
                    buf.len(),
                    self.shm.user_size()
                ),
            ));
        }
        self.mtx.lock()?;
        unsafe {
            std::ptr::copy_nonoverlapping(buf.as_ptr(), self.shm.as_mut_ptr(), buf.len());
        }
        self.mtx.unlock()?;
        Ok(())
    }

    /// Write 4 zero bytes to mark the region as empty.
    pub fn clear_locked(&self) -> io::Result<()> {
        self.write_locked(&[0u8; 4])
    }
}

/// Returns true when the raw SHM bytes represent an "empty" slot.
pub fn is_empty(data: &[u8]) -> bool {
    data.len() < 4 || data[..4] == [0, 0, 0, 0]
}
