/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:set ts=4 sw=4 sts=4 ci et: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/mozPoisonWrite.h"
#include "mozilla/Util.h"
#include "nsTraceRefcntImpl.h"
#include "mozilla/Assertions.h"
#include "mozilla/Scoped.h"
#include "mozilla/Mutex.h"
#include "mozilla/Telemetry.h"
#include "nsStackWalk.h"
#include "nsPrintfCString.h"
#include "mach_override.h"
#include "prio.h"
#include "plstr.h"
#include "nsCOMPtr.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsDirectoryServiceUtils.h"
#include <sys/stat.h>
#include <vector>
#include <algorithm>
#include <string.h>
#include <sys/uio.h>
#include <aio.h>
#include <dlfcn.h>

namespace {
using namespace mozilla;

struct FuncData {
    const char *Name;      // Name of the function for the ones we use dlsym
    const void *Wrapper;   // The function that we will replace 'Function' with
    void *Function;        // The function that will be replaced with 'Wrapper'
    void *Buffer;          // Will point to the jump buffer that lets us call
                           // 'Function' after it has been replaced.
};


// FIXME: duplicated code. The HangMonitor could also report processed addresses,
// this class should be moved somewhere it can be shared.
class ProcessedStack
{
public:
    ProcessedStack() : mProcessed(false)
    {
    }
    void Reserve(unsigned int n)
    {
        mStack.reserve(n);
    }
    size_t GetStackSize()
    {
        return mStack.size();
    }
    struct ProcessedStackFrame
    {
        uintptr_t mOffset;
        uint16_t mModIndex;
    };
    ProcessedStackFrame GetFrame(unsigned aIndex)
    {
        const StackFrame &Frame = mStack[aIndex];
        ProcessedStackFrame Ret = { Frame.mPC, Frame.mModIndex };
        return Ret;
    }
    size_t GetNumModules()
    {
        MOZ_ASSERT(mProcessed);
        return mModules.GetSize();
    }
    const char *GetModuleName(unsigned aIndex)
    {
        MOZ_ASSERT(mProcessed);
        return mModules.GetEntry(aIndex).GetName();
    }
    void AddStackFrame(uintptr_t aPC)
    {
        MOZ_ASSERT(!mProcessed);
        StackFrame Frame = {aPC, static_cast<uint16_t>(mStack.size()),
                            std::numeric_limits<uint16_t>::max()};
        mStack.push_back(Frame);
    }
    void Process()
    {
        mProcessed = true;
        mModules = SharedLibraryInfo::GetInfoForSelf();
        mModules.SortByAddress();

        // Remove all modules not referenced by a PC on the stack
        std::sort(mStack.begin(), mStack.end(), CompareByPC);

        size_t moduleIndex = 0;
        size_t stackIndex = 0;
        size_t stackSize = mStack.size();

        while (moduleIndex < mModules.GetSize()) {
            SharedLibrary& module = mModules.GetEntry(moduleIndex);
            uintptr_t moduleStart = module.GetStart();
            uintptr_t moduleEnd = module.GetEnd() - 1;
            // the interval is [moduleStart, moduleEnd)

            bool moduleReferenced = false;
            for (;stackIndex < stackSize; ++stackIndex) {
                uintptr_t pc = mStack[stackIndex].mPC;
                if (pc >= moduleEnd)
                    break;

                if (pc >= moduleStart) {
                    // If the current PC is within the current module, mark
                    // module as used
                    moduleReferenced = true;
                    mStack[stackIndex].mPC -= moduleStart;
                    mStack[stackIndex].mModIndex = moduleIndex;
                } else {
                    // PC does not belong to any module. It is probably from
                    // the JIT. Use a fixed mPC so that we don't get different
                    // stacks on different runs.
                    mStack[stackIndex].mPC =
                        std::numeric_limits<uintptr_t>::max();
                }
            }

            if (moduleReferenced) {
                ++moduleIndex;
            } else {
                // Remove module if no PCs within its address range
                mModules.RemoveEntries(moduleIndex, moduleIndex + 1);
            }
        }

        for (;stackIndex < stackSize; ++stackIndex) {
            // These PCs are past the last module.
            mStack[stackIndex].mPC = std::numeric_limits<uintptr_t>::max();
        }

        std::sort(mStack.begin(), mStack.end(), CompareByIndex);
    }

private:
  struct StackFrame
  {
      uintptr_t mPC;      // The program counter at this position in the call stack.
      uint16_t mIndex;    // The number of this frame in the call stack.
      uint16_t mModIndex; // The index of module that has this program counter.
  };
  static bool CompareByPC(const StackFrame &a, const StackFrame &b)
  {
      return a.mPC < b.mPC;
  }
  static bool CompareByIndex(const StackFrame &a, const StackFrame &b)
  {
    return a.mIndex < b.mIndex;
  }
  SharedLibraryInfo mModules;
  std::vector<StackFrame> mStack;
  bool mProcessed;
};

void RecordStackWalker(void *aPC, void *aSP, void *aClosure)
{
    ProcessedStack *stack = static_cast<ProcessedStack*>(aClosure);
    stack->AddStackFrame(reinterpret_cast<uintptr_t>(aPC));
}

char *sProfileDirectory = NULL;

bool ValidWriteAssert(bool ok)
{
    // On a debug build, just crash.
    MOZ_ASSERT(ok);

    if (ok || !sProfileDirectory || !Telemetry::CanRecord())
        return ok;

    // Write the stack and loaded libraries to a file. We can get here
    // concurrently from many writes, so we use multiple temporary files.
    ProcessedStack stack;
    NS_StackWalk(RecordStackWalker, 0, reinterpret_cast<void*>(&stack), 0);
    stack.Process();

    nsPrintfCString nameAux("%s%s", sProfileDirectory,
                            "/Telemetry.LateWriteTmpXXXXXX");
    char *name;
    nameAux.GetMutableData(&name);
    int fd = mkstemp(name);
    MozillaRegisterDebugFD(fd);
    FILE *f = fdopen(fd, "w");

    size_t numModules = stack.GetNumModules();
    fprintf(f, "%zu\n", numModules);
    for (int i = 0; i < numModules; ++i) {
        const char *name = stack.GetModuleName(i);
        fprintf(f, "%s\n", name ? name : "");
    }

    size_t numFrames = stack.GetStackSize();
    fprintf(f, "%zu\n", numFrames);
    for (size_t i = 0; i < numFrames; ++i) {
        const ProcessedStack::ProcessedStackFrame &frame = stack.GetFrame(i);
        // NOTE: We write the offsets, while the atos tool expects a value with
        // the virtual address added. For example, running otool -l on the the firefox
        // binary shows
        //      cmd LC_SEGMENT_64
        //      cmdsize 632
        //      segname __TEXT
        //      vmaddr 0x0000000100000000
        // so to print the line matching the offset 123 one has to run
        // atos -o firefox 0x100000123.
        fprintf(f, "%d %jx\n", frame.mModIndex, frame.mOffset);
    }

    fflush(f);
    MozillaUnRegisterDebugFD(fd);
    fclose(f);

    // FIXME: For now we just record the last write. We should write the files
    // to filenames that include the md5. That will provide a simple early
    // deduplication if the same bug is found in multiple runs.
    nsPrintfCString finalName("%s%s", sProfileDirectory,
                              "/Telemetry.LateWriteFinal-last");
    PR_Delete(finalName.get());
    PR_Rename(name, finalName.get());
    return false;
}

// Wrap aio_write. We have not seen it before, so just assert/report it.
typedef ssize_t (*aio_write_t)(struct aiocb *aiocbp);
ssize_t wrap_aio_write(struct aiocb *aiocbp);
FuncData aio_write_data = { 0, (void*) wrap_aio_write, (void*) aio_write };
ssize_t wrap_aio_write(struct aiocb *aiocbp) {
    ValidWriteAssert(0);
    aio_write_t old_write = (aio_write_t) aio_write_data.Buffer;
    return old_write(aiocbp);
}

// Wrap pwrite-like functions.
// We have not seen them before, so just assert/report it.
typedef ssize_t (*pwrite_t)(int fd, const void *buf, size_t nbyte, off_t offset);
template<FuncData &foo>
ssize_t wrap_pwrite_temp(int fd, const void *buf, size_t nbyte, off_t offset) {
    ValidWriteAssert(0);
    pwrite_t old_write = (pwrite_t) foo.Buffer;
    return old_write(fd, buf, nbyte, offset);
}

// Define a FuncData for a pwrite-like functions.
// FIXME: clang accepts usinging wrap_pwrite_temp<X ## _data> in the struct
// initialization. Is this a gcc 4.2 bug?
#define DEFINE_PWRITE_DATA(X, NAME)                                        \
ssize_t wrap_ ## X (int fd, const void *buf, size_t nbyte, off_t offset);  \
FuncData X ## _data = { NAME, (void*) wrap_ ## X };                        \
ssize_t wrap_ ## X (int fd, const void *buf, size_t nbyte, off_t offset) { \
    return wrap_pwrite_temp<X ## _data>(fd, buf, nbyte, offset);           \
}

