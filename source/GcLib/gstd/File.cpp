#include "source/GcLib/pch.h"

#include "File.hpp"
#include "GstdUtility.hpp"

#if defined(DNH_PROJ_EXECUTOR)
#include "Logger.hpp"
#endif

#if defined(DNH_PROJ_EXECUTOR) || defined(DNH_PROJ_FILEARCHIVER)
#include "ArchiveFile.hpp"
#endif

using namespace gstd;

//*******************************************************************
//ByteBuffer
//*******************************************************************
ByteBuffer::ByteBuffer() : offset_(0) {}
ByteBuffer::ByteBuffer(size_t size) : ByteBuffer() {
	SetSize(size);
}
ByteBuffer::ByteBuffer(std::stringstream& src) : ByteBuffer() {
	std::streampos org = src.tellg();
	const auto& srcData = src.str();

	offset_ = org;

	data_.resize(srcData.size());
	memcpy(data_.data(), srcData.data(), srcData.size());
}

void ByteBuffer::Clear() {
	data_.clear();
	offset_ = 0;
}

void ByteBuffer::SetSize(size_t size) {
	data_.resize(size);
}
void ByteBuffer::Reserve(size_t newReserve) {
	data_.reserve(newReserve);
}

void ByteBuffer::Seek(size_t pos) {
	offset_ = pos;
	if (offset_ > GetSize())
		offset_ = GetSize();
}
DWORD ByteBuffer::Write(LPVOID buf, DWORD size) {
	if (size == 0) return 0;

	if (offset_ + size >= data_.size())
		data_.resize(offset_ + size);

	memcpy(&data_[offset_], buf, size);
	offset_ += size;

	return size;
}
DWORD ByteBuffer::Read(LPVOID buf, DWORD size) {
	if (offset_ + size > GetSize())
		return 0;

	memcpy(buf, &data_[offset_], size);
	offset_ += size;

	return size;
}

[[nodiscard]] char* ByteBuffer::GetPointer(size_t offset) {
#if _DEBUG
	if (offset > GetSize())
		throw gstd::wexception("ByteBuffer: Index out of bounds.");
#endif
	return reinterpret_cast<char*>(&data_[offset]);
}
[[nodiscard]] const char* ByteBuffer::GetPointer(size_t offset) const {
#if _DEBUG
	if (offset > GetSize())
		throw gstd::wexception("ByteBuffer: Index out of bounds.");
#endif
	return reinterpret_cast<const char*>(&data_[offset]);
}

//*******************************************************************
//File
//*******************************************************************
std::wstring File::lastError_ = L"";
std::map<std::wstring, size_t> File::mapFileUseCount_ = {};

bool File::CreateFileDirectory(const std::wstring& path) {
#ifdef __L_STD_FILESYSTEM
	stdfs::path dir = stdfs::path(path).parent_path();
	if (stdfs::exists(dir)) return true;

	return stdfs::create_directories(dir);
#else
	std::wstring dir = PathProperty::GetFileDirectory(path_);
	if (File::IsExists(dir)) return true;

	std::vector<std::wstring> str = StringUtility::Split(dir, L"\\/");
	std::wstring tPath = L"";
	for (int iDir = 0; iDir < str.size(); iDir++) {
		tPath += str[iDir] + L"\\";
		WIN32_FIND_DATA fData;
		HANDLE hFile = ::FindFirstFile(tPath.c_str(), &fData);
		if (hFile == INVALID_HANDLE_VALUE) {
			SECURITY_ATTRIBUTES attr;
			attr.nLength = sizeof(SECURITY_ATTRIBUTES);
			attr.lpSecurityDescriptor = nullptr;
			attr.bInheritHandle = FALSE;
			::CreateDirectory(tPath.c_str(), &attr);
		}
		::FindClose(hFile);
	}
#endif
	return true;
}
bool File::IsExists(const std::wstring& path) {
#ifdef __L_STD_FILESYSTEM
	path_t _p = path;
	bool exist = stdfs::exists(_p);
	stdfs::file_status status = stdfs::status(_p);
	bool res = exist && stdfs::is_regular_file(status);
#else
	bool res = PathFileExists(path.c_str()) == TRUE;
#endif
	return res;
}
bool File::IsDirectory(const std::wstring& path) {
#ifdef __L_STD_FILESYSTEM
	path_t _p = path;
	bool exist = stdfs::exists(_p);
	stdfs::file_status status = stdfs::status(_p);
	bool res = exist && stdfs::is_directory(status);
#else
	WIN32_FIND_DATA fData;
	HANDLE hFile = ::FindFirstFile(path.c_str(), &fData);
	bool res = hFile != INVALID_HANDLE_VALUE ? true : false;
	if (res) res = (FILE_ATTRIBUTE_DIRECTORY & fData.dwFileAttributes) > 0;

	::FindClose(hFile);
#endif
	return res;
}

