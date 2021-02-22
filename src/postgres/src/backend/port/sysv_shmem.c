/*-------------------------------------------------------------------------
 *
 * sysv_shmem.c
 *	  Implement shared memory using SysV facilities
 *
 * These routines used to be a fairly thin layer on top of SysV shared
 * memory functionality.  With the addition of anonymous-shmem logic,
 * they're a bit fatter now.  We still require a SysV shmem block to
 * exist, though, because mmap'd shmem provides no way to find out how
 * many processes are attached, which we need for interlocking purposes.
 *
 * Portions Copyright (c) 1996-2018, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/backend/port/sysv_shmem.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <signal.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#ifdef HAVE_SYS_IPC_H
#include <sys/ipc.h>
#endif
#ifdef HAVE_SYS_SHM_H
#include <sys/shm.h>
#endif

#include "miscadmin.h"
#include "portability/mem.h"
#include "storage/dsm.h"
#include "storage/fd.h"
#include "storage/ipc.h"
#include "storage/pg_shmem.h"
#include "utils/guc.h"
#include "utils/pidfile.h"


/*
 * As of PostgreSQL 9.3, we normally allocate only a very small amount of
 * System V shared memory, and only for the purposes of providing an
 * interlock to protect the data directory.  The real shared memory block
 * is allocated using mmap().  This works around the problem that many
 * systems have very low limits on the amount of System V shared memory
 * that can be allocated.  Even a limit of a few megabytes will be enough
 * to run many copies of PostgreSQL without needing to adjust system settings.
 *
 * We assume that no one will attempt to run PostgreSQL 9.3 or later on
 * systems that are ancient enough that anonymous shared memory is not
 * supported, such as pre-2.4 versions of Linux.  If that turns out to be
 * false, we might need to add compile and/or run-time tests here and do this
 * only if the running kernel supports it.
 *
 * However, we must always disable this logic in the EXEC_BACKEND case, and
 * fall back to the old method of allocating the entire segment using System V
 * shared memory, because there's no way to attach an anonymous mmap'd segment
 * to a process after exec().  Since EXEC_BACKEND is intended only for
 * developer use, this shouldn't be a big problem.  Because of this, we do
 * not worry about supporting anonymous shmem in the EXEC_BACKEND cases below.
 *
 * As of PostgreSQL 12, we regained the ability to use a large System V shared
 * memory region even in non-EXEC_BACKEND builds, if shared_memory_type is set
 * to sysv (though this is not the default).
 */


typedef key_t IpcMemoryKey;		/* shared memory key passed to shmget(2) */
typedef int IpcMemoryId;		/* shared memory ID returned by shmget(2) */


unsigned long UsedShmemSegID = 0;
void	   *UsedShmemSegAddr = NULL;

static Size AnonymousShmemSize;
static void *AnonymousShmem = NULL;

static void *InternalIpcMemoryCreate(IpcMemoryKey memKey, Size size);
static void IpcMemoryDetach(int status, Datum shmaddr);
static void IpcMemoryDelete(int status, Datum shmId);
static PGShmemHeader *PGSharedMemoryAttach(IpcMemoryKey key,
					 IpcMemoryId *shmid);


/*
 *	InternalIpcMemoryCreate(memKey, size)
 *
 * Attempt to create a new shared memory segment with the specified key.
 * Will fail (return NULL) if such a segment already exists.  If successful,
 * attach the segment to the current process and return its attached address.
 * On success, callbacks are registered with on_shmem_exit to detach and
 * delete the segment when on_shmem_exit is called.
 *
 * If we fail with a failure code other than collision-with-existing-segment,
 * print out an error and abort.  Other types of errors are not recoverable.
 */
