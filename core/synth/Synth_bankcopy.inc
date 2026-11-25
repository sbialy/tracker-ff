// Synth.cpp, included where this block was: the card operations of the patch bank copy (FEATURE_SYNTH_OPEN, open_bankcopy.h)
#ifdef FEATURE_SYNTH_OPEN
namespace
{
// The card operations of the bank copy (open_bankcopy.h). A file is copied to <dst>.tmp and replaces the destination only when it is
// complete, so an SD error leaves the destination file as it was.
struct BankCopyOps
{
    FileSystem* fs;

    polysynth::FileSystem::Presence stat(const std::string& path) { return FileSystemAdapter::statPath(path.c_str()); }
    bool copyFile(const std::string& src, const std::string& dst)
    {
        const std::string tmp = dst + ".tmp";
        if (!cFileTransfer::copyFile(fs, src.c_str(), tmp.c_str())) // it deletes tmp itself when it fails
        {
            return false;
        }
        if (!FileSystemAdapter::replaceFile(fs, tmp.c_str(), dst.c_str()))
        {
            fs->remove(tmp.c_str());
            return false;
        }
        return true;
    }
    bool remove(const std::string& path) { return fs->remove(path.c_str()); }
};
} // namespace
#endif
