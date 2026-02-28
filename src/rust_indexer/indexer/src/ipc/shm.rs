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

    /// Read the current SHM contents, pass them to `f` which returns a new buffer, then write it back.
    /// The entire operation is performed under the mutex.
    pub fn read_modify_write<F>(&self, f: F) -> io::Result<()>
    where
        F: FnOnce(&[u8]) -> Vec<u8>,
    {
        self.mtx.lock()?;
        let new_buf = {
            let data =
                unsafe { std::slice::from_raw_parts(self.shm.as_ptr(), self.shm.user_size()) };
            f(data)
        };
        if new_buf.len() > self.shm.user_size() {
            self.mtx.unlock()?;
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!(
                    "IpcShm::read_modify_write: {} bytes > shm size {}",
                    new_buf.len(),
                    self.shm.user_size()
                ),
            ));
        }
        unsafe {
            std::ptr::copy_nonoverlapping(new_buf.as_ptr(), self.shm.as_mut_ptr(), new_buf.len());
        }
        self.mtx.unlock()?;
        Ok(())
    }

    /// Read current SHM bytes, let `f` decide whether to write replacement bytes,
    /// and return a caller-defined result — all under one mutex lock.
    pub fn read_modify_write_with_result<F, R>(&self, f: F) -> io::Result<R>
    where
        F: FnOnce(&[u8]) -> io::Result<(Option<Vec<u8>>, R)>,
    {
        self.mtx.lock()?;

        let outcome = {
            let data =
                unsafe { std::slice::from_raw_parts(self.shm.as_ptr(), self.shm.user_size()) };
            f(data)
        };

        let (new_buf, result) = match outcome {
            Ok(v) => v,
            Err(err) => {
                self.mtx.unlock()?;
                return Err(err);
            }
        };

        if let Some(buf) = new_buf {
            if buf.len() > self.shm.user_size() {
                self.mtx.unlock()?;
                return Err(io::Error::new(
                    io::ErrorKind::InvalidInput,
                    format!(
                        "IpcShm::read_modify_write_with_result: {} bytes > shm size {}",
                        buf.len(),
                        self.shm.user_size()
                    ),
                ));
            }
            unsafe {
                std::ptr::copy_nonoverlapping(buf.as_ptr(), self.shm.as_mut_ptr(), buf.len());
            }
        }

        self.mtx.unlock()?;
        Ok(result)
    }
}

/// Returns true when the raw SHM bytes represent an "empty" slot.
pub fn is_empty(data: &[u8]) -> bool {
    data.len() < 4 || data[..4] == [0, 0, 0, 0]
}
