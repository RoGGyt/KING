/* KING client: in-process installer for release zips (see king_install.h). */
#include "king_install.h"

#include <base/fs.h>
#include <base/io.h>
#include <base/log.h>
#include <base/str.h>

#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

static constexpr const char *KING_OLD_SUFFIX = ".king_old";
static constexpr const char *KING_STAGING_DIR = "king_update_staging";

namespace
{
uint16_t Read16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t Read32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

struct SZipEntry
{
	std::string m_Name; // relative path inside the zip, '/' separated
	uint16_t m_Method;
	uint32_t m_Crc;
	uint32_t m_CompSize;
	uint32_t m_Size;
	uint32_t m_LocalOffset;
};

bool SafeRelativePath(const std::string &Path)
{
	if(Path.empty() || Path[0] == '/' || Path[0] == '\\' || Path.find(':') != std::string::npos || Path.find('\\') != std::string::npos)
		return false;
	size_t Start = 0;
	while(Start <= Path.size())
	{
		const size_t End = std::min(Path.find('/', Start), Path.size());
		const std::string Part = Path.substr(Start, End - Start);
		if(Part == "..")
			return false;
		Start = End + 1;
	}
	return true;
}

bool ReadWholeFile(const char *pPath, std::vector<uint8_t> &vData)
{
	IOHANDLE File = io_open(pPath, IOFLAG_READ);
	if(!File)
		return false;
	const int64_t Length = io_length(File);
	if(Length <= 0 || Length > 512ll * 1024 * 1024)
	{
		io_close(File);
		return false;
	}
	vData.resize((size_t)Length);
	const bool Ok = io_read(File, vData.data(), (unsigned)Length) == (unsigned)Length;
	io_close(File);
	return Ok;
}

bool WriteWholeFile(const char *pPath, const uint8_t *pData, size_t Size)
{
	if(fs_makedir_rec_for(pPath) != 0)
		return false;
	IOHANDLE File = io_open(pPath, IOFLAG_WRITE);
	if(!File)
		return false;
	const bool Ok = Size == 0 || io_write(File, pData, (unsigned)Size) == (unsigned)Size;
	return io_close(File) == 0 && Ok;
}

bool ParseZip(const std::vector<uint8_t> &vZip, std::vector<SZipEntry> &vEntries)
{
	const size_t Size = vZip.size();
	if(Size < 22)
		return false;
	// end of central directory: last record, comment up to 64 KiB
	size_t Eocd = std::string::npos;
	for(size_t i = Size - 22 + 1; i-- > 0 && Size - i <= 22 + 65535;)
	{
		if(Read32(&vZip[i]) == 0x06054b50)
		{
			Eocd = i;
			break;
		}
	}
	if(Eocd == std::string::npos)
		return false;
	const uint16_t Count = Read16(&vZip[Eocd + 10]);
	const uint32_t CdSize = Read32(&vZip[Eocd + 12]);
	const uint32_t CdOffset = Read32(&vZip[Eocd + 16]);
	if((uint64_t)CdOffset + CdSize > Size)
		return false;
	size_t Pos = CdOffset;
	for(int i = 0; i < Count; i++)
	{
		if(Pos + 46 > Size || Read32(&vZip[Pos]) != 0x02014b50)
			return false;
		SZipEntry Entry;
		const uint16_t Flags = Read16(&vZip[Pos + 8]);
		Entry.m_Method = Read16(&vZip[Pos + 10]);
		Entry.m_Crc = Read32(&vZip[Pos + 16]);
		Entry.m_CompSize = Read32(&vZip[Pos + 20]);
		Entry.m_Size = Read32(&vZip[Pos + 24]);
		const uint16_t NameLen = Read16(&vZip[Pos + 28]);
		const uint16_t ExtraLen = Read16(&vZip[Pos + 30]);
		const uint16_t CommentLen = Read16(&vZip[Pos + 32]);
		Entry.m_LocalOffset = Read32(&vZip[Pos + 42]);
		if(Pos + 46 + NameLen > Size || (Flags & 1)) // encrypted entries are not supported
			return false;
		Entry.m_Name.assign((const char *)&vZip[Pos + 46], NameLen);
		std::replace(Entry.m_Name.begin(), Entry.m_Name.end(), '\\', '/');
		Pos += 46 + NameLen + ExtraLen + CommentLen;
		if(!Entry.m_Name.empty() && Entry.m_Name.back() == '/')
			continue; // folder
		if(Entry.m_Size == 0xFFFFFFFF || Entry.m_CompSize == 0xFFFFFFFF)
			return false; // zip64 is not needed for a client release
		vEntries.push_back(std::move(Entry));
	}
	return !vEntries.empty();
}

bool ExtractEntry(const std::vector<uint8_t> &vZip, const SZipEntry &Entry, std::vector<uint8_t> &vOut)
{
	const size_t Local = Entry.m_LocalOffset;
	if(Local + 30 > vZip.size() || Read32(&vZip[Local]) != 0x04034b50)
		return false;
	const size_t Data = Local + 30 + Read16(&vZip[Local + 26]) + Read16(&vZip[Local + 28]);
	if((uint64_t)Data + Entry.m_CompSize > vZip.size())
		return false;
	vOut.resize(Entry.m_Size);
	if(Entry.m_Method == 0)
	{
		if(Entry.m_CompSize != Entry.m_Size)
			return false;
		std::copy_n(&vZip[Data], Entry.m_Size, vOut.begin());
	}
	else if(Entry.m_Method == 8)
	{
		z_stream Stream = {};
		if(inflateInit2(&Stream, -MAX_WBITS) != Z_OK)
			return false;
		Stream.next_in = const_cast<Bytef *>(&vZip[Data]);
		Stream.avail_in = Entry.m_CompSize;
		Stream.next_out = vOut.data();
		Stream.avail_out = Entry.m_Size;
		const int Result = inflate(&Stream, Z_FINISH);
		const bool Ok = (Result == Z_STREAM_END || (Result == Z_BUF_ERROR && Entry.m_Size == 0)) && Stream.total_out == Entry.m_Size;
		inflateEnd(&Stream);
		if(!Ok)
			return false;
	}
	else
		return false;
	return crc32(0L, vOut.data(), (uInt)vOut.size()) == Entry.m_Crc;
}

void JoinPath(char *pOut, size_t OutSize, const char *pDir, const std::string &Rel)
{
	str_format(pOut, OutSize, "%s/%s", pDir, Rel.c_str());
}

struct SCleanup
{
	std::string m_Dir;
	int m_Removed = 0;
	int m_Depth = 0;
};

int CleanupCallback(const char *pName, int IsDir, int, void *pUser)
{
	SCleanup *pCleanup = static_cast<SCleanup *>(pUser);
	if(pName[0] == '.')
		return 0;
	const std::string Path = pCleanup->m_Dir + "/" + pName;
	if(IsDir)
	{
		if(pCleanup->m_Depth >= 6)
			return 0;
		SCleanup Sub;
		Sub.m_Dir = Path;
		Sub.m_Depth = pCleanup->m_Depth + 1;
		fs_listdir(Path.c_str(), CleanupCallback, 0, &Sub);
		pCleanup->m_Removed += Sub.m_Removed;
	}
	else if(str_endswith(pName, KING_OLD_SUFFIX) && fs_remove(Path.c_str()) == 0)
		pCleanup->m_Removed++;
	return 0;
}

void RemoveTree(const std::string &Dir, int Depth = 0)
{
	if(Depth > 8)
		return;
	struct SCtx
	{
		std::string m_Dir;
		int m_Depth;
	} Ctx{Dir, Depth};
	fs_listdir(
		Dir.c_str(), [](const char *pName, int IsDir, int, void *pUser) {
			SCtx *p = static_cast<SCtx *>(pUser);
			if(str_comp(pName, ".") == 0 || str_comp(pName, "..") == 0)
				return 0;
			const std::string Path = p->m_Dir + "/" + pName;
			if(IsDir)
				RemoveTree(Path, p->m_Depth + 1);
			else
				(void)fs_remove(Path.c_str());
			return 0;
		},
		0, &Ctx);
	(void)fs_removedir(Dir.c_str());
}
} // namespace