// This exists everywhere.
DEFINE_PWRITE_DATA(pwrite, "pwrite")
// These exist on 32 bit OS X
DEFINE_PWRITE_DATA(pwrite_NOCANCEL_UNIX2003, "pwrite$NOCANCEL$UNIX2003");
DEFINE_PWRITE_DATA(pwrite_UNIX2003, "pwrite$UNIX2003");
// This exists on 64 bit OS X
DEFINE_PWRITE_DATA(pwrite_NOCANCEL, "pwrite$NOCANCEL");

void AbortOnBadWrite(int fd, const void *wbuf, size_t count);

typedef ssize_t (*writev_t)(int fd, const struct iovec *iov, int iovcnt);
template<FuncData &foo>
ssize_t wrap_writev_temp(int fd, const struct iovec *iov, int iovcnt) {
    AbortOnBadWrite(fd, 0, iovcnt);
    writev_t old_write = (writev_t) foo.Buffer;
    return old_write(fd, iov, iovcnt);
}

// Define a FuncData for a writev-like functions.
#define DEFINE_WRITEV_DATA(X, NAME)                                  \
ssize_t wrap_ ## X (int fd, const struct iovec *iov, int iovcnt);    \
FuncData X ## _data = { NAME, (void*) wrap_ ## X };                  \
ssize_t wrap_ ## X (int fd, const struct iovec *iov, int iovcnt) {   \
    return wrap_writev_temp<X ## _data>(fd, iov, iovcnt);            \
}

