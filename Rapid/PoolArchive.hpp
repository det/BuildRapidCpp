#pragma once

#include "ArchiveEntry.hpp"
#include "BitArray.hpp"
#include "Crc32.hpp"
#include "Md5.hpp"
#include "FileEntry.hpp"
#include "Store.hpp"

#include <map>
#include <string>
#include <stdexcept>
#include <vector>

namespace Rapid {

class PoolArchiveT
{
	private:
	StoreT & mStore;
	std::map<std::string, FileEntryT> mEntries;

	public:
	PoolArchiveT(StoreT & Store);

	void clear();
	void load(DigestT const & Digest);
	void add(std::string Name, FileEntryT const & Entry);
	void remove(std::string Name);
	void removePrefix(std::string Prefix);
	ArchiveEntryT save();
	DigestT getDigest();
	ChecksumT getChecksum();
	void makeZip(std::string const & Path);

	template<typename FunctorT>
	void iterate(BitArrayT const & Bits, FunctorT Functor)
	{
		if (Bits.size() < mEntries.size()) throw std::runtime_error{"Not enough bits"};

		auto BitIndex = 0;
		for (auto & Pair : mEntries)
		{
			if (!Bits[BitIndex++]) continue;
			Functor(Pair.second);
		}
	}
};

}