bool KingInstallZip(const char *pZipPath, const char *pDestDir, char *pError, size_t ErrorSize)
{
	std::vector<uint8_t> vZip;
	if(!ReadWholeFile(pZipPath, vZip))
	{
		str_copy(pError, "Could not read the downloaded update.", ErrorSize);
		return false;
	}
	std::vector<SZipEntry> vEntries;
	if(!ParseZip(vZip, vEntries))
	{
		str_copy(pError, "The downloaded update is not a valid zip.", ErrorSize);
		return false;
	}

	// strip a single top folder (KING-x.y.z-win64/...)
	{
		const size_t Slash = vEntries[0].m_Name.find('/');
		if(Slash != std::string::npos)
		{
			const std::string Prefix = vEntries[0].m_Name.substr(0, Slash + 1);
			if(std::all_of(vEntries.begin(), vEntries.end(), [&](const SZipEntry &E) { return E.m_Name.rfind(Prefix, 0) == 0; }))
				for(SZipEntry &E : vEntries)
					E.m_Name.erase(0, Prefix.size());
		}
	}
	bool HasExe = false;
	for(const SZipEntry &E : vEntries)
	{
		if(!SafeRelativePath(E.m_Name))
		{
			str_format(pError, ErrorSize, "Unsafe path in update: %s", E.m_Name.c_str());
			return false;
		}
		HasExe |= str_comp_nocase(E.m_Name.c_str(), "KING.exe") == 0;
	}
	if(!HasExe)
	{
		str_copy(pError, "KING.exe not found in the update.", ErrorSize);
		return false;
	}

	// 1) unpack and verify everything into a staging folder next to the install
	char aStaging[IO_MAX_PATH_LENGTH];
	str_format(aStaging, sizeof(aStaging), "%s/%s", pDestDir, KING_STAGING_DIR);
	RemoveTree(aStaging);
	std::vector<uint8_t> vData;
	char aPath[IO_MAX_PATH_LENGTH];
	for(const SZipEntry &E : vEntries)
	{
		if(!ExtractEntry(vZip, E, vData))
		{
			str_format(pError, ErrorSize, "Damaged file in update: %s", E.m_Name.c_str());
			RemoveTree(aStaging);
			return false;
		}
		JoinPath(aPath, sizeof(aPath), aStaging, E.m_Name);
		if(!WriteWholeFile(aPath, vData.data(), vData.size()))
		{
			str_format(pError, ErrorSize, "Could not write %s. Is the KING folder write protected?", E.m_Name.c_str());
			RemoveTree(aStaging);
			return false;
		}
	}
	vZip.clear();
	vZip.shrink_to_fit();

	// 2) move into place; files in use are renamed out of the way first
	int Failed = 0;
	char aFirstFailed[256] = "";
	char aTarget[IO_MAX_PATH_LENGTH];
	char aOld[IO_MAX_PATH_LENGTH];
	for(const SZipEntry &E : vEntries)
	{
		JoinPath(aPath, sizeof(aPath), aStaging, E.m_Name);
		JoinPath(aTarget, sizeof(aTarget), pDestDir, E.m_Name);
		if(fs_is_file(aTarget))
		{
			str_format(aOld, sizeof(aOld), "%s%s", aTarget, KING_OLD_SUFFIX);
			(void)fs_remove(aOld);
			if(fs_remove(aTarget) != 0 && fs_rename(aTarget, aOld) != 0)
			{
				if(!Failed++)
					str_copy(aFirstFailed, E.m_Name.c_str());
				continue;
			}
		}
		else if(fs_makedir_rec_for(aTarget) != 0)
		{
			if(!Failed++)
				str_copy(aFirstFailed, E.m_Name.c_str());
			continue;
		}
		if(fs_rename(aPath, aTarget) != 0)
		{
			// put the old file back so the install stays usable
			str_format(aOld, sizeof(aOld), "%s%s", aTarget, KING_OLD_SUFFIX);
			(void)fs_rename(aOld, aTarget);
			if(!Failed++)
				str_copy(aFirstFailed, E.m_Name.c_str());
		}
	}
	RemoveTree(aStaging);
	if(Failed)
	{
		str_format(pError, ErrorSize, "%d file(s) could not be replaced (first: %s).", Failed, aFirstFailed);
		log_error("king", "update: %s", pError);
		return false;
	}
	log_info("king", "update: installed %d files into %s", (int)vEntries.size(), pDestDir);
	return true;
}

int KingCleanupOldFiles(const char *pDir)
{
	SCleanup Cleanup;
	Cleanup.m_Dir = pDir;
	fs_listdir(pDir, CleanupCallback, 0, &Cleanup);
	return Cleanup.m_Removed;
}