bool File::IsEqualsPath(const std::wstring& path1, const std::wstring& path2) {
#ifdef __L_STD_FILESYSTEM
	bool res = (path_t(path1) == path_t(path2));
#else
	path1 = PathProperty::GetUnique(path1);
	path2 = PathProperty::GetUnique(path2);
	bool res = (wcscmp(path1.c_str(), path2.c_str()) == 0);
#endif
	return res;
}
std::vector<std::wstring> File::GetFilePathList(const std::wstring& dir, bool bSearchArchive) {
	std::set<std::wstring> res;

#ifdef __L_STD_FILESYSTEM
	path_t p = dir;
	if (stdfs::exists(p) && stdfs::is_directory(p)) {
		for (auto& itr : stdfs::directory_iterator(p)) {
			if (!itr.is_directory())
				res.insert(PathProperty::ReplaceYenToSlash(itr.path()));
		}
	}
#else
	WIN32_FIND_DATA data;
	HANDLE hFind;
	std::wstring findDir = dir + L"*.*";
	hFind = FindFirstFile(findDir.c_str(), &data);
	do {
		std::wstring name = data.cFileName;
		if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
			(name != L".." && name != L".")) {
			//ディレクトリ
			std::wstring tDir = dir + name;
			tDir += L"\\";

			continue;
		}
		if (wcscmp(data.cFileName, L"..") == 0 || wcscmp(data.cFileName, L".") == 0)
			continue;

		//ファイル
		std::wstring path = dir + name;

		res.push_back(path);

	} while (FindNextFile(hFind, &data));
	FindClose(hFind);
#endif

#if defined(DNH_PROJ_EXECUTOR)
	if (bSearchArchive) {
		std::wstring moduleDir = PathProperty::GetModuleDirectory();
		std::wstring dirUnique = PathProperty::GetUnique(dir);
		auto listArchiveEntry = FileManager::GetBase()->GetArchiveFilesInDirectory(dirUnique, false);
		for (auto& iEntry : listArchiveEntry) {
			res.insert(moduleDir + iEntry->fullPath);
		}
	}
#endif

	return std::vector<std::wstring>(res.begin(), res.end());
}
std::vector<std::wstring> File::GetDirectoryPathList(const std::wstring& dir, bool bSearchArchive) {
	std::set<std::wstring> res;

#ifdef __L_STD_FILESYSTEM
	path_t p = dir;
	if (stdfs::exists(p) && stdfs::is_directory(p)) {
		for (auto& itr : stdfs::directory_iterator(dir)) {
			if (itr.is_directory()) {
				std::wstring str = PathProperty::AppendSlash(
					PathProperty::ReplaceYenToSlash(itr.path()));
				res.insert(str);
			}
		}
	}
#else
	WIN32_FIND_DATA data;
	HANDLE hFind;
	std::wstring findDir = dir + L"*.*";
	hFind = FindFirstFile(findDir.c_str(), &data);
	do {
		std::wstring name = data.cFileName;
		if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
			(name != L".." && name != L".")) {
			//ディレクトリ
			std::wstring tDir = dir + name;
			tDir += L"\\";
			res.push_back(tDir);
			continue;
		}
		if (wcscmp(data.cFileName, L"..") == 0 || wcscmp(data.cFileName, L".") == 0)
			continue;

		//ファイル

	} while (FindNextFile(hFind, &data));
	FindClose(hFind);
#endif

#if defined(DNH_PROJ_EXECUTOR)
	if (bSearchArchive) {
		std::wstring moduleDir = PathProperty::GetModuleDirectory();
		std::wstring dirUnique = PathProperty::GetUnique(dir);
		auto listArchiveEntry = FileManager::GetBase()->GetArchiveSubDirectoriesInDirectory(dirUnique);
		for (auto& iDir : listArchiveEntry) {
			std::wstring dir = PathProperty::GetFileDirectory(moduleDir + iDir);
			res.insert(dir);
		}
	}
#endif

	return std::vector<std::wstring>(res.begin(), res.end());
}

File::File() {
	path_ = L"";
	perms_ = 0;
}
File::File(const std::wstring& path) : File() {
	path_ = path;
}
File::~File() {
	Close();
}

void File::Delete() {
	Close();
	::DeleteFile(path_.c_str());
}
bool File::IsExists() {
	if (IsOpen()) return true;
	return IsExists(path_);
}
bool File::IsDirectory() {
	return IsDirectory(path_);
}
size_t File::GetSize() {
	size_t res = 0;
	if (IsOpen()) {
		size_t prev = GetFilePointer();
		SetFilePointerEnd();
		size_t size = GetFilePointer();
		Seek(prev, std::ios::beg);
		return size;
	}

	//File not opened
	try {
		res = stdfs::file_size(path_);
	}
	catch (stdfs::filesystem_error&) {
		res = 0;
	}
	return res;
}