static void *
InternalIpcMemoryCreate(IpcMemoryKey memKey, Size size)
{
	IpcMemoryId shmid;
	void	   *requestedAddress = NULL;
	void	   *memAddress;

	/*
	 * Normally we just pass requestedAddress = NULL to shmat(), allowing the
	 * system to choose where the segment gets mapped.  But in an EXEC_BACKEND
	 * build, it's possible for whatever is chosen in the postmaster to not
	 * work for backends, due to variations in address space layout.  As a
	 * rather klugy workaround, allow the user to specify the address to use
	 * via setting the environment variable PG_SHMEM_ADDR.  (If this were of
	 * interest for anything except debugging, we'd probably create a cleaner
	 * and better-documented way to set it, such as a GUC.)
	 */
#ifdef EXEC_BACKEND
	{
		char	   *pg_shmem_addr = getenv("PG_SHMEM_ADDR");

		if (pg_shmem_addr)
			requestedAddress = (void *) strtoul(pg_shmem_addr, NULL, 0);
	}
#endif

	shmid = shmget(memKey, size, IPC_CREAT | IPC_EXCL | IPCProtection);

	if (shmid < 0)
	{
		int			shmget_errno = errno;

		/*
		 * Fail quietly if error indicates a collision with existing segment.
		 * One would expect EEXIST, given that we said IPC_EXCL, but perhaps
		 * we could get a permission violation instead?  Also, EIDRM might
		 * occur if an old seg is slated for destruction but not gone yet.
		 */
		if (shmget_errno == EEXIST || shmget_errno == EACCES
#ifdef EIDRM
			|| shmget_errno == EIDRM
#endif
			)
			return NULL;

		/*
		 * Some BSD-derived kernels are known to return EINVAL, not EEXIST, if
		 * there is an existing segment but it's smaller than "size" (this is
		 * a result of poorly-thought-out ordering of error tests). To
		 * distinguish between collision and invalid size in such cases, we
		 * make a second try with size = 0.  These kernels do not test size
		 * against SHMMIN in the preexisting-segment case, so we will not get
		 * EINVAL a second time if there is such a segment.
		 */
		if (shmget_errno == EINVAL)
		{
			shmid = shmget(memKey, 0, IPC_CREAT | IPC_EXCL | IPCProtection);

			if (shmid < 0)
			{
				/* As above, fail quietly if we verify a collision */
				if (errno == EEXIST || errno == EACCES
#ifdef EIDRM
					|| errno == EIDRM
#endif
					)
					return NULL;
				/* Otherwise, fall through to report the original error */
			}
			else
			{
				/*
				 * On most platforms we cannot get here because SHMMIN is
				 * greater than zero.  However, if we do succeed in creating a
				 * zero-size segment, free it and then fall through to report
				 * the original error.
				 */
				if (shmctl(shmid, IPC_RMID, NULL) < 0)
					elog(LOG, "shmctl(%d, %d, 0) failed: %m",
						 (int) shmid, IPC_RMID);
			}
		}

		/*
		 * Else complain and abort.
		 *
		 * Note: at this point EINVAL should mean that either SHMMIN or SHMMAX
		 * is violated.  SHMALL violation might be reported as either ENOMEM
		 * (BSDen) or ENOSPC (Linux); the Single Unix Spec fails to say which
		 * it should be.  SHMMNI violation is ENOSPC, per spec.  Just plain
		 * not-enough-RAM is ENOMEM.
		 */
		errno = shmget_errno;
		ereport(FATAL,
				(errmsg("could not create shared memory segment: %m"),
				 errdetail("Failed system call was shmget(key=%lu, size=%zu, 0%o).",
						   (unsigned long) memKey, size,
						   IPC_CREAT | IPC_EXCL | IPCProtection),
				 (shmget_errno == EINVAL) ?
				 errhint("This error usually means that PostgreSQL's request for a shared memory "
						 "segment exceeded your kernel's SHMMAX parameter, or possibly that "
						 "it is less than "
						 "your kernel's SHMMIN parameter.\n"
						 "The PostgreSQL documentation contains more information about shared "
						 "memory configuration.") : 0,
				 (shmget_errno == ENOMEM) ?
				 errhint("This error usually means that PostgreSQL's request for a shared "
						 "memory segment exceeded your kernel's SHMALL parameter.  You might need "
						 "to reconfigure the kernel with larger SHMALL.\n"
						 "The PostgreSQL documentation contains more information about shared "
						 "memory configuration.") : 0,
				 (shmget_errno == ENOSPC) ?
				 errhint("This error does *not* mean that you have run out of disk space.  "
						 "It occurs either if all available shared memory IDs have been taken, "
						 "in which case you need to raise the SHMMNI parameter in your kernel, "
						 "or because the system's overall limit for shared memory has been "
						 "reached.\n"
						 "The PostgreSQL documentation contains more information about shared "
						 "memory configuration.") : 0));
	}

	/* Register on-exit routine to delete the new segment */
	on_shmem_exit(IpcMemoryDelete, Int32GetDatum(shmid));

	/* OK, should be able to attach to the segment */
	memAddress = shmat(shmid, requestedAddress, PG_SHMAT_FLAGS);

	if (memAddress == (void *) -1)
		elog(FATAL, "shmat(id=%d, addr=%p, flags=0x%x) failed: %m",
			 shmid, requestedAddress, PG_SHMAT_FLAGS);

	/* Register on-exit routine to detach new segment before deleting */
	on_shmem_exit(IpcMemoryDetach, PointerGetDatum(memAddress));

	/*
	 * Store shmem key and ID in data directory lockfile.  Format to try to
	 * keep it the same length always (trailing junk in the lockfile won't
	 * hurt, but might confuse humans).
	 */
	{
		char		line[64];

		sprintf(line, "%9lu %9lu",
				(unsigned long) memKey, (unsigned long) shmid);
		AddToDataDirLockFile(LOCK_FILE_LINE_SHMEM_KEY, line);
	}

	return memAddress;
}