// This exists everywhere.
DEFINE_WRITEV_DATA(writev, "writev");
// These exist on 32 bit OS X
DEFINE_WRITEV_DATA(writev_NOCANCEL_UNIX2003, "writev$NOCANCEL$UNIX2003");
DEFINE_WRITEV_DATA(writev_UNIX2003, "writev$UNIX2003");
// This exists on 64 bit OS X
DEFINE_WRITEV_DATA(writev_NOCANCEL, "writev$NOCANCEL");

typedef ssize_t (*write_t)(int fd, const void *buf, size_t count);
template<FuncData &foo>
ssize_t wrap_write_temp(int fd, const void *buf, size_t count) {
    AbortOnBadWrite(fd, buf, count);
    write_t old_write = (write_t) foo.Buffer;
    return old_write(fd, buf, count);
}

// Define a FuncData for a write-like functions.
#define DEFINE_WRITE_DATA(X, NAME)                                   \
ssize_t wrap_ ## X (int fd, const void *buf, size_t count);          \
FuncData X ## _data = { NAME, (void*) wrap_ ## X };                  \
ssize_t wrap_ ## X (int fd, const void *buf, size_t count) {         \
    return wrap_write_temp<X ## _data>(fd, buf, count);              \
}

// This exists everywhere.
DEFINE_WRITE_DATA(write, "write");
// These exist on 32 bit OS X
DEFINE_WRITE_DATA(write_NOCANCEL_UNIX2003, "write$NOCANCEL$UNIX2003");
DEFINE_WRITE_DATA(write_UNIX2003, "write$UNIX2003");
// This exists on 64 bit OS X
DEFINE_WRITE_DATA(write_NOCANCEL, "write$NOCANCEL");

FuncData *Functions[] = { &aio_write_data,

                          &pwrite_data,
                          &pwrite_NOCANCEL_UNIX2003_data,
                          &pwrite_UNIX2003_data,
                          &pwrite_NOCANCEL_data,

                          &write_data,
                          &write_NOCANCEL_UNIX2003_data,
                          &write_UNIX2003_data,
                          &write_NOCANCEL_data,

                          &writev_data,
                          &writev_NOCANCEL_UNIX2003_data,
                          &writev_UNIX2003_data,
                          &writev_NOCANCEL_data};

const int NumFunctions = ArrayLength(Functions);

std::vector<int>& getDebugFDs() {
    // We have to use new as some write happen during static destructors
    // so an static std::vector might be destroyed while we still need it.
    static std::vector<int> *DebugFDs = new std::vector<int>();
    return *DebugFDs;
}

struct AutoLockTraits {
    typedef PRLock *type;
    const static type empty() {
      return NULL;
    }
    const static void release(type aL) {
        PR_Unlock(aL);
    }
};