bool File::Open() {
	return this->Open(AccessType::READ);
}
bool File::Open(DWORD typeAccess) {
	this->Close();

	constexpr const std::ios::openmode ACC_READ = std::ios::in;
	constexpr const std::ios::openmode ACC_READWRITE = std::ios::in | std::ios::out;
	constexpr const std::ios::openmode ACC_NEWWRITEONLY = std::ios::out | std::ios::trunc;
	
	std::ios::openmode modeAccess = 0;
	if (typeAccess == READ)
		modeAccess = ACC_READ;
	else if (typeAccess == WRITE)
		modeAccess = ACC_READWRITE;
	else if (typeAccess == WRITEONLY)
		modeAccess = ACC_NEWWRITEONLY;

	if (modeAccess == 0) return false;
	perms_ = typeAccess;

	Close();

	hFile_.exceptions(std::ios::failbit);
	try {
		try {
			hFile_.open(path_, modeAccess | std::ios::binary);
		}
		catch (std::system_error& e) {
			if (typeAccess == WRITE)	//Open failed, file might not exist, try creating one
				hFile_.open(path_, ACC_NEWWRITEONLY | std::ios::in | std::ios::binary);
			else throw e;
		}
	}
	catch (std::system_error&) {
		int code = errno;
		lastError_ = StringUtility::FormatToWide("%s (code=%d)", 
			strerror(code), code);
	}
	hFile_.exceptions(0);

	if (hFile_.is_open()) {
#if _DEBUG
		auto itr = mapFileUseCount_.find(path_);
		if (itr != mapFileUseCount_.end())
			++(itr->second);
		else
			mapFileUseCount_.insert(std::make_pair(path_, 1));
#endif
		return true;
	}
	return false;
}
void File::Close() {
#if _DEBUG
	if (IsOpen()) {
		auto itr = mapFileUseCount_.find(path_);
		if (itr != mapFileUseCount_.end())
			--(itr->second);
	}
#endif
	hFile_ = std::fstream();
	perms_ = 0;
}

DWORD File::Read(LPVOID buf, DWORD size) {
	if (!IsOpen()) return 0;
	hFile_.clear();
	hFile_.read((char*)buf, size);
	return hFile_.gcount();
}
DWORD File::Write(LPVOID buf, DWORD size) {
	if (!IsOpen()) return 0;
	hFile_.write((char*)buf, size);
	return hFile_.good();
}

bool File::Seek(size_t offset, std::ios::seekdir way, AccessType type) {
	if (!IsOpen()) return false;
	hFile_.clear();
	if (type == READ)
		hFile_.seekg(offset, way);
	else
		hFile_.seekp(offset, way);
	return hFile_.good();
}
size_t File::GetFilePointer(AccessType type) {
	if (!IsOpen()) return 0;
	if (type == READ) return hFile_.tellg();
	else return hFile_.tellp();
}

//*******************************************************************
//FileManager
//*******************************************************************
FileManager* FileManager::thisBase_ = nullptr;
FileManager::FileManager() {}
FileManager::~FileManager() {
#if defined(DNH_PROJ_EXECUTOR)
	EndLoadThread();
#endif
}
bool FileManager::Initialize() {
	if (thisBase_) return false;
	thisBase_ = this;

#if defined(DNH_PROJ_EXECUTOR)
	threadLoad_ = shared_ptr<LoadThread>(new LoadThread());
	threadLoad_->Start();
#endif

	return true;
}

#if defined(DNH_PROJ_EXECUTOR)
void FileManager::EndLoadThread() {

	{
		Lock lock(lock_);
		if (threadLoad_ == nullptr) return;
		threadLoad_->Stop();
		threadLoad_->Join();
		threadLoad_ = nullptr;
	}
}

bool FileManager::AddArchiveFile(const std::wstring& archivePath, size_t readOff) {
	//Archive already loaded
	if (mapArchiveFile_.find(archivePath) != mapArchiveFile_.end()) {
		std::wstring log = StringUtility::Format(
			L"Archive already loaded [%s]",
			PathProperty::ReduceModuleDirectory(archivePath).c_str());
		Logger::WriteTop(log);
		return true;
	}

	std::wstring moduleDir = PathProperty::GetModuleDirectory();
	std::wstring archiveDir = PathProperty::GetFileDirectory(archivePath);

	if (archiveDir.find(moduleDir) == std::wstring::npos) {
		std::wstring log = StringUtility::Format(
			L"Cannot load an archive file at [%s]",
			archiveDir.c_str());
		Logger::WriteTop(log);
		return false;
	}

	unique_ptr<ArchiveFile> archive(new ArchiveFile(archivePath, readOff));
	if (!archive->Open())
		return false;

	for (auto& [path, entry] : archive->GetEntryMap()) {
		// path is without module dir
		std::wstring fullEntryPath = moduleDir + path;

		ArchiveFileEntry* pEntry = &entry;

		pEntry->fullPath = path;

		auto itrFind = mapArchiveEntries_.find(path);
		if (itrFind != mapArchiveEntries_.end()) {
			std::wstring log = StringUtility::Format(
				L"Archive file entry already exists [%s]",
				path.c_str());
			Logger::WriteTop(log);
			throw wexception(log);
		}
		else {
			ArchiveEntryStore entry = {
				archive.get(),
				pEntry,
				nullptr
			};
			mapArchiveEntries_[path] = MOVE(entry);
		}
	}

	mapArchiveFile_[archivePath] = MOVE(archive);
	return true;
}
bool FileManager::RemoveArchiveFile(const std::wstring& archivePath) {
	auto itr = mapArchiveFile_.find(archivePath);
	if (itr != mapArchiveFile_.end()) {
		ArchiveFile* archive = itr->second.get();
		archive->Close();
		
		mapArchiveFile_.erase(itr);
		return true;
	}
	return false;
}
ArchiveFile* FileManager::GetArchiveFile(const std::wstring& archivePath) {
	auto itrFind = mapArchiveFile_.find(archivePath);
	if (itrFind != mapArchiveFile_.end())
		return itrFind->second.get();
	return nullptr;
}
FileManager::ArchiveEntryStore* FileManager::GetArchiveFileEntry(const std::wstring& path) {
	std::wstring pathNoModule = PathProperty::GetPathWithoutModuleDirectory(path);

	auto itrFind = mapArchiveEntries_.find(pathNoModule);
	if (itrFind != mapArchiveEntries_.end())
		return &itrFind->second;
	return nullptr;
}