/****************************************************************************/
/*	IpcMemoryDetach(status, shmaddr)	removes a shared memory segment		*/
/*										from process' address space			*/
/*	(called as an on_shmem_exit callback, hence funny argument list)		*/
/****************************************************************************/
static void
IpcMemoryDetach(int status, Datum shmaddr)
{
	/* Detach System V shared memory block. */
	if (shmdt(DatumGetPointer(shmaddr)) < 0)
		elog(LOG, "shmdt(%p) failed: %m", DatumGetPointer(shmaddr));
}

/****************************************************************************/
/*	IpcMemoryDelete(status, shmId)		deletes a shared memory segment		*/
/*	(called as an on_shmem_exit callback, hence funny argument list)		*/
/****************************************************************************/
static void
IpcMemoryDelete(int status, Datum shmId)
{
	if (shmctl(DatumGetInt32(shmId), IPC_RMID, NULL) < 0)
		elog(LOG, "shmctl(%d, %d, 0) failed: %m",
			 DatumGetInt32(shmId), IPC_RMID);
}

/*
 * PGSharedMemoryIsInUse
 *
 * Is a previously-existing shmem segment still existing and in use?
 *
 * The point of this exercise is to detect the case where a prior postmaster
 * crashed, but it left child backends that are still running.  Therefore
 * we only care about shmem segments that are associated with the intended
 * DataDir.  This is an important consideration since accidental matches of
 * shmem segment IDs are reasonably common.
 */
bool
PGSharedMemoryIsInUse(unsigned long id1, unsigned long id2)
{
	IpcMemoryId shmId = (IpcMemoryId) id2;
	struct shmid_ds shmStat;
	struct stat statbuf;
	PGShmemHeader *hdr;

	/*
	 * We detect whether a shared memory segment is in use by seeing whether
	 * it (a) exists and (b) has any processes attached to it.
	 */
	if (shmctl(shmId, IPC_STAT, &shmStat) < 0)
	{
		/*
		 * EINVAL actually has multiple possible causes documented in the
		 * shmctl man page, but we assume it must mean the segment no longer
		 * exists.
		 */
		if (errno == EINVAL)
			return false;

		/*
		 * EACCES implies that the segment belongs to some other userid, which
		 * means it is not a Postgres shmem segment (or at least, not one that
		 * is relevant to our data directory).
		 */
		if (errno == EACCES)
			return false;

		/*
		 * Some Linux kernel versions (in fact, all of them as of July 2007)
		 * sometimes return EIDRM when EINVAL is correct.  The Linux kernel
		 * actually does not have any internal state that would justify
		 * returning EIDRM, so we can get away with assuming that EIDRM is
		 * equivalent to EINVAL on that platform.
		 */
#ifdef HAVE_LINUX_EIDRM_BUG
		if (errno == EIDRM)
			return false;
#endif

		/*
		 * Otherwise, we had better assume that the segment is in use. The
		 * only likely case is EIDRM, which implies that the segment has been
		 * IPC_RMID'd but there are still processes attached to it.
		 */
		return true;
	}

	/* If it has no attached processes, it's not in use */
	if (shmStat.shm_nattch == 0)
		return false;

	/*
	 * Try to attach to the segment and see if it matches our data directory.
	 * This avoids shmid-conflict problems on machines that are running
	 * several postmasters under the same userid.
	 */
	if (stat(DataDir, &statbuf) < 0)
		return true;			/* if can't stat, be conservative */

	hdr = (PGShmemHeader *) shmat(shmId, NULL, PG_SHMAT_FLAGS);

	if (hdr == (PGShmemHeader *) -1)
		return true;			/* if can't attach, be conservative */

	if (hdr->magic != PGShmemMagic ||
		hdr->device != statbuf.st_dev ||
		hdr->inode != statbuf.st_ino)
	{
		/*
		 * It's either not a Postgres segment, or not one for my data
		 * directory.  In either case it poses no threat.
		 */
		shmdt((void *) hdr);
		return false;
	}

	/* Trouble --- looks a lot like there's still live backends */
	shmdt((void *) hdr);

	return true;
}

