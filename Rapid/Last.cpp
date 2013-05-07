#include "Last.hpp"

#include "Marshal.hpp"
#include "TempFile.hpp"

#include <sys/stat.h>
#include <sys/types.h>

namespace Rapid {

void LastT::save(LastT const & Last, StoreT & Store, std::string const & Prefix,
	std::string const & Base)
{
	TempFileT Temp{Store};
	unsigned char Buffer[4];
	Marshal::packLittle(Last.RevisionNum, Buffer);
	Temp.getOut().write(Buffer, 4);
	Temp.getOut().write(Last.Digest.Buffer, 16);
	Temp.commit(Store.getLastPath(Prefix, Base));
}

LastT LastT::load(StoreT & Store, std::string const & Prefix, std::string const & Base)
{
	LastT Last;

	auto Path = Store.getLastPath(Prefix, Base);

	struct stat Stats;
	auto Error = stat(Path.c_str(), &Stats);
	if (Error == -1)
	{
		// If there was no last for this prefix/base, we use revision 0
		if (errno == ENOENT)
		{
			Last.RevisionNum = 0;
			return Last;
		}
		else throw std::runtime_error{"Error loading last"};
	}


	GzipT In{Path, "rb"};
	unsigned char Buffer[4];
	In.readExpected(Buffer, 4);
	Marshal::unpackLittle(Last.RevisionNum, Buffer);
	In.readExpected(Last.Digest.Buffer, 16);

	return Last;
}

}
