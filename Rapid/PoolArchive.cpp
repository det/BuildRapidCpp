#include "PoolArchive.hpp"

#include "Lua.hpp"
#include "Marshal.hpp"
#include "Gzip.hpp"
#include "TempFile.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_set>

#include <zip.h>

namespace Rapid {

PoolArchiveT::PoolArchiveT(StoreT & Store)
:
	mStore(Store)
{}

void PoolArchiveT::clear()
{
	mEntries.clear();
}

void PoolArchiveT::load(DigestT const & Digest)
{
	GzipT In{mStore.getSdpPath(Digest), "rb"};
	unsigned char Length;
	char Name[255];
	unsigned char Checksum[4];
	unsigned char Size[4];

	while (In.readMaybe(&Length, 1))
	{
		std::pair<std::string, FileEntryT> Pair;
		In.readExpected(Name, Length);
		In.readExpected(Pair.second.Digest.Buffer, 16);
		In.readExpected(Checksum, 4);
		In.readExpected(Size, 4);
		Pair.first = {Name, Length};
		Marshal::unpackLittle(Pair.second.Checksum, Checksum);
		Marshal::unpackLittle(Pair.second.Size, Size);
		mEntries.insert(Pair);
	}
}

namespace {

std::unordered_set<std::string> const IgnoredDepends = {
	"Spring Bitmaps", "Spring Cursors", "Map Helper v1", "Spring content v1",
	"TA Content version 2", "tatextures.sdz", "TA Textures v0.62", "tacontent.sdz",
	"springcontent.sdz", "cursors.sdz"};

}

ArchiveEntryT PoolArchiveT::save()
{
	TempFileT TempFile{mStore};
	auto & Out = TempFile.getOut();

	unsigned char ChecksumBuffer[4];
	unsigned char SizeBuffer[4];

	for (auto & Pair : mEntries)
	{
		Marshal::packLittle(Pair.second.Checksum, ChecksumBuffer);
		Marshal::packLittle(Pair.second.Size, SizeBuffer);
		unsigned char Length = Pair.first.size();
		Out.write(&Length, 1);
		Out.write(Pair.first.data(), Length);
		Out.write(Pair.second.Digest.Buffer, 16);
		Out.write(ChecksumBuffer, 4);
		Out.write(SizeBuffer, 4);
	}

	// Gather mod information
	auto Iter = mEntries.find("modinfo.lua");
	if (Iter == mEntries.end()) throw std::runtime_error{"Archive missing modinfo.lua"};
	auto Buffer = GzipT::readFile(mStore.getPoolPath(Iter->second.Digest));
	LuaT Lua;
	auto Modinfo = Lua.getModinfo(Buffer);

	auto Digest = getDigest();
	TempFile.commit(mStore.getSdpPath(Digest));

	// Ignore blacklisted depends
	std::size_t Destination = 0;
	auto IgnoredEnd = IgnoredDepends.end();
	for (std::size_t I = 0, E = Modinfo.Depends.size(); I != E; ++I)
	{
		if (IgnoredDepends.find(Modinfo.Depends[I]) != IgnoredEnd) continue;
		Modinfo.Depends[Destination] = Modinfo.Depends[I];
		++Destination;
	}
	Modinfo.Depends.resize(Destination);

	ArchiveEntryT Entry;
	Entry.Digest = Digest;
	Entry.Depends = std::move(Modinfo.Depends);
	Entry.Name = std::move(Modinfo.Name);

	// Append version to modname if it doesn't already exist
	if (Entry.Name.find(Modinfo.Version) == std::string::npos)
	{
		Entry.Name += ' ';
		Entry.Name += Modinfo.Version;
	}

	return Entry;
}

void PoolArchiveT::add(std::string Name, FileEntryT const & Entry)
{
	for (auto & Char : Name) Char = std::tolower(Char);
	auto Pair = mEntries.insert({std::move(Name), Entry});
	// Overwrite the old entry if it already existed
	if (!Pair.second) Pair.first->second = Entry;
}

void PoolArchiveT::remove(std::string Name)
{
	for (auto & Char : Name) Char = std::tolower(Char);
	auto Iter = mEntries.find(Name);
	auto End = mEntries.end();

	if (Iter == End) throw std::runtime_error{"Attempt to remove non-existent key"};
	mEntries.erase(Iter);
}

void PoolArchiveT::removePrefix(std::string Prefix)
{
	for (auto & Char : Prefix) Char = std::tolower(Char);
	auto Iter = mEntries.lower_bound(Prefix);
	auto End = mEntries.end();
	auto PrefixBegin = Prefix.begin();
	auto PrefixEnd = Prefix.end();
	auto PrefixSize = Prefix.size();

	while (Iter != End)
	{
		if (Iter == End) break;
		auto NameBegin = Iter->first.begin();
		auto NameSize = Iter->first.size();
		if (NameSize < PrefixSize) break;
		if (!std::equal(PrefixBegin, PrefixEnd, NameBegin)) break;
		Iter = mEntries.erase(Iter);
	}
}

DigestT PoolArchiveT::getDigest()
{
	Md5T Md5;

	for (auto & Pair : mEntries)
	{
		Md5T NameMd5;
		NameMd5.update(Pair.first.data(), Pair.first.size());
		auto NameDigest = NameMd5.final();
		Md5.update(NameDigest.Buffer, 16);
		Md5.update(Pair.second.Digest.Buffer, 16);
	}

	return Md5.final();
}

ChecksumT PoolArchiveT::getChecksum()
{
	Crc32T Crc32;

	for (auto & Pair : mEntries)
	{
		unsigned char ChecksumBuffer[4];
		Crc32T NameCrc32;
		NameCrc32.update(Pair.first.data(), Pair.first.size());
		auto NameChecksum = NameCrc32.final();
		Marshal::packBig(NameChecksum, ChecksumBuffer);
		Crc32.update(ChecksumBuffer, 4);
		Marshal::packBig(Pair.second.Checksum, ChecksumBuffer);
		Crc32.update(ChecksumBuffer, 4);
	}

	return Crc32.final();
}

namespace {

struct UserDataT
{
	gzFile File;
	StoreT const & Store;
	FileEntryT const & Entry;
};

zip_int64_t handleZip(void * State, void * Data, zip_uint64_t Length, enum zip_source_cmd Cmd)
{
	auto & UserData = *static_cast<UserDataT *>(State);

	switch (Cmd)
	{

	case ZIP_SOURCE_OPEN:
	{
		UserData.File = gzopen(UserData.Store.getPoolPath(UserData.Entry.Digest).c_str(), "rb");
		if (UserData.File == nullptr) return -1;
		else return 0;
	} break;

	case ZIP_SOURCE_READ:
	{
		return gzread(UserData.File, Data, Length);
	} break;

	case ZIP_SOURCE_CLOSE:
	{
		return gzclose(UserData.File);
	} break;

	case ZIP_SOURCE_STAT:
	{
		auto & Stat = *static_cast<struct zip_stat *>(Data);
		zip_stat_init(&Stat);
		Stat.size = UserData.Entry.Size;
		return sizeof(struct zip_stat);
	} break;

	case ZIP_SOURCE_ERROR:
	{		
		return sizeof(int) * 2;
	} break;

	case ZIP_SOURCE_FREE:
	{
		delete &UserData;
		return 0;
	} break;

	// Unnecessary, but squelches a GCC warning
	default: return 0;

	}
}

}

void PoolArchiveT::makeZip(std::string const & Path)
{
	int Error;
	auto Zip = zip_open(Path.c_str(), ZIP_CREATE | ZIP_EXCL, &Error);
	if (Zip == nullptr) throw std::runtime_error{"Unable to create zip"};

	// Begin exception free zone (except new)
	for (auto & Pair : mEntries)
	{
		auto UserData = new UserDataT{nullptr, mStore, Pair.second};
		auto Source = zip_source_function(Zip, &handleZip, UserData);
		zip_add(Zip, Pair.first.c_str(), Source);
	}

	Error = zip_close(Zip);
	// End exception free zone
	if (Error != 0) throw std::runtime_error{"Unable to create zip"};

}

}