#ifdef MAP_HUGETLB

/*
 * Identify the huge page size to use.
 *
 * Some Linux kernel versions have a bug causing mmap() to fail on requests
 * that are not a multiple of the hugepage size.  Versions without that bug
 * instead silently round the request up to the next hugepage multiple ---
 * and then munmap() fails when we give it a size different from that.
 * So we have to round our request up to a multiple of the actual hugepage
 * size to avoid trouble.
 *
 * Doing the round-up ourselves also lets us make use of the extra memory,
 * rather than just wasting it.  Currently, we just increase the available
 * space recorded in the shmem header, which will make the extra usable for
 * purposes such as additional locktable entries.  Someday, for very large
 * hugepage sizes, we might want to think about more invasive strategies,
 * such as increasing shared_buffers to absorb the extra space.
 *
 * Returns the (real or assumed) page size into *hugepagesize,
 * and the hugepage-related mmap flags to use into *mmap_flags.
 *
 * Currently *mmap_flags is always just MAP_HUGETLB.  Someday, on systems
 * that support it, we might OR in additional bits to specify a particular
 * non-default huge page size.
 */
static void
GetHugePageSize(Size *hugepagesize, int *mmap_flags)
{
	/*
	 * If we fail to find out the system's default huge page size, assume it
	 * is 2MB.  This will work fine when the actual size is less.  If it's
	 * more, we might get mmap() or munmap() failures due to unaligned
	 * requests; but at this writing, there are no reports of any non-Linux
	 * systems being picky about that.
	 */
	*hugepagesize = 2 * 1024 * 1024;
	*mmap_flags = MAP_HUGETLB;

	/*
	 * System-dependent code to find out the default huge page size.
	 *
	 * On Linux, read /proc/meminfo looking for a line like "Hugepagesize:
	 * nnnn kB".  Ignore any failures, falling back to the preset default.
	 */
#ifdef __linux__
	{
		FILE	   *fp = AllocateFile("/proc/meminfo", "r");
		char		buf[128];
		unsigned int sz;
		char		ch;

		if (fp)
		{
			while (fgets(buf, sizeof(buf), fp))
			{
				if (sscanf(buf, "Hugepagesize: %u %c", &sz, &ch) == 2)
				{
					if (ch == 'k')
					{
						*hugepagesize = sz * (Size) 1024;
						break;
					}
					/* We could accept other units besides kB, if needed */
				}
			}
			FreeFile(fp);
		}
	}
#endif							/* __linux__ */
}

#endif							/* MAP_HUGETLB */

/*
 * Creates an anonymous mmap()ed shared memory segment.
 *
 * Pass the requested size in *size.  This function will modify *size to the
 * actual size of the allocation, if it ends up allocating a segment that is
 * larger than requested.
 */