std::vector<ArchiveFileEntry*> FileManager::GetArchiveFilesInDirectory(const std::wstring& dir, bool bSubDirectory) {
	std::vector<ArchiveFileEntry*> res;

	std::wstring dirNoModule = PathProperty::GetPathWithoutModuleDirectory(dir);
	dirNoModule = PathProperty::AppendSlash(dirNoModule);

	for (auto& [_, entryData] : mapArchiveEntries_) {
		ArchiveFileEntry* pEntry = entryData.entry;

		if (!bSubDirectory) {
			std::wstring entryDir = PathProperty::GetFileDirectory(pEntry->fullPath);
			if (entryDir == dirNoModule) {
				res.push_back((ArchiveFileEntry*)pEntry);
			}
		}
		else {
			if (pEntry->fullPath.starts_with(dirNoModule)) {
				res.push_back((ArchiveFileEntry*)pEntry);
			}
		}
	}

	return res;
}
std::set<std::wstring> FileManager::GetArchiveSubDirectoriesInDirectory(const std::wstring& dir) {
	std::set<std::wstring> res;

	std::wstring dirNoModule = PathProperty::GetPathWithoutModuleDirectory(dir);
	dirNoModule = PathProperty::AppendSlash(dirNoModule);

	for (auto& [_, entryData] : mapArchiveEntries_) {
		ArchiveFileEntry* pEntry = entryData.entry;

		if (pEntry->fullPath.starts_with(dirNoModule)) {
			size_t pos = pEntry->fullPath.find_first_of(L"/", dirNoModule.size());

			if (pos != std::wstring::npos) {	// If the substr is a directory
				std::wstring subDir = pEntry->fullPath.substr(0, pos);
				subDir = PathProperty::AppendSlash(subDir);

				res.insert(subDir);
			}
		}
	}

	return res;
}

bool FileManager::IsArchiveFileExists(const std::wstring& path) {
	/*
	std::wstring moduleDir = PathProperty::GetModuleDirectory();
	if (path.find(moduleDir) == std::wstring::npos)
		return false;
	*/

	auto pEntry = GetArchiveFileEntry(path);
	return pEntry != nullptr && pEntry->entry != nullptr;
}
bool FileManager::IsArchiveDirectoryExists(const std::wstring& _dir) {
	std::wstring moduleDir = PathProperty::GetModuleDirectory();
	if (_dir.find(moduleDir) != std::wstring::npos) {
		std::wstring dir = PathProperty::AppendSlash(_dir.substr(moduleDir.size()));

		for (auto& [path, _] : mapArchiveEntries_) {
			std::wstring entryDir = PathProperty::GetFileDirectory(path);
			if (entryDir.starts_with(dir))
				return true;
		}
	}
	return false;
}

bool FileManager::ClearArchiveFileCache() {
	mapArchiveFile_.clear();
	mapArchiveEntries_.clear();
	return true;
}
#endif

#if defined(DNH_PROJ_EXECUTOR) || defined(DNH_PROJ_FILEARCHIVER)
shared_ptr<FileReader> FileManager::GetFileReader(const std::wstring& path) {
	std::wstring pathAsUnique = PathProperty::GetUnique(path);

	shared_ptr<FileReader> res = nullptr;
	if (File::IsExists(pathAsUnique)) {
		shared_ptr<File> fileRaw(new File(pathAsUnique));
		res.reset(new ManagedFileReader(fileRaw, nullptr));
	}
#if defined(DNH_PROJ_EXECUTOR)
	else {
		// Cannot find a physical file, search in the archive entries.

		if (auto pEntry = GetArchiveFileEntry(pathAsUnique)) {
			shared_ptr<File> file = pEntry->archive->GetFile();
			res.reset(new ManagedFileReader(file, pEntry->entry));
		}
	}
#endif

	if (res) res->_SetOriginalPath(path);
	return res;
}

