// ui_shot, USB_MSC=1: the card gate of the original's Drive mode (platforms/mk66/source/FileSystemImpl.cpp, FEATURE_USB_MSC) around the
// host file system. Host only, NOT firmware. Included by host_platform.cpp after HFileSystem.
//
// HDriveGate is what the core sees as the file system. lendToHost() (Usb::setClassState(kStorage, true), as msc_drive.cpp does on the
// MK66) closes the gate: every call the MK66 FileSystemImpl refuses is refused here with the same failure value and counted by name.
// remount() opens it again and starts a new mount: like FatFs after f_mount, every handle opened before the mode is invalid (its
// calls fail and are counted as stale). The backend (HFileSystem) counts on its own every call that reaches the host folder while the
// computer owns the card (hostp::g_driveBackend): with a working gate it stays 0, the S2 and S6 rows assert that.
// Not modelled: the block side (the computer's reads and writes are files put into the SD folder by the script, `put`, `hostrm`).
// Included inside namespace hostp (anonymous part) of host_platform.cpp: no includes here (<map> and <string> are there), the
// counters are declared in host_platform.h.

class HDriveGate : public FileSystem
{
  public:
    explicit HDriveGate(FileSystem& b) : b_(b) {}

    bool lendToHost()
    {
        if (hostp::g_driveOwned) return true;
        if (!b_.isSdCardInserted()) return false;
        hostp::g_driveRefused = 0;
        hostp::g_driveRefusedBy.clear();
        hostp::g_driveOwned = true;
        gen_++;   // FatFs unmounted: no handle of this mount is valid again
        return true;
    }
    bool hostOwned() const { return hostp::g_driveOwned; }

    bool isSdCardInserted() override { return b_.isSdCardInserted(); }
    void setMountEventCallback(CardStateChangeCallback f) override { b_.setMountEventCallback(f); }
    bool remount() override
    {
        hostp::g_driveOwned = false;
        gen_++;
        return b_.remount();
    }

    bool exists(const char* p) override { return refuse("exists") ? false : b_.exists(p); }
    bool rename(const char* a, const char* c) override { return refuse("rename") ? false : b_.rename(a, c); }
    bool mkdir(uint8_t h, const char* p, bool f) override { return refuse("mkdir") ? false : b_.mkdir(h, p, f); }
    bool remove(const char* p) override { return refuse("remove") ? false : b_.remove(p); }
    bool removeDirWithFiles(const char* p) override { return refuse("removeDirWithFiles") ? false : b_.removeDirWithFiles(p); }
    bool removeOnlyDirFiles(const char* p) override { return refuse("removeOnlyDirFiles") ? false : b_.removeOnlyDirFiles(p); }
    size_t getFileSize(const char* p) override { return refuse("getFileSize") ? 0 : b_.getFileSize(p); }
    size_t getFileSize(FileHandle h) override { return refuse("getFileSize(h)") || stale(h) ? 0 : b_.getFileSize(h); }
    size_t getFileAvailable(FileHandle h) override { return refuse("getFileAvailable") || stale(h) ? 0 : b_.getFileAvailable(h); }
    std::tuple<bool, FileHandle> openFile(const char* p, FileOpenMode m, FileHandle* fh) override
    {
        if (refuse("openFile")) return {false, fh != nullptr ? *fh : nullptr};
        auto r = b_.openFile(p, m, fh);
        if (std::get<1>(r) != nullptr) handles_[std::get<1>(r)] = gen_;
        return r;
    }
    int32_t readFile(void* d, size_t n, FileHandle h) override { return refuse("readFile") || stale(h) ? -1 : b_.readFile(d, n, h); }
    int32_t writeFile(const void* d, size_t n, FileHandle h) override { return refuse("writeFile") || stale(h) ? -1 : b_.writeFile(d, n, h); }
    bool seekFileCur(int o, FileHandle h) override { return refuse("seekFileCur") || stale(h) ? false : b_.seekFileCur(o, h); }
    bool seekFile(uint32_t p, FileHandle h) override { return refuse("seekFile") || stale(h) ? false : b_.seekFile(p, h); }
    void closeFile(FileHandle h, bool del) override
    {
        // the MK66 gives the pool slot back without touching the card. The host still closes its FILE* (a write buffer is flushed
        // into the host folder where the device drops it), outside the backend count, and the call counts as refused all the same
        refuse("closeFile");
        stale(h);
        const bool keep = hostp::g_driveOwned;
        hostp::g_driveOwned = false;
        b_.closeFile(h, del);
        hostp::g_driveOwned = keep;
        if (del) handles_.erase(h);
    }
    void endFile(FileHandle h) override { if (!refuse("endFile") && !stale(h)) b_.endFile(h); }
    bool isFileOpen(FileHandle h) override { return b_.isFileOpen(h); }
    bool enableFastSeek(FileHandle h, uint32_t* c, uint32_t n, uint32_t* d, uint32_t* s) override
    {
        return refuse("enableFastSeek") || stale(h) ? false : b_.enableFastSeek(h, c, n, d, s);
    }
    bool readSectorsAsync(uint8_t* b, uint32_t s, uint32_t n) override { return refuse("readSectorsAsync") ? false : b_.readSectorsAsync(b, s, n); }
    bool pollAsyncRead(bool* ok) override { return b_.pollAsyncRead(ok); }
    bool waitAsyncRead() override { return b_.waitAsyncRead(); }
    bool isAsyncReadBusy() override { return b_.isAsyncReadBusy(); }
    int createInstrumentsList(const char* p, char** l, uint8_t n, uint32_t* s) override
    {
        return refuse("createInstrumentsList") ? kListError : b_.createInstrumentsList(p, l, n, s);
    }
    int countContainedFiles(const char* p) override { return refuse("countContainedFiles") ? 0 : b_.countContainedFiles(p); }
    int createFilesList(const char* p, uint8_t s, char** l, uint8_t n, uint16_t m, enListFileFilter f, uint16_t k, uint16_t x, enListFileFilter g) override
    {
        return refuse("createFilesList") ? kListError : b_.createFilesList(p, s, l, n, m, f, k, x, g);
    }
    void listDirContents(const char* d, DirContentType t, std::function<bool(const char*)> f, char* b, int bs, int ml, int& c, char** fl) override
    {
        if (refuse("listDirContents")) { c = 0; return; }
        b_.listDirContents(d, t, f, b, bs, ml, c, fl);
    }
    int readNbFiles(const char* p) override { return refuse("readNbFiles") ? 0 : b_.readNbFiles(p); }
    bool nesDir_open(const char* p) override { return refuse("nesDir_open") ? false : b_.nesDir_open(p); }
    bool nesDir_readItem(char* p, uint8_t* d, uint8_t* h, uint32_t* s) override { return refuse("nesDir_readItem") ? false : b_.nesDir_readItem(p, d, h, s); }
    void nesDir_close() override { if (!refuse("nesDir_close")) b_.nesDir_close(); }

  private:
    bool refuse(const char* what)
    {
        static const bool open = getenv("UISHOT_DRIVE_NOGATE") != nullptr;   // negative control of usb_msc_sim.sh: no gate at all
        if (!hostp::g_driveOwned || open) return false;
        hostp::g_driveRefused++;
        hostp::g_driveRefusedBy[what]++;
        return true;
    }
    bool stale(FileHandle h)
    {
        auto it = handles_.find(h);
        if (it == handles_.end() || it->second == gen_) return false;
        hostp::g_driveStale++;
        return true;
    }
    FileSystem& b_;
    unsigned gen_ = 0;
    std::map<FileHandle, unsigned> handles_;
};