static void *
CreateAnonymousSegment(Size *size)
{
	Size		allocsize = *size;
	void	   *ptr = MAP_FAILED;
	int			mmap_errno = 0;

#ifndef MAP_HUGETLB
	/* PGSharedMemoryCreate should have dealt with this case */
	Assert(huge_pages != HUGE_PAGES_ON);
#else
	if (huge_pages == HUGE_PAGES_ON || huge_pages == HUGE_PAGES_TRY)
	{
		/*
		 * Round up the request size to a suitable large value.
		 */
		Size		hugepagesize;
		int			mmap_flags;

		GetHugePageSize(&hugepagesize, &mmap_flags);

		if (allocsize % hugepagesize != 0)
			allocsize += hugepagesize - (allocsize % hugepagesize);

		ptr = mmap(NULL, allocsize, PROT_READ | PROT_WRITE,
				   PG_MMAP_FLAGS | mmap_flags, -1, 0);
		mmap_errno = errno;
		if (huge_pages == HUGE_PAGES_TRY && ptr == MAP_FAILED)
			elog(DEBUG1, "mmap(%zu) with MAP_HUGETLB failed, huge pages disabled: %m",
				 allocsize);
	}
#endif

	if (ptr == MAP_FAILED && huge_pages != HUGE_PAGES_ON)
	{
		/*
		 * Use the original size, not the rounded-up value, when falling back
		 * to non-huge pages.
		 */
		allocsize = *size;
		ptr = mmap(NULL, allocsize, PROT_READ | PROT_WRITE,
				   PG_MMAP_FLAGS, -1, 0);
		mmap_errno = errno;
	}

	if (ptr == MAP_FAILED)
	{
		errno = mmap_errno;
		ereport(FATAL,
				(errmsg("could not map anonymous shared memory: %m"),
				 (mmap_errno == ENOMEM) ?
				 errhint("This error usually means that PostgreSQL's request "
						 "for a shared memory segment exceeded available memory, "
						 "swap space, or huge pages. To reduce the request size "
						 "(currently %zu bytes), reduce PostgreSQL's shared "
						 "memory usage, perhaps by reducing shared_buffers or "
						 "max_connections.",
						 *size) : 0));
	}

	*size = allocsize;
	return ptr;
}

/*
 * AnonymousShmemDetach --- detach from an anonymous mmap'd block
 * (called as an on_shmem_exit callback, hence funny argument list)
 */
static void
AnonymousShmemDetach(int status, Datum arg)
{
	/* Release anonymous shared memory block, if any. */
	if (AnonymousShmem != NULL)
	{
		if (munmap(AnonymousShmem, AnonymousShmemSize) < 0)
			elog(LOG, "munmap(%p, %zu) failed: %m",
				 AnonymousShmem, AnonymousShmemSize);
		AnonymousShmem = NULL;
	}
}

/*
 * PGSharedMemoryCreate
 *
 * Create a shared memory segment of the given size and initialize its
 * standard header.  Also, register an on_shmem_exit callback to release
 * the storage.
 *
 * Dead Postgres segments are recycled if found, but we do not fail upon
 * collision with non-Postgres shmem segments.  The idea here is to detect and
 * re-use keys that may have been assigned by a crashed postmaster or backend.
 *
 * makePrivate means to always create a new segment, rather than attach to
 * or recycle any existing segment.
 *
 * The port number is passed for possible use as a key (for SysV, we use
 * it to generate the starting shmem key).  In a standalone backend,
 * zero will be passed.
 */
PGShmemHeader *
PGSharedMemoryCreate(Size size, bool makePrivate, int port,
					 PGShmemHeader **shim)
{
	IpcMemoryKey NextShmemSegID;
	void	   *memAddress;
	PGShmemHeader *hdr;
	IpcMemoryId shmid;
	struct stat statbuf;
	Size		sysvsize;

	/* Complain if hugepages demanded but we can't possibly support them */
#if !defined(MAP_HUGETLB)
	if (huge_pages == HUGE_PAGES_ON)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("huge pages not supported on this platform")));
