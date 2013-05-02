#include "Gzip.hpp"

#include <stdexcept>

namespace BuildRapid {

GzipT::GzipT()
:
	mFile{nullptr}
{}

GzipT::GzipT(std::string const & Path, char const * Mode)
{
	mFile = gzopen(Path.c_str(), Mode);
	if (mFile == nullptr) throw std::runtime_error{"Error opening gzip"};
}

void GzipT::open(std::string const & Path, char const * Mode)
{
	if (mFile == nullptr) throw std::runtime_error{"Gzip already open"};
	mFile = gzopen(Path.c_str(), Mode);
	if (mFile == nullptr) throw std::runtime_error{"Error opening gzip"};
}

GzipT::~GzipT()
{
	gzclose(mFile);
}

void GzipT::close()
{
	gzclose(mFile);
	mFile = nullptr;
}

void GzipT::readExpected(void * Buffer, unsigned Length)
{
	auto ReadBytes = gzread(mFile, static_cast<char *>(Buffer), Length);
	if (ReadBytes == -1) throw std::runtime_error{"Error reading gzip"};
	if (static_cast<unsigned>(ReadBytes) != Length) throw std::runtime_error{"Error reading gzip"};
}

// o/` Hey, I just met you, and this is crazy, but here's my buffer, so read me maybe o/`

bool GzipT::readMaybe(void * Buffer, unsigned Length)
{
	auto ReadBytes = gzread(mFile, static_cast<char *>(Buffer), Length);
	if (ReadBytes == -1) throw std::runtime_error{"Error reading gzip"};
	if (static_cast<unsigned>(ReadBytes) != Length) return false;
	else return true;
}

void GzipT::write(void const * Buffer, unsigned Length)
{
	auto Error = gzwrite(mFile, Buffer, Length);
	if (Error == 0) throw std::runtime_error{"Error writing gzip"};
}

}