ByteBuffer* FileManager::_GetByteBuffer(ArchiveFileEntry* entry) {
	ByteBuffer* res = nullptr;

	try {
		Lock lock(lock_);

		const std::wstring& fullPath = entry->fullPath;	// without module dir

		auto itr = mapArchiveEntries_.find(fullPath);
		if (itr != mapArchiveEntries_.end()) {
			auto archive = itr->second.archive;
			auto& buf = itr->second.dataBuffer;

			// Buffer for this entry doesn't yet exist, create new one
			if (buf == nullptr) {
				buf = MOVE(archive->CreateEntryBuffer(entry));
			}

			res = buf.get();
		}
	}
	catch (...) {}

	return res;
}
void FileManager::_ReleaseByteBuffer(ArchiveFileEntry* entry) {
	{
		Lock lock(lock_);

		const std::wstring& fullPath = entry->fullPath;	// Without module dir

		auto itr = mapArchiveEntries_.find(fullPath);
		if (itr != mapArchiveEntries_.end()) {
			itr->second.dataBuffer = nullptr;
		}
	}
}
#endif

#if defined(DNH_PROJ_EXECUTOR)
void FileManager::AddLoadThreadEvent(shared_ptr<FileManager::LoadThreadEvent> event) {
	{
		Lock lock(lock_);
		if (threadLoad_)
			threadLoad_->AddEvent(event);
	}
}
void FileManager::AddLoadThreadListener(FileManager::LoadThreadListener* listener) {
	{
		Lock lock(lock_);
		if (threadLoad_)
			threadLoad_->AddListener(listener);
	}
}
void FileManager::RemoveLoadThreadListener(FileManager::LoadThreadListener* listener) {
	{
		Lock lock(lock_);
		if (threadLoad_)
			threadLoad_->RemoveListener(listener);
	}
}
void FileManager::WaitForThreadLoadComplete() {
	while (!threadLoad_->IsThreadLoadComplete())
		::Sleep(1);
}

//FileManager::LoadThread
FileManager::LoadThread::LoadThread() {}
FileManager::LoadThread::~LoadThread() {}
void FileManager::LoadThread::_Run() {
	while (this->GetStatus() == RUN) {
		signal_.Wait(10);

		while (this->GetStatus() == RUN) {
			shared_ptr<FileManager::LoadThreadEvent> event = nullptr;
			{
				Lock lock(lockEvent_);

				if (listEvent_.size() == 0) break;
				event = listEvent_.front();
				listEvent_.pop_front();
			}
			if (event) {
				Lock lock(lockListener_);

				for (auto itr = listListener_.begin(); itr != listListener_.end(); itr++) {
					FileManager::LoadThreadListener* listener = (*itr);
					if (event->GetListener() == listener)
						listener->CallFromLoadThread(event);
				}
			}
		}
		::Sleep(10);
	}

	{
		Lock lock(lockListener_);
		listListener_.clear();
	}
}
void FileManager::LoadThread::Stop() {
	Thread::Stop();
	signal_.SetSignal();
}
bool FileManager::LoadThread::IsThreadLoadComplete() {
	bool res = false;
	{
		Lock lock(lockEvent_);
		res = listEvent_.size() == 0;
	}
	return res;
}
bool FileManager::LoadThread::IsThreadLoadExists(std::wstring path) {
	bool res = false;
	{
		Lock lock(lockEvent_);
		//res = listPath_.find(path) != listPath_.end();
	}
	return res;
}
void FileManager::LoadThread::AddEvent(shared_ptr<FileManager::LoadThreadEvent> event) {
	{
		Lock lock(lockEvent_);
		std::wstring path = event->GetPath();
		if (IsThreadLoadExists(path)) return;
		listEvent_.push_back(event);
		//listPath_.insert(path);
		signal_.SetSignal();
		signal_.SetSignal(false);
	}
}
void FileManager::LoadThread::AddListener(FileManager::LoadThreadListener* listener) {
	{
		Lock lock(lockListener_);

		for (auto itr = listListener_.begin(); itr != listListener_.end(); ++itr) {
			if (*itr == listener) return;
		}
		listListener_.push_back(listener);
	}
}
void FileManager::LoadThread::RemoveListener(FileManager::LoadThreadListener* listener) {
	{
		Lock lock(lockListener_);

		for (auto itr = listListener_.begin(); itr != listListener_.end(); ++itr) {
			if (*itr != listener) continue;
			listListener_.erase(itr);
			break;
		}
	}
}
#endif