#endif

	/* Room for a header? */
	Assert(size > MAXALIGN(sizeof(PGShmemHeader)));

	if (shared_memory_type == SHMEM_TYPE_MMAP)
	{
		AnonymousShmem = CreateAnonymousSegment(&size);
		AnonymousShmemSize = size;

		/* Register on-exit routine to unmap the anonymous segment */
		on_shmem_exit(AnonymousShmemDetach, (Datum) 0);

		/* Now we need only allocate a minimal-sized SysV shmem block. */
		sysvsize = sizeof(PGShmemHeader);
	}
	else
		sysvsize = size;

	/* Make sure PGSharedMemoryAttach doesn't fail without need */
	UsedShmemSegAddr = NULL;

	/* Loop till we find a free IPC key */
	NextShmemSegID = port * 1000;

	for (NextShmemSegID++;; NextShmemSegID++)
	{
		/* Try to create new segment */
		memAddress = InternalIpcMemoryCreate(NextShmemSegID, sysvsize);
		if (memAddress)
			break;				/* successful create and attach */

		/* Check shared memory and possibly remove and recreate */

		if (makePrivate)		/* a standalone backend shouldn't do this */
			continue;

		if ((memAddress = PGSharedMemoryAttach(NextShmemSegID, &shmid)) == NULL)
			continue;			/* can't attach, not one of mine */

		/*
		 * If I am not the creator and it belongs to an extant process,
		 * continue.
		 */
		hdr = (PGShmemHeader *) memAddress;
		if (hdr->creatorPID != getpid())
		{
			if (kill(hdr->creatorPID, 0) == 0 || errno != ESRCH)
			{
				shmdt(memAddress);
				continue;		/* segment belongs to a live process */
			}
		}

		/*
		 * The segment appears to be from a dead Postgres process, or from a
		 * previous cycle of life in this same process.  Zap it, if possible,
		 * and any associated dynamic shared memory segments, as well. This
		 * probably shouldn't fail, but if it does, assume the segment belongs
		 * to someone else after all, and continue quietly.
		 */
		if (hdr->dsm_control != 0)
			dsm_cleanup_using_control_segment(hdr->dsm_control);
		shmdt(memAddress);
		if (shmctl(shmid, IPC_RMID, NULL) < 0)
			continue;

		/*
		 * Now try again to create the segment.
		 */
		memAddress = InternalIpcMemoryCreate(NextShmemSegID, sysvsize);
		if (memAddress)
			break;				/* successful create and attach */

		/*
		 * Can only get here if some other process managed to create the same
		 * shmem key before we did.  Let him have that one, loop around to try
		 * next key.
		 */
	}

	/*
	 * OK, we created a new segment.  Mark it as created by this process. The
	 * order of assignments here is critical so that another Postgres process
	 * can't see the header as valid but belonging to an invalid PID!
	 */
	hdr = (PGShmemHeader *) memAddress;
	hdr->creatorPID = getpid();
	hdr->magic = PGShmemMagic;
	hdr->dsm_control = 0;

	/* Fill in the data directory ID info, too */
	if (stat(DataDir, &statbuf) < 0)
		ereport(FATAL,
				(errcode_for_file_access(),
				 errmsg("could not stat data directory \"%s\": %m",
						DataDir)));
	hdr->device = statbuf.st_dev;
	hdr->inode = statbuf.st_ino;

	/*
	 * Initialize space allocation status for segment.
	 */
	hdr->totalsize = size;
	hdr->freeoffset = MAXALIGN(sizeof(PGShmemHeader));
	*shim = hdr;

	/* Save info for possible future use */
	UsedShmemSegAddr = memAddress;
	UsedShmemSegID = (unsigned long) NextShmemSegID;

	/*
	 * If AnonymousShmem is NULL here, then we're not using anonymous shared
	 * memory, and should return a pointer to the System V shared memory
	 * block. Otherwise, the System V shared memory block is only a shim, and
	 * we must return a pointer to the real block.
	 */
	if (AnonymousShmem == NULL)
		return hdr;
	memcpy(AnonymousShmem, hdr, sizeof(PGShmemHeader));
	return (PGShmemHeader *) AnonymousShmem;
}

#ifdef EXEC_BACKEND

/*
 * PGSharedMemoryReAttach
 *
 * This is called during startup of a postmaster child process to re-attach to
 * an already existing shared memory segment.  This is needed only in the
 * EXEC_BACKEND case; otherwise postmaster children inherit the shared memory
 * segment attachment via fork().
 *
 * UsedShmemSegID and UsedShmemSegAddr are implicit parameters to this
 * routine.  The caller must have already restored them to the postmaster's
 * values.
 */