class MyAutoLock : public Scoped<AutoLockTraits> {
public:
    static PRLock *getDebugFDsLock() {
        // We have to use something lower level than a mutex. If we don't, we
        // can get recursive in here when called from logging a call to free.
        static PRLock *Lock = PR_NewLock();
        return Lock;
    }

    MyAutoLock() : Scoped<AutoLockTraits>(getDebugFDsLock()) {
        PR_Lock(get());
    }
};

bool PoisoningDisabled = false;
void AbortOnBadWrite(int fd, const void *wbuf, size_t count) {
    if (PoisoningDisabled)
        return;

    // Ignore writes of zero bytes, firefox does some during shutdown.
    if (count == 0)
        return;

    // Stdout and Stderr are OK.
    if(fd == 1 || fd == 2)
        return;

    struct stat buf;
    int rv = fstat(fd, &buf);
    if (!ValidWriteAssert(rv == 0))
        return;

    // FIFOs are used for thread communication during shutdown.
    if ((buf.st_mode & S_IFMT) == S_IFIFO)
        return;

    {
        MyAutoLock lockedScope;

        // Debugging FDs are OK
        std::vector<int> &Vec = getDebugFDs();
        if (std::find(Vec.begin(), Vec.end(), fd) != Vec.end())
            return;
    }

    // For writev we pass NULL in wbuf. We should only get here from
    // dbm, and it uses write, so assert that we have wbuf.
    if (!ValidWriteAssert(wbuf))
        return;

    // As a really bad hack, accept writes that don't change the on disk
    // content. This is needed because dbm doesn't keep track of dirty bits
    // and can end up writing the same data to disk twice. Once when the
    // user (nss) asks it to sync and once when closing the database.
    ScopedFreePtr<void> wbuf2(malloc(count));
    if (!ValidWriteAssert(wbuf2))
        return;
    off_t pos = lseek(fd, 0, SEEK_CUR);
    if (!ValidWriteAssert(pos != -1))
        return;
    ssize_t r = read(fd, wbuf2, count);
    if (!ValidWriteAssert(r == count))
        return;
    int cmp = memcmp(wbuf, wbuf2, count);
    if (!ValidWriteAssert(cmp == 0))
        return;
    off_t pos2 = lseek(fd, pos, SEEK_SET);
    if (!ValidWriteAssert(pos2 == pos))
        return;
}

// We cannot use destructors to free the lock and the list of debug fds since
// we don't control the order the destructors are called. Instead, we use
// libc funcion __cleanup callback which runs after the destructors.
void (*OldCleanup)();
extern "C" void (*__cleanup)();
void FinalCleanup() {
    if (OldCleanup)
        OldCleanup();
    if (sProfileDirectory)
        PL_strfree(sProfileDirectory);
    sProfileDirectory = nullptr;
    delete &getDebugFDs();
    PR_DestroyLock(MyAutoLock::getDebugFDsLock());
}

} // anonymous namespace

extern "C" {
    void MozillaRegisterDebugFD(int fd) {
        MyAutoLock lockedScope;
        std::vector<int> &Vec = getDebugFDs();
        MOZ_ASSERT(std::find(Vec.begin(), Vec.end(), fd) == Vec.end());
        Vec.push_back(fd);
    }
    void MozillaUnRegisterDebugFD(int fd) {
        MyAutoLock lockedScope;
        std::vector<int> &Vec = getDebugFDs();
        std::vector<int>::iterator i = std::find(Vec.begin(), Vec.end(), fd);
        MOZ_ASSERT(i != Vec.end());
        Vec.erase(i);
    }
}

namespace mozilla {
void PoisonWrite() {
    PoisoningDisabled = false;

    nsCOMPtr<nsIFile> mozFile;
    NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR, getter_AddRefs(mozFile));
    if (mozFile) {
        nsCAutoString nativePath;
        nsresult rv = mozFile->GetNativePath(nativePath);
        if (NS_SUCCEEDED(rv)) {
            sProfileDirectory = PL_strdup(nativePath.get());
        }
    }

    OldCleanup = __cleanup;
    __cleanup = FinalCleanup;

    for (int i = 0; i < NumFunctions; ++i) {
        FuncData *d = Functions[i];
        if (!d->Function)
            d->Function = dlsym(RTLD_DEFAULT, d->Name);
        if (!d->Function)
            continue;
        mach_error_t t = mach_override_ptr(d->Function, d->Wrapper,
                                           &d->Buffer);
        MOZ_ASSERT(t == err_none);
    }
}
void DisableWritePoisoning() {
    PoisoningDisabled = true;
}
}