#if defined(DNH_PROJ_EXECUTOR) || defined(DNH_PROJ_FILEARCHIVER)
//*******************************************************************
//ManagedFileReader
//*******************************************************************
ManagedFileReader::ManagedFileReader(shared_ptr<File> file, ArchiveFileEntry* entry) {
	offset_ = 0;
	file_ = file;

	entry_ = entry;

	if (entry_ == nullptr) {
		type_ = TYPE_NORMAL;
	}
	else {
		switch (entry_->compressionType) {
		case ArchiveFileEntry::CT_NONE:
			type_ = TYPE_ARCHIVED; break;
		case ArchiveFileEntry::CT_ZLIB:
			type_ = TYPE_ARCHIVED_COMPRESSED; break;
		}
	}
}
ManagedFileReader::~ManagedFileReader() {
	Close();
}
bool ManagedFileReader::Open() {
	offset_ = 0;
	switch (type_) {
	case TYPE_NORMAL:
		return file_->Open();
	case TYPE_ARCHIVED:
	case TYPE_ARCHIVED_COMPRESSED:
		buffer_ = FileManager::GetBase()->_GetByteBuffer(entry_);
		return buffer_ != nullptr;
	}
	return false;
}
void ManagedFileReader::Close() {
	if (file_) file_->Close();
	if (buffer_) {
		buffer_ = nullptr;
		//FileManager::GetBase()->_ReleaseByteBuffer(entry_);
	}
}
size_t ManagedFileReader::GetFileSize() {
	switch (type_) {
	case TYPE_NORMAL:
		return file_->GetSize();
	case TYPE_ARCHIVED:
	case TYPE_ARCHIVED_COMPRESSED:
		return buffer_ ? entry_->sizeFull : 0;
	}
	return 0;
}
DWORD ManagedFileReader::Read(LPVOID buf, DWORD size) {
	DWORD res = 0;
	if (type_ == TYPE_NORMAL) {
		res = file_->Read(buf, size);
	}
	else if (type_ == TYPE_ARCHIVED || type_ == TYPE_ARCHIVED_COMPRESSED) {
		size_t read = size;
		if (buffer_->GetSize() < offset_ + size) {
			read = buffer_->GetSize() - offset_;
		}
		memcpy(buf, &buffer_->GetPointer()[offset_], read);
		res = read;
	}
	offset_ += res;
	return res;
}
bool ManagedFileReader::SetFilePointerBegin(File::AccessType type) {
	bool res = false;
	offset_ = 0;
	if (type_ == TYPE_NORMAL) {
		res = file_->SetFilePointerBegin(type);
	}
	else if (type_ == TYPE_ARCHIVED || type_ == TYPE_ARCHIVED_COMPRESSED) {
		if (buffer_) {
			offset_ = 0;
			res = true;
		}
	}
	return res;
}
bool ManagedFileReader::SetFilePointerEnd(File::AccessType type) {
	bool res = false;
	if (type_ == TYPE_NORMAL) {
		res = file_->SetFilePointerEnd(type);
	}
	else if (type_ == TYPE_ARCHIVED || type_ == TYPE_ARCHIVED_COMPRESSED) {
		if (buffer_) {
			offset_ = buffer_->GetSize();
			res = true;
		}
	}
	return res;
}
bool ManagedFileReader::Seek(size_t offset, File::AccessType type) {
	bool res = false;
	if (type_ == TYPE_NORMAL) {
		res = file_->Seek(offset, std::ios::beg, type);
	}
	else if (type_ == TYPE_ARCHIVED || type_ == TYPE_ARCHIVED_COMPRESSED) {
		res = buffer_ != nullptr;
	}
	if (res) offset_ = offset;
	return res;
}
size_t ManagedFileReader::GetFilePointer(File::AccessType type) {
	size_t res = 0;
	if (type_ == TYPE_NORMAL) {
		res = file_->GetFilePointer(type);
	}
	else if (type_ == TYPE_ARCHIVED || type_ == TYPE_ARCHIVED_COMPRESSED) {
		if (buffer_) {
			res = offset_;
		}
	}
	return res;
}
bool ManagedFileReader::IsArchived() {
	return type_ != TYPE_NORMAL;
}
bool ManagedFileReader::IsCompressed() {
	return type_ == TYPE_ARCHIVED_COMPRESSED;
}
#endif

//*******************************************************************
//RecordEntry
//*******************************************************************
RecordEntry::RecordEntry() {
}
RecordEntry::~RecordEntry() {
}
size_t RecordEntry::_GetEntryRecordSize() {
	size_t res = 0;
	res += sizeof(uint32_t);
	res += key_.size();
	res += sizeof(uint32_t);
	res += buffer_.GetSize();
	return res;
}
void RecordEntry::_WriteEntryRecord(Writer& writer) {
	writer.WriteValue<uint32_t>(key_.size());
	if (key_.size() > 0)
	writer.Write(&key_[0], key_.size());

	uint32_t size = buffer_.GetSize();
	writer.WriteValue<uint32_t>(size);
	if (size > 0)
		writer.Write(buffer_.GetPointer(), size);
}
void RecordEntry::_ReadEntryRecord(Reader& reader) {
	key_.resize(reader.ReadValue<uint32_t>());
	if (key_.size() > 0)
	reader.Read(&key_[0], key_.size());

	uint32_t size = reader.ReadValue<uint32_t>();
	buffer_ = ByteBuffer(size);
	if (size > 0)
	reader.Read(buffer_.GetPointer(), size);
}

#if defined(DNH_PROJ_EXECUTOR) || defined(DNH_PROJ_CONFIG)
//*******************************************************************
//RecordBuffer
//*******************************************************************
RecordBuffer::RecordBuffer() {
}
RecordBuffer::~RecordBuffer() {
	this->Clear();
}