void
PGSharedMemoryReAttach(void)
{
	IpcMemoryId shmid;
	void	   *hdr;
	void	   *origUsedShmemSegAddr = UsedShmemSegAddr;

	Assert(UsedShmemSegAddr != NULL);
	Assert(IsUnderPostmaster);

#ifdef __CYGWIN__
	/* cygipc (currently) appears to not detach on exec. */
	PGSharedMemoryDetach();
	UsedShmemSegAddr = origUsedShmemSegAddr;
#endif

	elog(DEBUG3, "attaching to %p", UsedShmemSegAddr);
	hdr = (void *) PGSharedMemoryAttach((IpcMemoryKey) UsedShmemSegID, &shmid);
	if (hdr == NULL)
		elog(FATAL, "could not reattach to shared memory (key=%d, addr=%p): %m",
			 (int) UsedShmemSegID, UsedShmemSegAddr);
	if (hdr != origUsedShmemSegAddr)
		elog(FATAL, "reattaching to shared memory returned unexpected address (got %p, expected %p)",
			 hdr, origUsedShmemSegAddr);
	dsm_set_control_handle(((PGShmemHeader *) hdr)->dsm_control);

	UsedShmemSegAddr = hdr;		/* probably redundant */
}

/*
 * PGSharedMemoryNoReAttach
 *
 * This is called during startup of a postmaster child process when we choose
 * *not* to re-attach to the existing shared memory segment.  We must clean up
 * to leave things in the appropriate state.  This is not used in the non
 * EXEC_BACKEND case, either.
 *
 * The child process startup logic might or might not call PGSharedMemoryDetach
 * after this; make sure that it will be a no-op if called.
 *
 * UsedShmemSegID and UsedShmemSegAddr are implicit parameters to this
 * routine.  The caller must have already restored them to the postmaster's
 * values.
 */
void
PGSharedMemoryNoReAttach(void)
{
	Assert(UsedShmemSegAddr != NULL);
	Assert(IsUnderPostmaster);

#ifdef __CYGWIN__
	/* cygipc (currently) appears to not detach on exec. */
	PGSharedMemoryDetach();
#endif

	/* For cleanliness, reset UsedShmemSegAddr to show we're not attached. */
	UsedShmemSegAddr = NULL;
	/* And the same for UsedShmemSegID. */
	UsedShmemSegID = 0;
}

#endif							/* EXEC_BACKEND */

/*
 * PGSharedMemoryDetach
 *
 * Detach from the shared memory segment, if still attached.  This is not
 * intended to be called explicitly by the process that originally created the
 * segment (it will have on_shmem_exit callback(s) registered to do that).
 * Rather, this is for subprocesses that have inherited an attachment and want
 * to get rid of it.
 *
 * UsedShmemSegID and UsedShmemSegAddr are implicit parameters to this
 * routine, also AnonymousShmem and AnonymousShmemSize.
 */
void
PGSharedMemoryDetach(void)
{
	if (UsedShmemSegAddr != NULL)
	{
		if ((shmdt(UsedShmemSegAddr) < 0)
#if defined(EXEC_BACKEND) && defined(__CYGWIN__)
		/* Work-around for cygipc exec bug */
			&& shmdt(NULL) < 0
#endif
			)
			elog(LOG, "shmdt(%p) failed: %m", UsedShmemSegAddr);
		UsedShmemSegAddr = NULL;
	}

	if (AnonymousShmem != NULL)
	{
		if (munmap(AnonymousShmem, AnonymousShmemSize) < 0)
			elog(LOG, "munmap(%p, %zu) failed: %m",
				 AnonymousShmem, AnonymousShmemSize);
		AnonymousShmem = NULL;
	}
}


/*
 * Attach to shared memory and make sure it has a Postgres header
 *
 * Returns attach address if OK, else NULL
 */
static PGShmemHeader *
PGSharedMemoryAttach(IpcMemoryKey key, IpcMemoryId *shmid)
{
	PGShmemHeader *hdr;

	if ((*shmid = shmget(key, sizeof(PGShmemHeader), 0)) < 0)
		return NULL;

	hdr = (PGShmemHeader *) shmat(*shmid, UsedShmemSegAddr, PG_SHMAT_FLAGS);

	if (hdr == (PGShmemHeader *) -1)
		return NULL;			/* failed: must be some other app's */

	if (hdr->magic != PGShmemMagic)
	{
		shmdt((void *) hdr);
		return NULL;			/* segment belongs to a non-Postgres app */
	}

	return hdr;
}