void RecordBuffer::Clear() {
	mapEntry_.clear();
}

RecordEntry* RecordBuffer::GetEntry(const std::string& key) {
	auto itr = mapEntry_.find(key);
	if (itr != mapEntry_.end())
		return &itr->second;
	return nullptr;
}
const RecordEntry* RecordBuffer::GetEntry(const std::string& key) const {
	auto itr = mapEntry_.find(key);
	if (itr != mapEntry_.cend())
		return &itr->second;
	return nullptr;
}
bool RecordBuffer::IsExists(const std::string& key) const {
	return mapEntry_.find(key) != mapEntry_.end();
}
std::vector<std::string> RecordBuffer::GetKeyList() const {
	std::vector<std::string> res;
	for (auto& [key, _] : mapEntry_)
		res.push_back(key);
	return res;
}

void RecordBuffer::Write(Writer& writer) {
	uint32_t countEntry = mapEntry_.size();
	writer.Write<uint32_t>(countEntry);

	for (auto& [name, entry] : mapEntry_) {
		entry._WriteEntryRecord(writer);
	}
}
void RecordBuffer::Read(Reader& reader) {
	this->Clear();

	uint32_t countEntry = 0;
	reader.Read<uint32_t>(countEntry);

	for (size_t iEntry = 0; iEntry < countEntry; ++iEntry) {
		RecordEntry entry;
		entry._ReadEntryRecord(reader);

		auto& key = entry.GetKey();
		mapEntry_[key] = MOVE(entry);
	}
}

bool RecordBuffer::WriteToFile(const std::wstring& path, uint32_t version, const char* header, size_t headerSize) {
	File file(path);
	if (!file.Open(File::AccessType::WRITEONLY))
		return false;

	file.Write((LPVOID)header, headerSize);
	file.WriteValue<uint32_t>(version);

	Write(file);

	return true;
}
bool RecordBuffer::ReadFromFile(const std::wstring& path, uint32_t version, const char* header, size_t headerSize) {
	File file(path);
	if (!file.Open())
		return false;

	if (file.GetSize() < headerSize + sizeof(uint32_t))
		return false;

	{
		auto fHead = make_unique<char[]>(headerSize);
		file.Read(fHead.get(), headerSize);

		if (memcmp(fHead.get(), header, headerSize) != 0)
			return false;
	}
	{
		uint32_t fVersion{};
		file.Read(&fVersion, sizeof(uint32_t));

		if (fVersion != version)
			return false;
	}

	Read(file);

	return true;
}

size_t RecordBuffer::GetEntrySize(const std::string& key) {
	auto itr = mapEntry_.find(key);
	if (itr == mapEntry_.end())
		return 0U;

	auto& buffer = itr->second.GetBufferRef();
	return buffer.GetSize();
}

bool RecordBuffer::GetRecord(const std::string& key, LPVOID buf, DWORD size) {
	auto itr = mapEntry_.find(key);
	if (itr == mapEntry_.end())
		return false;

	auto& buffer = itr->second.GetBufferRef();
	buffer.Seek(0);
	buffer.Read(buf, size);

	return true;
}
optional<std::string> RecordBuffer::GetRecordAsStringA(const std::string& key) {
	auto itr = mapEntry_.find(key);
	if (itr == mapEntry_.end())
		return {};

	auto& buffer = itr->second.GetBufferRef();
	buffer.Seek(0);

	std::string res;

	res.resize(buffer.GetSize());
	if (res.size() > 0) {
	buffer.Read(&res[0], buffer.GetSize());
	}

	return MOVE(res);
}
optional<RecordBuffer> RecordBuffer::GetRecordAsRecordBuffer(const std::string& key) {
	auto itr = mapEntry_.find(key);
	if (itr == mapEntry_.end())
		return {};

	auto& buffer = itr->second.GetBufferRef();
	buffer.Seek(0);

	RecordBuffer res;
	res.Read(buffer);

	return MOVE(res);
}
optional<ByteBuffer> RecordBuffer::GetRecordAsByteBuffer(const std::string& key) {
	auto itr = mapEntry_.find(key);
	if (itr == mapEntry_.end())
		return {};

	auto& buffer = itr->second.GetBufferRef();
	buffer.Seek(0);

	return ByteBuffer(buffer);
}

void RecordBuffer::SetRecord(const std::string& key, LPVOID buf, DWORD size) {
	RecordEntry entry;
	entry.SetKey(key);

	auto& buffer = entry.GetBufferRef();
	buffer.SetSize(size);
	buffer.Write(buf, size);

	mapEntry_[key] = MOVE(entry);
}
void RecordBuffer::SetRecordAsRecordBuffer(const std::string& key, RecordBuffer& record) {
	RecordEntry entry;
	entry.SetKey(key);

	record.Write(entry.buffer_);

	mapEntry_[key] = MOVE(entry);
}
void RecordBuffer::SetRecordAsByteBuffer(const std::string& key, ByteBuffer&& buffer) {
	RecordEntry entry;
	entry.SetKey(key);

	entry.buffer_ = MOVE(buffer);

	mapEntry_[key] = MOVE(entry);
}

//*******************************************************************
//PropertyFile
//*******************************************************************
PropertyFile::PropertyFile() {}
PropertyFile::~PropertyFile() {}
bool PropertyFile::Load(const std::wstring& path) {
	mapEntry_.clear();

	std::vector<char> text;

#if defined(DNH_PROJ_EXECUTOR) || defined(DNH_PROJ_FILEARCHIVER)
	FileManager* fileManager = FileManager::GetBase();
	if (fileManager) {
		shared_ptr<FileReader> reader = fileManager->GetFileReader(path);

		if (reader == nullptr || !reader->Open()) {
			Logger::WriteTop(L"PropertyFile::Load: " + ErrorUtility::GetFileNotFoundErrorMessage(path, true));
			return false;
		}

		size_t size = reader->GetFileSize();
		text.resize(size + 1);
		reader->Read(&text[0], size);
		text[size] = '\0';
	}
	else {
#endif
		File file(path);
		if (!file.Open()) return false;

		size_t size = file.GetSize();
		text.resize(size + 1);
		file.Read(&text[0], size);
		text[size] = '\0';
#if defined(DNH_PROJ_EXECUTOR) || defined(DNH_PROJ_FILEARCHIVER)
	}
#endif

	bool res = false;
	gstd::Scanner scanner(text);
	try {
		while (scanner.HasNext()) {
			gstd::Token& tok = scanner.Next();
			if (tok.GetType() != Token::Type::TK_ID) continue;
			std::wstring key = tok.GetElement();
			while (true) {
				tok = scanner.Next();
				if (tok.GetType() == Token::Type::TK_EQUAL) break;
				key += tok.GetElement();
			}

			std::wstring value;
			try {
				int posS = scanner.GetCurrentPointer();
				int posE = posS;
				while (true) {
					bool bEndLine = false;
					if (!scanner.HasNext()) {
						bEndLine = true;
					}
					else {
						tok = scanner.Next();
						bEndLine = tok.GetType() == Token::Type::TK_NEWLINE;
					}

					if (bEndLine) {
						posE = scanner.GetCurrentPointer();
						value = scanner.GetString(posS, posE);
						value = StringUtility::Trim(value);
						break;
					}
				}
			}
			catch (...) {}

			mapEntry_[key] = value;
		}

		res = true;
	}
	catch (gstd::wexception& e) {
		mapEntry_.clear();
#if defined(DNH_PROJ_EXECUTOR)
		Logger::WriteTop(
			ErrorUtility::GetParseErrorMessage(path, scanner.GetCurrentLine(), e.what()));
#endif
		res = false;
	}

	return res;
}
bool PropertyFile::HasProperty(const std::wstring& key) {
	return mapEntry_.find(key) != mapEntry_.end();
}
std::wstring PropertyFile::GetString(const std::wstring& key, const std::wstring& def) {
	if (!HasProperty(key)) return def;
	return mapEntry_[key];
}
int PropertyFile::GetInteger(const std::wstring& key, int def) {
	if (!HasProperty(key)) return def;
	const std::wstring& strValue = mapEntry_[key];
	return StringUtility::ToInteger(strValue);
}
double PropertyFile::GetReal(const std::wstring& key, double def) {
	if (!HasProperty(key)) return def;
	const std::wstring& strValue = mapEntry_[key];
	return StringUtility::ToDouble(strValue);
}
#endif

#if defined(DNH_PROJ_EXECUTOR)
//*******************************************************************
//SystemValueManager
//*******************************************************************
const std::string SystemValueManager::RECORD_SYSTEM = "__RECORD_SYSTEM__";
const std::string SystemValueManager::RECORD_SYSTEM_GLOBAL = "__RECORD_SYSTEM_GLOBAL__";
SystemValueManager* SystemValueManager::thisBase_ = nullptr;

SystemValueManager::SystemValueManager() {}
SystemValueManager::~SystemValueManager() {}

bool SystemValueManager::Initialize() {
	if (thisBase_) return false;

	mapRecord_[RECORD_SYSTEM] = make_unique<RecordBuffer>();
	mapRecord_[RECORD_SYSTEM_GLOBAL] = make_unique<RecordBuffer>();

	thisBase_ = this;
	return true;
}

void SystemValueManager::ClearRecordBuffer(const std::string& key) {
	if (!IsExists(key)) return;
	mapRecord_[key]->Clear();
}

bool SystemValueManager::IsExists(const std::string& key) const {
	return mapRecord_.find(key) != mapRecord_.end();
}
bool SystemValueManager::IsExists(const std::string& keyRecord, const std::string& keyValue) const {
	const RecordBuffer* record = GetRecordBuffer(keyRecord);
	return record != nullptr && record->IsExists(keyValue);
}

const RecordBuffer* SystemValueManager::GetRecordBuffer(const std::string& key) const {
	auto itr = mapRecord_.find(key);
	if (itr != mapRecord_.end())
		return itr->second.get();
	return nullptr;
}

#endif