#include "source/GcLib/pch.h"

#include "DirectSound.hpp"

#include <kissfft/kissfft.hh>

using namespace gstd;
using namespace directx;

//*******************************************************************
//DirectSoundManager
//*******************************************************************
DirectSoundManager* DirectSoundManager::thisBase_ = nullptr;
DirectSoundManager::DirectSoundManager() {
	ZeroMemory(&dxSoundCaps_, sizeof(DSCAPS));

	pDirectSound_ = nullptr;
	pDirectSoundPrimaryBuffer_ = nullptr;

	CreateSoundDivision(SoundDivision::DIVISION_BGM);
	CreateSoundDivision(SoundDivision::DIVISION_SE);
	CreateSoundDivision(SoundDivision::DIVISION_VOICE);
}
DirectSoundManager::~DirectSoundManager() {
	Logger::WriteTop("DirectSound: Finalizing.");
	this->Clear();

	threadManage_->Stop();
	threadManage_->Join();
	threadManage_ = nullptr;

	for (auto itr = mapDivision_.begin(); itr != mapDivision_.end(); ++itr)
		ptr_delete(itr->second);

	ptr_release(pDirectSoundPrimaryBuffer_);
	ptr_release(pDirectSound_);

	panelInfo_ = nullptr;
	thisBase_ = nullptr;
	Logger::WriteTop("DirectSound: Finalized.");
}
bool DirectSoundManager::Initialize(HWND hWnd) {
	if (thisBase_) return false;

	Logger::WriteTop("DirectSound: Initializing.");

	auto WrapDX = [&](HRESULT hr, const std::wstring& routine) {
		if (SUCCEEDED(hr)) return;
		std::wstring err = StringUtility::Format(L"DirectSound: %s failure. [%s]\r\n  %s",
			routine.c_str(), DXGetErrorString9(hr), DXGetErrorDescription9(hr));
		Logger::WriteTop(err);
		throw wexception(err);
	};

	WrapDX(DirectSoundCreate8(nullptr, &pDirectSound_, nullptr), L"DirectSoundCreate8");

	WrapDX(pDirectSound_->SetCooperativeLevel(hWnd, DSSCL_PRIORITY), L"SetCooperativeLevel");

	//Get device caps
	dxSoundCaps_.dwSize = sizeof(DSCAPS);
	WrapDX(pDirectSound_->GetCaps(&dxSoundCaps_), L"GetCaps");

	//Create the primary buffer
	{
		DSBUFFERDESC desc;
		ZeroMemory(&desc, sizeof(DSBUFFERDESC));
		desc.dwSize = sizeof(DSBUFFERDESC);
		desc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_PRIMARYBUFFER;
		desc.dwBufferBytes = 0;
		desc.lpwfxFormat = nullptr;
		WrapDX(pDirectSound_->CreateSoundBuffer(&desc, (LPDIRECTSOUNDBUFFER*)&pDirectSoundPrimaryBuffer_, nullptr),
			L"CreateSoundBuffer(primary)");

		WAVEFORMATEX pcmwf;
		ZeroMemory(&pcmwf, sizeof(WAVEFORMATEX));
		pcmwf.wFormatTag = WAVE_FORMAT_PCM;
		pcmwf.nChannels = 2;
		pcmwf.nSamplesPerSec = 44100;
		pcmwf.nBlockAlign = 4;
		pcmwf.nAvgBytesPerSec = pcmwf.nSamplesPerSec * pcmwf.nBlockAlign;
		pcmwf.wBitsPerSample = 16;
		WrapDX(pDirectSoundPrimaryBuffer_->SetFormat(&pcmwf), L"SetFormat");
	}

	//Sound manager thread, this thread runs even when the window is unfocused,
	//	and manages stuff like fade, deletion, and the LogWindow's Sound panel.
	threadManage_.reset(new SoundManageThread(this));
	threadManage_->Start();

	Logger::WriteTop("DirectSound: Initialized.");

	thisBase_ = this;
	return true;
}
void DirectSoundManager::Clear() {
	try {
		Lock lock(lock_);

		for (auto itrPlayer = listManagedPlayer_.begin(); itrPlayer != listManagedPlayer_.end(); ++itrPlayer) {
			shared_ptr<SoundPlayer> player = *itrPlayer;
			if (player == nullptr) continue;
			player->Stop();
		}

		mapSoundSource_.clear();
	}
	catch (...) {}
}
shared_ptr<SoundSourceData> DirectSoundManager::GetSoundSource(const std::wstring& path, bool bCreate) {
	shared_ptr<SoundSourceData> res;
	try {
		Lock lock(lock_);

		res = _GetSoundSource(path);
		if (res == nullptr && bCreate) {
			res = _CreateSoundSource(path);
		}
	}
	catch (...) {}

	return res;
}
shared_ptr<SoundSourceData> DirectSoundManager::_GetSoundSource(const std::wstring& path) {
	auto itr = mapSoundSource_.find(path);
	if (itr == mapSoundSource_.end()) return nullptr;
	return itr->second;
}
shared_ptr<SoundSourceData> DirectSoundManager::_CreateSoundSource(std::wstring path) {
	FileManager* fileManager = FileManager::GetBase();

	shared_ptr<SoundSourceData> res;

	path = PathProperty::GetUnique(path);
	std::wstring pathReduce = PathProperty::ReduceModuleDirectory(path);
	try {
		shared_ptr<FileReader> reader = fileManager->GetFileReader(path);
		if (reader == nullptr || !reader->Open())
			throw gstd::wexception(ErrorUtility::GetFileNotFoundErrorMessage(path, true));

		size_t sizeFile = reader->GetFileSize();
		if (sizeFile <= 64)
			throw gstd::wexception(L"Audio file invalid.");

		ByteBuffer header(0x100);
		reader->Read(header.GetPointer(), header.GetSize());

		SoundFileFormat format = SoundFileFormat::Unknown;
		if (!memcmp(header.GetPointer(), "RIFF", 4)) {		//WAVE
			format = SoundFileFormat::Wave;
			/*
			size_t ptr = 0xC;
			while (ptr <= 0x100) {
				size_t chkSize = *(uint32_t*)header.GetPointer(ptr + sizeof(uint32_t));
				if (memcmp(header.GetPointer(ptr), "fmt ", 4) == 0 && chkSize >= 0x10) {
					WAVEFORMATEX* wavefmt = (WAVEFORMATEX*)header.GetPointer(ptr + sizeof(uint32_t) * 2);
					if (wavefmt->wFormatTag == WAVE_FORMAT_MPEGLAYER3)
						format = SoundFileFormat::AWave;	//Surprise! You thought it was .wav, BUT IT WAS ME, .MP3!!
					break;
				}
				ptr += chkSize + sizeof(uint32_t);
			}
			*/
		}
		else if (!memcmp(header.GetPointer(), "OggS", 4)) {	//Ogg Vorbis
			format = SoundFileFormat::Ogg;
		}

		bool bSuccess = false;

		//Create the sound player object
		switch (format) {
		case SoundFileFormat::Wave:
			res.reset(new SoundSourceDataWave());
			bSuccess = res->Load(reader);
			break;
		case SoundFileFormat::Ogg:
			res.reset(new SoundSourceDataOgg());
			bSuccess = res->Load(reader);
			break;
		}

		if (res) {
			res->path_ = path;
			res->pathHash_ = std::hash<std::wstring>{}(path);
			res->format_ = format;
		}

		if (bSuccess) {
			Lock lock(lock_);

			mapSoundSource_[path] = res;
			std::wstring str = StringUtility::Format(L"DirectSound: Audio loaded [%s]", pathReduce.c_str());
			Logger::WriteTop(str);
		}
		else {
			res = nullptr;
			std::wstring str = StringUtility::Format(L"DirectSound: Audio load failed [%s]", pathReduce.c_str());
			Logger::WriteTop(str);
		}
	}
	catch (gstd::wexception& e) {
		res = nullptr;
		std::wstring str = StringUtility::Format(L"DirectSound: Audio load failed [%s]\r\n\t%s", 
			pathReduce.c_str(), e.what());
		Logger::WriteTop(str);
	}
	return res;
}
shared_ptr<SoundPlayer> DirectSoundManager::CreatePlayer(shared_ptr<SoundSourceData> source) {
	if (source == nullptr) return nullptr;

	const std::wstring& path = source->path_;
	std::wstring pathReduce = PathProperty::ReduceModuleDirectory(path);

	shared_ptr<SoundPlayer> res;

	try {
		//Create the sound player object
		switch (source->format_) {
		case SoundFileFormat::Wave:
			if (source->audioSizeTotal_ < 1024U * 1024U) {
				//The audio is small enough (<1MB), just load the entire thing into memory
				//Max: ~23.78sec at 44100hz
				res = std::shared_ptr<SoundPlayerWave>(new SoundPlayerWave());
			}
			else {
				//File too bigg uwu owo, pweasm be gentwe and take it in swowwy owo *blushes*
				res = std::shared_ptr<SoundStreamingPlayerWave>(new SoundStreamingPlayerWave());
			}
			break;
		case SoundFileFormat::Ogg:
			res = std::shared_ptr<SoundStreamingPlayerOgg>(new SoundStreamingPlayerOgg());
			break;
		}

		bool bSuccess = false;
		if (res) {
			//Create a DirectSound buffer
			bSuccess = res->_CreateBuffer(source);
			if (bSuccess)
				res->Seek(0.0);
		}

		if (bSuccess) {
			Lock lock(lock_);

			res->manager_ = this;

			res->path_ = path;
			res->pathHash_ = std::hash<std::wstring>{}(path);

			listManagedPlayer_.push_back(res);
			/*
			std::wstring str = StringUtility::Format(L"DirectSound: Sound player created [%s]", pathReduce.c_str());
			Logger::WriteTop(str);
			*/
		}
		else {
			res = nullptr;
			std::wstring str = StringUtility::Format(L"DirectSound: Sound player create failed [%s]", 
				pathReduce.c_str());
			Logger::WriteTop(str);
		}
	}
	catch (gstd::wexception& e) {
		res = nullptr;
		std::wstring str = StringUtility::Format(L"DirectSound: Sound player create failed [%s]\r\n\t%s", 
			pathReduce.c_str(), e.what());
		Logger::WriteTop(str);
	}
	return res;
}
shared_ptr<SoundPlayer> DirectSoundManager::GetPlayer(const std::wstring& path) {
	shared_ptr<SoundPlayer> res;
	{
		Lock lock(lock_);

		size_t hash = std::hash<std::wstring>{}(path);

		for (auto itrPlayer = listManagedPlayer_.begin(); itrPlayer != listManagedPlayer_.end(); ++itrPlayer) {
			shared_ptr<SoundPlayer> player = *itrPlayer;
			if (hash == player->GetPathHash()) {
				res = player;
				break;
			}
		}
	}
	return res;
}
SoundDivision* DirectSoundManager::CreateSoundDivision(int index) {
	auto itrDiv = mapDivision_.find(index);
	if (itrDiv != mapDivision_.end())
		return itrDiv->second;

	SoundDivision* division = new SoundDivision();
	mapDivision_[index] = division;
	return division;
}
SoundDivision* DirectSoundManager::GetSoundDivision(int index) {
	auto itrDiv = mapDivision_.find(index);
	if (itrDiv == mapDivision_.end()) return nullptr;
	return itrDiv->second;
}
void DirectSoundManager::SetFadeDeleteAll() {
	try {
		Lock lock(lock_);

		for (auto itrPlayer = listManagedPlayer_.begin(); itrPlayer != listManagedPlayer_.end(); ++itrPlayer) {
			SoundPlayer* player = itrPlayer->get();
			if (player == nullptr) continue;
			player->SetFadeDelete(SoundPlayer::FADE_DEFAULT);
		}
	}
	catch (...) {}
}

//DirectSoundManager::SoundManageThread
DirectSoundManager::SoundManageThread::SoundManageThread(DirectSoundManager* manager) {
	_SetOuter(manager);
	timeCurrent_ = 0;
	timePrevious_ = 0;
}
void DirectSoundManager::SoundManageThread::_Run() {
	DirectSoundManager* manager = _GetOuter();
	while (this->GetStatus() == RUN) {
		timeCurrent_ = SystemUtility::GetCpuTime2();

		{
			Lock lock(manager->GetLock());
			_Fade();
			_Arrange();
		}

		if (manager->panelInfo_ != nullptr && this->GetStatus() == RUN)
			manager->panelInfo_->Update(manager);
		
		timePrevious_ = timeCurrent_;
		::Sleep(100);
	}
}
void DirectSoundManager::SoundManageThread::_Arrange() {
	DirectSoundManager* manager = _GetOuter();

	{
		auto* listPlayer = &manager->listManagedPlayer_;
		for (auto itrPlayer = listPlayer->begin(); itrPlayer != listPlayer->end();) {
			SoundPlayer* player = itrPlayer->get();
			if (player == nullptr) {
				itrPlayer = listPlayer->erase(itrPlayer);
				continue;
			}

			bool bPlaying = player->IsPlaying();
			bool bDelete = player->bDelete_ || (player->bAutoDelete_ && !bPlaying);
			if (!bDelete && (itrPlayer->use_count() == 1)) {
				bDelete = !bPlaying;
			}

			if (bDelete) {
				//Logger::WriteTop(StringUtility::Format(L"DirectSound: Released player [%s]", player->GetPath().c_str()));
				player->Stop();
				itrPlayer = listPlayer->erase(itrPlayer);
			}
			else ++itrPlayer;
		}
	}
	{
		auto* mapSource = &manager->mapSoundSource_;
		for (auto itrSource = mapSource->begin(); itrSource != mapSource->end();) {
			shared_ptr<SoundSourceData> source = itrSource->second;
			if (source == nullptr) {
				itrSource = mapSource->erase(itrSource);
				continue;
			}

			//1 in here, and 1 in the sound manager = not preloaded or not owned by any sound player
			bool bDelete = source.use_count() == 2;

			if (bDelete) {
				Logger::WriteTop(StringUtility::Format(L"DirectSound: Released data [%s]", 
					PathProperty::ReduceModuleDirectory(source->path_).c_str()));
				itrSource = mapSource->erase(itrSource);
			}
			else ++itrSource;
		}
	}
}
void DirectSoundManager::SoundManageThread::_Fade() {
	DirectSoundManager* manager = _GetOuter();
	uint64_t timeGap = timeCurrent_ - timePrevious_;

	auto* listPlayer = &manager->listManagedPlayer_;
	for (auto itrPlayer = listPlayer->begin(); itrPlayer != listPlayer->end(); ++itrPlayer) {
		SoundPlayer* player = itrPlayer->get();
		if (player == nullptr) continue;

		double rateFade = player->GetFadeVolumeRate();
		if (rateFade == 0) continue;

		double rateVolume = player->GetVolumeRate();
		rateFade *= timeGap / 1000.0;
		rateVolume += rateFade;
		player->SetVolumeRate(rateVolume);

		if (rateVolume <= 0 && player->bFadeDelete_) {
			player->Stop();
			player->Delete();
		}
	}
}


//*******************************************************************
//SoundInfoPanel
//*******************************************************************
SoundInfoPanel::SoundInfoPanel() {
}

void SoundInfoPanel::Initialize(const std::string& name) {
	ILoggerPanel::Initialize(name);
}

void SoundInfoPanel::Update(DirectSoundManager* manager) {
	if (manager == nullptr) {
		listDisplay_.clear();
		return;
	}

	{
		Lock lock(Logger::GetTop()->GetLock());

		auto& mapData = manager->mapSoundSource_;
		listDisplay_.resize(mapData.size());

		int iTex = 0;
		for (auto itrMap = mapData.begin(); itrMap != mapData.end(); ++itrMap, ++iTex) {
			const std::wstring& path = itrMap->first;
			SoundSourceData* data = (itrMap->second).get();

			int countRef = (itrMap->second).use_count();

			std::wstring fileName = PathProperty::GetFileName(path);
			std::wstring pathReduce = PathProperty::ReduceModuleDirectory(path);

			SoundDisplay displayData = {
				(uintptr_t)data,
				StringUtility::FromAddress((uintptr_t)data),
				STR_MULTI(fileName),
				STR_MULTI(pathReduce),
				countRef,
				data->format_,
				data->audioSizeTotal_,
				data->formatWave_
			};

			listDisplay_[iTex] = displayData;
		}

		// Sort new data as well
		if (SoundDisplay::imguiSortSpecs) {
			if (listDisplay_.size() > 1) {
				std::sort(listDisplay_.begin(), listDisplay_.end(), SoundDisplay::Compare);
			}
		}
	}

	{
		DSCAPS _sndCaps;
		manager->GetDirectSound()->GetCaps(&_sndCaps);

		UINT memRemain = _sndCaps.dwFreeHwMemBytes / (1024U * 1024U);
		UINT memTotal = _sndCaps.dwTotalHwMemBytes / (1024U * 1024U);

		soundMemAvail_ = memRemain;
		soundMem_ = memTotal;
	}
}

const char* SoundFormatToString(SoundFileFormat format) {
	switch (format) {
	case SoundFileFormat::Wave:	return "wave";
	case SoundFileFormat::Ogg:	return "ogg";
	}
	return "unknown";
}

void SoundInfoPanel::ProcessGui() {
	Logger* parent = Logger::GetTop();

	float ht = ImGui::GetContentRegionAvail().y - 32;

	if (ImGui::BeginChild("psound_child_table", ImVec2(0, ht), false, ImGuiWindowFlags_HorizontalScrollbar)) {
		ImGuiTableFlags flags = ImGuiTableFlags_Reorderable | ImGuiTableFlags_Resizable
			| ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoHostExtendX
			| ImGuiTableFlags_RowBg
			| ImGuiTableFlags_Sortable /*| ImGuiTableFlags_SortMulti*/;

		if (ImGui::BeginTable("psound_table", 6, flags)) {
			ImGui::TableSetupScrollFreeze(0, 1);

			constexpr auto sortDef = ImGuiTableColumnFlags_DefaultSort,
				sortNone = ImGuiTableColumnFlags_NoSort;
			constexpr auto colFlags = ImGuiTableColumnFlags_WidthStretch;
			ImGui::TableSetupColumn("Address", sortDef, 0, SoundDisplay::Address);
			ImGui::TableSetupColumn("Name", colFlags | sortDef, 120, SoundDisplay::Name);
			ImGui::TableSetupColumn("Path", colFlags | sortDef, 200, SoundDisplay::FullPath);
			ImGui::TableSetupColumn("Uses", sortDef, 0, SoundDisplay::Uses);
			ImGui::TableSetupColumn("Size", colFlags | sortDef, 80, SoundDisplay::Size);
			ImGui::TableSetupColumn("Format", sortDef, 0, SoundDisplay::Format);

			ImGui::TableHeadersRow();

			if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
				if (specs->SpecsDirty) {
					SoundDisplay::imguiSortSpecs = specs;
					if (listDisplay_.size() > 1) {
						std::sort(listDisplay_.begin(), listDisplay_.end(), SoundDisplay::Compare);
					}
					//SoundDisplay::imguiSortSpecs = nullptr;

					specs->SpecsDirty = false;
				}
			}

			{
				ImGui::PushFont(parent->GetFont("Arial15"));

#define _SETCOL(_i, _s) ImGui::TableSetColumnIndex(_i); ImGui::Text((_s).c_str());

				ImGuiListClipper clipper;
				clipper.Begin(listDisplay_.size());
				while (clipper.Step()) {
					for (size_t i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
						const SoundDisplay& item = listDisplay_[i];

						ImGui::TableNextRow();

						_SETCOL(0, item.strAddress);

						_SETCOL(1, item.fileName);
						if (ImGui::IsItemHovered())
							ImGui::SetTooltip(item.fileName.c_str());

						_SETCOL(2, item.fullPath);
						if (ImGui::IsItemHovered())
							ImGui::SetTooltip(item.fullPath.c_str());

						_SETCOL(3, std::to_string(item.countRef));

						_SETCOL(4, STR_FMT("%u KB", item.audioSize / 1024U));
						if (ImGui::IsItemHovered())
							ImGui::SetTooltip(std::to_string(item.audioSize).c_str());

						ImGui::TableSetColumnIndex(5);
						ImGui::Text(SoundFormatToString(item.format));
						if (ImGui::IsItemHovered()) {
							ImGui::BeginTooltipEx(ImGuiTooltipFlags_OverridePreviousTooltip, ImGuiWindowFlags_None);

							ImGui::Text("Format Tag: %d", item.waveFmt.wFormatTag);
							ImGui::Text("%u Hz, %u channels", item.waveFmt.nSamplesPerSec, item.waveFmt.nChannels);

							ImGui::EndTooltip();
						}

						
					}
				}

				ImGui::PopFont();

#undef _SETCOL
			}

			ImGui::EndTable();
		}
	}
	ImGui::EndChild();

	ImGui::Dummy(ImVec2(0, 4));
	ImGui::Indent(6);

	{
		ImGui::Text("Sound Memory: %u MB", soundMem_);

		ImGui::SameLine(200);

		ImGui::Text("Available: %u MB", soundMemAvail_);
	}
}

const ImGuiTableSortSpecs* SoundInfoPanel::SoundDisplay::imguiSortSpecs = nullptr;
bool SoundInfoPanel::SoundDisplay::Compare(const SoundDisplay& a, const SoundDisplay& b) {
	for (int i = 0; i < imguiSortSpecs->SpecsCount; ++i) {
		const ImGuiTableColumnSortSpecs* spec = &imguiSortSpecs->Specs[i];

		int rcmp = 0;

#define CASE_SORT(_id, _l, _r) \
		case _id: { \
			if (_l != _r) rcmp = (_l < _r) ? 1 : -1; \
			break; \
		}

		switch ((Column)spec->ColumnUserID) {
		CASE_SORT(Column::Address, a.address, b.address);
		case Column::Name:
			rcmp = a.fileName.compare(b.fileName);
			break;
		case Column::FullPath:
			rcmp = a.fullPath.compare(b.fullPath);
			break;
		CASE_SORT(Column::Uses, a.countRef, b.countRef);
		CASE_SORT(Column::Format, (int)a.format, (int)b.format);
		CASE_SORT(Column::Size, a.audioSize, b.audioSize);
		}

#undef CASE_SORT

		if (rcmp != 0) {
			return spec->SortDirection == ImGuiSortDirection_Ascending
				? rcmp < 0 : rcmp > 0;
		}
	}

	return a.address < b.address;
}

//*******************************************************************
//SoundDivision
//*******************************************************************
SoundDivision::SoundDivision() {
	rateVolume_ = 100;
}
SoundDivision::~SoundDivision() {
}

//*******************************************************************
//SoundSourceData
//*******************************************************************
SoundSourceData::SoundSourceData() {
	path_ = L"";
	pathHash_ = 0;

	Release();
}
void SoundSourceData::Release() {
	format_ = SoundFileFormat::Unknown;
	ZeroMemory(&formatWave_, sizeof(WAVEFORMATEX));

	reader_ = nullptr;
	audioSizeTotal_ = 0;
}

SoundSourceDataWave::SoundSourceDataWave() {
	posWaveStart_ = 0;
	posWaveEnd_ = 0;
}
void SoundSourceDataWave::Release() {
	SoundSourceData::Release();

	posWaveStart_ = 0;
	posWaveEnd_ = 0;
	bufWaveData_.Clear();
}
bool SoundSourceDataWave::Load(shared_ptr<gstd::FileReader> reader) {
	Release();

	reader_ = reader;
	reader->SetFilePointerBegin();

	try {
		byte chunk[4];
		uint32_t sizeChunk = 0;
		uint32_t sizeRiff = 0;

		//First, check if we're actually reading a .wav
		reader->Read(&chunk, 4);
		if (memcmp(chunk, "RIFF", 4) != 0) throw false;
		reader->Read(&sizeRiff, sizeof(uint32_t));
		reader->Read(&chunk, 4);
		if (memcmp(chunk, "WAVE", 4) != 0) throw false;

		bool bReadValidFmtChunk = false;
		uint32_t fmtChunkOffset = 0;
		bool bFoundValidDataChunk = false;
		uint32_t dataChunkOffset = 0;

		//Scan chunks
		while (reader->Read(&chunk, 4)) {
			reader->Read(&sizeChunk, sizeof(uint32_t));

			if (!bReadValidFmtChunk && memcmp(chunk, "fmt ", 4) == 0 && sizeChunk >= 0x10) {
				bReadValidFmtChunk = true;
				fmtChunkOffset = reader->GetFilePointer() - sizeof(uint32_t);
			}
			else if (!bFoundValidDataChunk && memcmp(chunk, "data", 4) == 0) {
				bFoundValidDataChunk = true;
				dataChunkOffset = reader->GetFilePointer() - sizeof(uint32_t);
			}

			reader->Seek(reader->GetFilePointer() + sizeChunk);
			if (bReadValidFmtChunk && bFoundValidDataChunk) break;
		}

		if (!bReadValidFmtChunk) throw gstd::wexception("wave format not found");
		if (!bFoundValidDataChunk) throw gstd::wexception("wave data not found");

		reader->Seek(fmtChunkOffset);
		reader->Read(&sizeChunk, sizeof(uint32_t));
		reader->Read(&formatWave_, sizeChunk);

		switch (formatWave_.wFormatTag) {
		case WAVE_FORMAT_UNKNOWN:
		case WAVE_FORMAT_EXTENSIBLE:
		case WAVE_FORMAT_DEVELOPMENT:
			//Unsupported format type
			throw gstd::wexception("unsupported wave format");
		}

		reader->Seek(dataChunkOffset);
		reader->Read(&sizeChunk, sizeof(uint32_t));

		//sizeChunk is now the size of the wave data
		audioSizeTotal_ = sizeChunk;

		posWaveStart_ = dataChunkOffset + sizeof(uint32_t);
		posWaveEnd_ = posWaveStart_ + sizeChunk;

		if (audioSizeTotal_ > 0 && audioSizeTotal_ <= 1024 * 1024) {
			bufWaveData_.SetSize(audioSizeTotal_);

			reader->Seek(posWaveStart_);
			reader->Read(bufWaveData_.GetPointer(), audioSizeTotal_);
		}
	}
	catch (bool) {
		return false;
	}
	catch (gstd::wexception& e) {
		throw e;
	}

	return true;
}

ov_callbacks SoundSourceDataOgg::oggCallBacks_ = {
	SoundSourceDataOgg::_ReadOgg,
	SoundSourceDataOgg::_SeekOgg,
	SoundSourceDataOgg::_CloseOgg,
	SoundSourceDataOgg::_TellOgg,
};
SoundSourceDataOgg::SoundSourceDataOgg() {
	fileOgg_ = nullptr;
}
SoundSourceDataOgg::~SoundSourceDataOgg() {
	Release();
}
void SoundSourceDataOgg::Release() {
	SoundSourceData::Release();

	if (fileOgg_) {
		ov_clear(fileOgg_);
		ptr_delete(fileOgg_);
	}
}
bool SoundSourceDataOgg::Load(shared_ptr<gstd::FileReader> reader) {
	Release();

	reader_ = reader;
	reader->SetFilePointerBegin();

	try {
		fileOgg_ = new OggVorbis_File;
		if (ov_open_callbacks((void*)this, fileOgg_, nullptr, 0, oggCallBacks_) < 0)
			throw false;

		vorbis_info* vi = ov_info(fileOgg_, -1);
		if (vi == nullptr) {
			ov_clear(fileOgg_);
			throw false;
		}

		formatWave_.cbSize = sizeof(WAVEFORMATEX);
		formatWave_.wFormatTag = WAVE_FORMAT_PCM;
		formatWave_.nChannels = vi->channels;
		formatWave_.nSamplesPerSec = vi->rate;
		formatWave_.nAvgBytesPerSec = vi->rate * vi->channels * 2;
		formatWave_.nBlockAlign = vi->channels * 2;		//Bytes per sample
		formatWave_.wBitsPerSample = 2 * 8;
		formatWave_.cbSize = 0;

		uint64_t pcmTotal = ov_pcm_total(fileOgg_, -1);
		audioSizeTotal_ = pcmTotal * formatWave_.nBlockAlign;
	}
	catch (bool) {
		return false;
	}
	catch (gstd::wexception& e) {
		throw e;
	}

	return true;
}
size_t SoundSourceDataOgg::_ReadOgg(void* ptr, size_t size, size_t nmemb, void* source) {
	SoundSourceDataOgg* parent = (SoundSourceDataOgg*)source;

	size_t sizeCopy = size * nmemb;
	sizeCopy = parent->reader_->Read(ptr, sizeCopy);

	return sizeCopy / size;
}
int SoundSourceDataOgg::_SeekOgg(void* source, ogg_int64_t offset, int whence) {
	SoundSourceDataOgg* parent = (SoundSourceDataOgg*)source;
	LONG high = (LONG)((offset & 0xFFFFFFFF00000000) >> 32);
	LONG low = (LONG)((offset & 0x00000000FFFFFFFF) >> 0);

	switch (whence) {
	case SEEK_CUR:
	{
		size_t current = parent->reader_->GetFilePointer() + low;
		parent->reader_->Seek(current);
		break;
	}
	case SEEK_END:
	{
		parent->reader_->SetFilePointerEnd();
		break;
	}
	case SEEK_SET:
	{
		parent->reader_->Seek(low);
		break;
	}
	}
	return 0;
}
int SoundSourceDataOgg::_CloseOgg(void* source) {
	return 0;
}
long SoundSourceDataOgg::_TellOgg(void* source) {
	SoundSourceDataOgg* parent = (SoundSourceDataOgg*)source;
	return parent->reader_->GetFilePointer();
}

//*******************************************************************
//SoundPlayer
//*******************************************************************
SoundPlayer::SoundPlayer() {
	pDirectSoundBuffer_ = nullptr;

	path_ = L"";
	pathHash_ = 0;

	playStyle_.bLoop_ = false;
	playStyle_.timeLoopStart_ = 0;
	playStyle_.timeLoopEnd_ = 0;
	playStyle_.timeStart_ = -1;
	playStyle_.bResume_ = false;

	bDelete_ = false;
	bFadeDelete_ = false;
	bAutoDelete_ = false;
	rateVolume_ = 100.0;
	rateVolumeFadePerSec_ = 0;

	bPause_ = false;

	division_ = nullptr;
	manager_ = nullptr;
}
SoundPlayer::~SoundPlayer() {
	Stop();
	ptr_release(pDirectSoundBuffer_);
}
void SoundPlayer::SetSoundDivision(SoundDivision* div) {
	division_ = div;
}
void SoundPlayer::SetSoundDivision(int index) {
	{
		Lock lock(lock_);
		DirectSoundManager* manager = DirectSoundManager::GetBase();
		SoundDivision* div = manager->GetSoundDivision(index);
		if (div) SetSoundDivision(div);
	}
}
bool SoundPlayer::Play() {
	return false;
}
bool SoundPlayer::Stop() {
	return false;
}
bool SoundPlayer::IsPlaying() {
	return false;
}
double SoundPlayer::GetVolumeRate() {
	return rateVolume_;
}
bool SoundPlayer::SetVolumeRate(double rateVolume) {
	{
		Lock lock(lock_);

		if (rateVolume < 0) rateVolume = 0.0;
		else if (rateVolume > 100) rateVolume = 100.0;
		rateVolume_ = rateVolume;

		if (pDirectSoundBuffer_) {
			double rateDiv = 100.0;
			if (division_)
				rateDiv = division_->GetVolumeRate();
			double rate = rateVolume_ / 100.0 * rateDiv / 100.0;

			//int volume = (int)((double)(DirectSoundManager::SD_VOLUME_MAX - DirectSoundManager::SD_VOLUME_MIN) * rate);
			//pDirectSoundBuffer_->SetVolume(DirectSoundManager::SD_VOLUME_MIN+volume);
			int volume = _GetVolumeAsDirectSoundDecibel(rate);
			pDirectSoundBuffer_->SetVolume(volume);
		}
	}

	return true;
}
bool SoundPlayer::SetPanRate(double ratePan) {
	{
		Lock lock(lock_);

		if (ratePan < -100) ratePan = -100.0;
		else if (ratePan > 100) ratePan = 100.0;

		if (pDirectSoundBuffer_) {
			double rateDiv = 100.0;
			if (division_)
				rateDiv = division_->GetVolumeRate();
			double rate = rateVolume_ / 100.0 * rateDiv / 100.0;

			LONG volume = (LONG)((DirectSoundManager::SD_VOLUME_MAX - DirectSoundManager::SD_VOLUME_MIN) * rate);
			//int volume = _GetValumeAsDirectSoundDecibel(rate);

			double span = (DSBPAN_RIGHT - DSBPAN_LEFT) / 2;
			span = volume / 2;
			double pan = span * ratePan / 100;
			HRESULT hr = pDirectSoundBuffer_->SetPan((LONG)pan);
		}
	}

	return true;
}
double SoundPlayer::GetFadeVolumeRate() {
	double res = 0;
	{
		Lock lock(lock_);
		res = rateVolumeFadePerSec_;
	}
	return res;
}
void SoundPlayer::SetFade(double rateVolumeFadePerSec) {
	{
		Lock lock(lock_);
		rateVolumeFadePerSec_ = rateVolumeFadePerSec;
	}
}
void SoundPlayer::SetFadeDelete(double rateVolumeFadePerSec) {
	{
		Lock lock(lock_);
		bFadeDelete_ = true;
		SetFade(rateVolumeFadePerSec);
	}
}
LONG SoundPlayer::_GetVolumeAsDirectSoundDecibel(float rate) {
	LONG result = 0;
	if (rate >= 1.0f) {
		result = DSBVOLUME_MAX;
	}
	else if (rate <= 0.0f) {
		result = DSBVOLUME_MIN;
	}
	else {
		// 10dBで音量2倍。
		// →(求めるdB)
		//	 = 10 * log2(音量)
		//	 = 10 * ( log10(音量) / log10(2) )
		//	 = 33.2... * log10(音量)
		result = (LONG)(33.2f * log10(rate) * 100);
	}
	return result;
}
DWORD SoundPlayer::GetCurrentPosition() {
	DWORD res = 0;
	if (pDirectSoundBuffer_) {
		HRESULT hr = pDirectSoundBuffer_->GetCurrentPosition(&res, nullptr);
		if (FAILED(hr)) res = 0;
	}
	return res;
}
void SoundPlayer::SetFrequency(DWORD freq) {
	if (manager_ == nullptr) return;
	if (pDirectSoundBuffer_) {
		if (freq > 0) {
			const DSCAPS* caps = manager_->GetDeviceCaps();
			DWORD rateMin = caps->dwMinSecondarySampleRate;
			DWORD rateMax = caps->dwMaxSecondarySampleRate;
			//DWORD rateMin = DSBFREQUENCY_MIN;
			//DWORD rateMax = DSBFREQUENCY_MAX;
			freq = std::clamp(freq, rateMin, rateMax);
		}
		HRESULT hr = pDirectSoundBuffer_->SetFrequency(freq);
		//std::wstring err = StringUtility::Format(L"%s: %s",
		//	DXGetErrorString9(hr), DXGetErrorDescription9(hr));
	}
}

void SoundPlayer::_LoadSamples(byte* pWaveData, size_t dataSize, size_t nSamples, double* pRes) {
	DWORD sampleRate = soundSource_->formatWave_.nSamplesPerSec;
	DWORD bytePerSample = soundSource_->formatWave_.wBitsPerSample / 8U;
	DWORD nBlockAlign = soundSource_->formatWave_.nBlockAlign;
	DWORD nChannels = soundSource_->formatWave_.nChannels;
	DWORD bytePerPCM = bytePerSample / nChannels;

	// I totally didn't copy these from LuaSTG!! what are you even talking about?!

	const DWORD STHRESHOLD = 1U << (nBlockAlign / nChannels * 8 - 1);

	size_t maxSamples = dataSize / nBlockAlign;

	if (nChannels == 1) {
		for (size_t i = 0; i < nSamples && i < maxSamples; ++i) {
			byte* pData = (byte*)pWaveData + (i * nBlockAlign);

			int64_t val = 0;
			for (size_t j = 0; j < nBlockAlign; ++j) {
				val += pData[j] << (j * 8);
			}
			if (val > STHRESHOLD - 1) {
				val -= STHRESHOLD * 2;
			}

			pRes[i] = val / (double)STHRESHOLD;
		}
	}
	else if (nChannels == 2) {
		for (size_t i = 0; i < nSamples && i < maxSamples; ++i) {
			byte* pData = (byte*)pWaveData + (i * nBlockAlign);

			int64_t val = 0;
			int c = 0;
			for (size_t j = 0; j < nChannels; ++j) {
				val ^= pData[j] << (c * 8);
				++c;
			}
			if (val > STHRESHOLD - 1) {
				val -= STHRESHOLD * 2;
			}

			pRes[i] = val / (double)STHRESHOLD;
		}
	}
	else {
		for (size_t i = 0; i < nSamples; ++i) {
			pRes[i] = 0;
		}
	}
}
void SoundPlayer::_DoFFT(const std::vector<double>& bufIn, std::vector<double>& bufOut, double multiplier, bool bAutoLog) {
	size_t cSamples = bufIn.size();
	size_t nResolution = bufOut.size();

	size_t cSamplesP2 = 0;
	{
		size_t nextPow2 = pow(2, ceil(log2(cSamples)));
		size_t prevPow2 = nextPow2 >> 1;

		//Round to the nearest power of two
		cSamplesP2 = ((nextPow2 - cSamples) < (cSamples - prevPow2)) ? nextPow2 : prevPow2;
	}
	size_t fillSize = std::min(cSamples, cSamplesP2);

	kissfft<double> fft(fillSize, true);

	std::vector<std::complex<double>> ffin(cSamplesP2, std::complex<double>(0, 0));
	std::vector<std::complex<double>> ffout(cSamplesP2);
	
	for (size_t i = 0; i < fillSize; ++i) {
		//Apply the Hann window function
		double window = 0.54 * (1 - cos(2 * GM_PI * i / (fillSize - 1)));
		//window *= multiplier;
		ffin[i] = std::complex<double>(bufIn[i] * window, 0);
	}

	fft.transform(ffin.data(), ffout.data());

	double avgPower = ffout[0].real();

	size_t halfSamp = std::min(cSamplesP2, cSamples) / 2;
	for (size_t i = 1; i < halfSamp; ++i) {
		double norm = std::norm(ffout[i]);
		ffout[i - 1] = { log(norm + 1), 0 };
	}

	size_t len = halfSamp - 1;
	for (size_t i = 0; i < nResolution; ++i) {
		double pos = i / (double)nResolution;
		if (bAutoLog) {
			//inverse function of [log10(1 + 99 * pos) / 2]
			constexpr double LOG_F = 1.69460519893;
			pos = 2 * (pow(10, LOG_F * pos) - 1) / 99;
		}
		pos *= len;

		size_t from = floor(pos);
		size_t to = std::min<size_t>(from + 1, len);

		double val = Math::Lerp::Smooth(ffout[from].real(), ffout[to].real(), pos - from);
		bufOut[i] = val;
	}
}
bool SoundPlayer::GetSamplesFFT(DWORD durationMs, size_t resolution, bool bAutoLog, std::vector<double>& res) {
	res.resize(resolution, 0);

	if (durationMs > 0 && pDirectSoundBuffer_ && IsPlaying()) {
		DWORD sampleRate = soundSource_->formatWave_.nSamplesPerSec;
		DWORD bytePerSample = soundSource_->formatWave_.wBitsPerSample / 8U;
		DWORD nChannels = soundSource_->formatWave_.nChannels;

		DWORD samplesNeeded = durationMs * sampleRate / 1000U;
		samplesNeeded = std::clamp<DWORD>(samplesNeeded, 32, sampleRate / 4);

		DWORD cAudioPos = GetCurrentPosition();
		DWORD cAudioPosMax = soundSource_->audioSizeTotal_;
		DWORD sizeLock = std::min(cAudioPosMax - cAudioPos, samplesNeeded * bytePerSample);

		std::vector<double> samples;

		void* pMem;
		DWORD dwSize;
		HRESULT hr = pDirectSoundBuffer_->Lock(cAudioPos, sizeLock, &pMem, &dwSize, nullptr, nullptr, 0);
		if (SUCCEEDED(hr)) {
			samples.resize(samplesNeeded);

			_LoadSamples((byte*)pMem, dwSize, samplesNeeded, samples.data());

			pDirectSoundBuffer_->Unlock(pMem, dwSize, nullptr, 0);
		}
		else return false;
		
		_DoFFT(samples, res, GetVolumeRate() / 100, bAutoLog);

		return true;
	}

	return false;
}

//*******************************************************************
//SoundStreamingPlayer
//*******************************************************************
SoundStreamingPlayer::SoundStreamingPlayer() {
	pDirectSoundNotify_ = nullptr;
	ZeroMemory(hEvent_, sizeof(HANDLE) * 3);
	thread_.reset(new StreamingThread(this));

	bStreaming_ = true;
	bStreamOver_ = false;
	streamOverIndex_ = -1;

	ZeroMemory(lastStreamCopyPos_, sizeof(DWORD) * 2);
	ZeroMemory(bufferPositionAtCopy_, sizeof(DWORD) * 2);

	lastReadPointer_ = 0;
}
SoundStreamingPlayer::~SoundStreamingPlayer() {
	this->Stop();

	for (size_t iEvent = 0; iEvent < 3; ++iEvent)
		::CloseHandle(hEvent_[iEvent]);
	ptr_release(pDirectSoundNotify_);
}
void SoundStreamingPlayer::_CreateSoundEvent(WAVEFORMATEX& formatWave) {
	sizeCopy_ = formatWave.nAvgBytesPerSec;

	HRESULT hrNotify = pDirectSoundBuffer_->QueryInterface(IID_IDirectSoundNotify, (LPVOID*)&pDirectSoundNotify_);
	DSBPOSITIONNOTIFY pn[3];
	for (size_t iEvent = 0; iEvent < 3; ++iEvent) {
		hEvent_[iEvent] = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		pn[iEvent].hEventNotify = hEvent_[iEvent];
	}

	pn[0].dwOffset = 0;
	pn[1].dwOffset = sizeCopy_;
	pn[2].dwOffset = DSBPN_OFFSETSTOP;
	pDirectSoundNotify_->SetNotificationPositions(3, pn);

	if (hrNotify == DSERR_BUFFERLOST) {
		this->Restore();
	}
}
void SoundStreamingPlayer::_CopyStream(int indexCopy) {
	if (pDirectSoundBuffer_ == nullptr) return;
	{
		//Lock lock(lock_);

		LPVOID pMem1, pMem2;
		DWORD dwSize1, dwSize2;

		DWORD copyOffset = sizeCopy_ * indexCopy;

		pDirectSoundBuffer_->GetCurrentPosition(&bufferPositionAtCopy_[indexCopy], nullptr);

		//Logger::WriteTop(StringUtility::Format("_CopyStream(%d): %u -> %u", indexCopy,
		//	bufferPositionAtCopy_[indexCopy], copyOffset));

		HRESULT hr = pDirectSoundBuffer_->Lock(copyOffset, sizeCopy_, &pMem1, &dwSize1, &pMem2, &dwSize2, 0);
		if (hr == DSERR_BUFFERLOST) {
			hr = pDirectSoundBuffer_->Restore();
			hr = pDirectSoundBuffer_->Lock(copyOffset, sizeCopy_, &pMem1, &dwSize1, &pMem2, &dwSize2, 0);
		}

		if (FAILED(hr))
			Stop();
		else {
			if (bStreamOver_) {
				if (indexCopy == streamOverIndex_) {
					streamOverIndex_ = -1;
					Stop();
					goto lab_unlock_buf;
				}
				goto lab_set_mem_zero;
			}
			else if (bStreaming_ && IsPlaying()) {
				if (dwSize1 > 0) {
					DWORD res = _CopyBuffer(pMem1, dwSize1);
					lastStreamCopyPos_[indexCopy] = res;
				}
				if (dwSize2 > 0) {
					_CopyBuffer(pMem2, dwSize2);
				}
				if (bStreamOver_)
					streamOverIndex_ = indexCopy;
			}
			else {
lab_set_mem_zero:
				memset(pMem1, 0, dwSize1);
				if (dwSize2 != 0)
					memset(pMem2, 0, dwSize2);
			}

lab_unlock_buf:
			pDirectSoundBuffer_->Unlock(pMem1, dwSize1, pMem2, dwSize2);
		}
	}
}
bool SoundStreamingPlayer::Play() {
	if (pDirectSoundBuffer_ == nullptr) return false;
	if (IsPlaying()) return true;

	{
		Lock lock(lock_);

		if (bFadeDelete_)
			SetVolumeRate(100);
		bFadeDelete_ = false;

		SetFade(0);

		bStreamOver_ = false;
		if (!bPause_ || !playStyle_.bResume_ || playStyle_.timeStart_ >= 0) {
			this->Seek(playStyle_.timeStart_ >= 0 ? playStyle_.timeStart_ : 0);
			pDirectSoundBuffer_->SetCurrentPosition(0);
		}
		playStyle_.timeStart_ = -1;

		for (size_t iEvent = 0; iEvent < 3; ++iEvent)
			::ResetEvent(hEvent_[iEvent]);

		if (bStreaming_) {
			thread_->Start();
			pDirectSoundBuffer_->Play(0, 0, DSBPLAY_LOOPING);
		}
		else {
			DWORD dwFlags = 0;
			if (playStyle_.bLoop_)
				dwFlags = DSBPLAY_LOOPING;
			pDirectSoundBuffer_->Play(0, 0, dwFlags);
		}
		bPause_ = false;
	}
	return true;
}
bool SoundStreamingPlayer::Stop() {
	{
		Lock lock(lock_);

		if (IsPlaying())
			bPause_ = true;

		if (pDirectSoundBuffer_)
			pDirectSoundBuffer_->Stop();

		if (!thread_->IsStop())
			thread_->Stop();
	}
	return true;
}
void SoundStreamingPlayer::ResetStreamForSeek() {
	if (pDirectSoundBuffer_) {
		_CopyStream(1);
		_CopyStream(0);

		pDirectSoundBuffer_->SetCurrentPosition(0);
	}
}
bool SoundStreamingPlayer::IsPlaying() {
	return thread_->GetStatus() == Thread::RUN;
}
DWORD SoundStreamingPlayer::GetCurrentPosition() {
	Lock lock(lock_);

	DWORD currentReader = 0;
	if (pDirectSoundBuffer_) {
		HRESULT hr = pDirectSoundBuffer_->GetCurrentPosition(&currentReader, nullptr);
		if (FAILED(hr)) currentReader = 0;
	}
	
	DWORD p0 = lastStreamCopyPos_[0];
	DWORD p1 = lastStreamCopyPos_[1];
	if (p0 < p1)
		return p0 + currentReader;
	else
		return p1 + currentReader - bufferPositionAtCopy_[0];
}

bool SoundStreamingPlayer::GetSamplesFFT(DWORD durationMs, size_t resolution, bool bAutoLog, std::vector<double>& res) {
	res.resize(resolution, 0);

	if (durationMs > 0 && pDirectSoundBuffer_ && IsPlaying()) {
		DWORD sampleRate = soundSource_->formatWave_.nSamplesPerSec;
		DWORD bytePerSample = soundSource_->formatWave_.wBitsPerSample / 8U;
		DWORD nChannels = soundSource_->formatWave_.nChannels;

		DWORD samplesNeeded = durationMs * sampleRate / 1000U;
		samplesNeeded = std::clamp<DWORD>(samplesNeeded, 32, sampleRate / 4);

		DWORD sizeLock = samplesNeeded * bytePerSample;

		DWORD currentPos = 0;
		if (pDirectSoundBuffer_) {
			HRESULT hr = pDirectSoundBuffer_->GetCurrentPosition(&currentPos, nullptr);
			if (FAILED(hr)) currentPos = 0;
		}

		std::vector<double> samples;

		void* pMem1, *pMem2;
		DWORD dwSize1, dwSize2;
		HRESULT hr = pDirectSoundBuffer_->Lock(currentPos, sizeLock, &pMem1, &dwSize1, &pMem2, &dwSize2, 0);
		if (SUCCEEDED(hr)) {
			samples.resize(samplesNeeded);

			// TODO: Switch to iterator concatenation instead of a temporary array

			std::vector<byte> tempData;
			tempData.reserve(dwSize1 + dwSize2);
			tempData.insert(tempData.end(), (byte*)pMem1, (byte*)pMem1 + dwSize1);
			tempData.insert(tempData.end(), (byte*)pMem2, (byte*)pMem2 + dwSize2);

			_LoadSamples(tempData.data(), tempData.size(), samplesNeeded, samples.data());

			pDirectSoundBuffer_->Unlock(pMem1, dwSize1, pMem2, dwSize2);
		}
		else return false;

		_DoFFT(samples, res, GetVolumeRate() / 100, bAutoLog);

		return true;
	}

	return false;
}

//StreamingThread
SoundStreamingPlayer::StreamingThread::StreamingThread(SoundStreamingPlayer* player) { 
	_SetOuter(player); 
}
void SoundStreamingPlayer::StreamingThread::_Run() {
	SoundStreamingPlayer* player = _GetOuter();

	DWORD point = 0;
	if (player->pDirectSoundBuffer_)
		player->pDirectSoundBuffer_->GetCurrentPosition(&point, 0);
	if (point == 0)
		player->_CopyStream(0);

	while (this->GetStatus() == RUN) {
		DWORD num = WaitForMultipleObjects(3, player->hEvent_, FALSE, INFINITE);
		
		player->pDirectSoundBuffer_->GetCurrentPosition(&point, 0);
		if (num == WAIT_OBJECT_0) {
			if ((point - 0) < 4096)
				player->_CopyStream(1);
		}
		else if (num == WAIT_OBJECT_0 + 1) {
			if ((point - player->sizeCopy_) < 4096)
				player->_CopyStream(0);
		}
		else if (num == WAIT_OBJECT_0 + 2)
			break;

		point = UINT_MAX;
	}
}
void SoundStreamingPlayer::StreamingThread::Notify(size_t index) {
	SoundStreamingPlayer* player = _GetOuter();
	::SetEvent(player->hEvent_[index]);
}

//*******************************************************************
//SoundPlayerWave
//*******************************************************************
SoundPlayerWave::SoundPlayerWave() {

}
SoundPlayerWave::~SoundPlayerWave() {
}
bool SoundPlayerWave::_CreateBuffer(shared_ptr<SoundSourceData> source) {
	FileManager* fileManager = FileManager::GetBase();
	DirectSoundManager* soundManager = DirectSoundManager::GetBase();

	{
		//Lock lock(soundManager->GetLock());

		soundSource_ = source;

		if (auto pSource = std::dynamic_pointer_cast<SoundSourceDataWave>(source)) {
			shared_ptr<FileReader> reader = pSource->reader_;

			try {
				DWORD waveSize = pSource->audioSizeTotal_;

				DSBUFFERDESC desc;
				ZeroMemory(&desc, sizeof(DSBUFFERDESC));
				desc.dwSize = sizeof(DSBUFFERDESC);
				desc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN | DSBCAPS_CTRLFREQUENCY
					| DSBCAPS_GETCURRENTPOSITION2 
					| DSBCAPS_LOCSOFTWARE | DSBCAPS_GLOBALFOCUS;
				desc.dwBufferBytes = waveSize;
				desc.lpwfxFormat = &pSource->formatWave_;
				HRESULT hrBuffer = soundManager->GetDirectSound()->CreateSoundBuffer(&desc,
					(LPDIRECTSOUNDBUFFER*)&pDirectSoundBuffer_, nullptr);
				if (FAILED(hrBuffer))
					throw gstd::wexception("IDirectSound8::CreateSoundBuffer failure");

				if (pDirectSoundBuffer_) {
					LPVOID pMem;
					DWORD dwSize;

					HRESULT hrLock = pDirectSoundBuffer_->Lock(0, waveSize, &pMem, &dwSize, nullptr, nullptr, 0);
					if (hrLock == DSERR_BUFFERLOST) {
						hrLock = pDirectSoundBuffer_->Restore();
						hrLock = pDirectSoundBuffer_->Lock(0, waveSize, &pMem, &dwSize, nullptr, nullptr, 0);
					}
					if (FAILED(hrLock))
						throw gstd::wexception("IDirectSoundBuffer8::Lock failure");

					if (pSource->bufWaveData_.GetSize() > 0)
						memcpy(pMem, pSource->bufWaveData_.GetPointer(), dwSize);
					else
						memset(pMem, 0, dwSize);

					pDirectSoundBuffer_->Unlock(pMem, dwSize, nullptr, 0);
				}
			}
			catch (bool) {
				return false;
			}
			catch (gstd::wexception& e) {
				throw e;
			}
		}
		else return false;
	}

	return true;
}
bool SoundPlayerWave::Play() {
	if (pDirectSoundBuffer_ == nullptr) return false;
	{
		Lock lock(lock_);

		if (bFadeDelete_)
			SetVolumeRate(100);
		bFadeDelete_ = false;

		SetFade(0);

		DWORD dwFlags = 0;
		if (playStyle_.bLoop_)
			dwFlags = DSBPLAY_LOOPING;

		if (!bPause_ || !playStyle_.bResume_ || playStyle_.timeStart_ >= 0) {
			this->Seek(playStyle_.timeStart_ >= 0 ? playStyle_.timeStart_ : 0);
		}
		playStyle_.timeStart_ = -1;

		HRESULT hr = pDirectSoundBuffer_->Play(0, 0, dwFlags);
		if (hr == DSERR_BUFFERLOST) {
			this->Restore();
		}

		bPause_ = false;
	}
	return true;
}
bool SoundPlayerWave::Stop() {
	{
		Lock lock(lock_);
		if (IsPlaying())
			bPause_ = true;

		if (pDirectSoundBuffer_)
			pDirectSoundBuffer_->Stop();
	}
	return true;
}
bool SoundPlayerWave::IsPlaying() {
	if (pDirectSoundBuffer_ == nullptr) return false;
	DWORD status = 0;
	pDirectSoundBuffer_->GetStatus(&status);
	bool res = (status & DSBSTATUS_PLAYING) > 0;
	return res;
}
bool SoundPlayerWave::Seek(double time) {
	if (soundSource_ == nullptr) return false;
	return Seek((DWORD)(time * soundSource_->formatWave_.nSamplesPerSec));
}
bool SoundPlayerWave::Seek(DWORD sample) {
	if (soundSource_ == nullptr || pDirectSoundBuffer_ == nullptr) return false;
	{
		Lock lock(lock_);
		pDirectSoundBuffer_->SetCurrentPosition(sample * soundSource_->formatWave_.nBlockAlign);
	}
	return true;
}
//*******************************************************************
//SoundStreamingPlayerWave
//*******************************************************************
SoundStreamingPlayerWave::SoundStreamingPlayerWave() {
}
bool SoundStreamingPlayerWave::_CreateBuffer(shared_ptr<SoundSourceData> source) {
	FileManager* fileManager = FileManager::GetBase();
	DirectSoundManager* soundManager = DirectSoundManager::GetBase();

	{
		//Lock lock(soundManager->GetLock());

		soundSource_ = source;

		if (auto pSource = std::dynamic_pointer_cast<SoundSourceDataWave>(source)) {
			shared_ptr<FileReader> reader = pSource->reader_;

			try {
				DWORD sizeBuffer = 2U * pSource->formatWave_.nAvgBytesPerSec;

				DSBUFFERDESC desc;
				ZeroMemory(&desc, sizeof(DSBUFFERDESC));
				desc.dwSize = sizeof(DSBUFFERDESC);
				desc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN | DSBCAPS_CTRLFREQUENCY
					| DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_GETCURRENTPOSITION2
					| DSBCAPS_LOCSOFTWARE | DSBCAPS_GLOBALFOCUS;
				desc.dwBufferBytes = sizeBuffer;
				desc.lpwfxFormat = &pSource->formatWave_;
				HRESULT hrBuffer = soundManager->GetDirectSound()->CreateSoundBuffer(&desc,
					(LPDIRECTSOUNDBUFFER*)&pDirectSoundBuffer_, nullptr);
				if (FAILED(hrBuffer))
					throw gstd::wexception("IDirectSound8::CreateSoundBuffer failure");

				sizeCopy_ = pSource->formatWave_.nAvgBytesPerSec;
				lastReadPointer_ = pSource->posWaveStart_;

				bStreaming_ = true;
				_CreateSoundEvent(pSource->formatWave_);
			}
			catch (bool) {
				return false;
			}
			catch (gstd::wexception& e) {
				throw e;
			}
		}
		else return false;
	}

	return true;
}
DWORD SoundStreamingPlayerWave::_CopyBuffer(LPVOID pMem, DWORD dwSize) {
	SoundSourceDataWave* source = (SoundSourceDataWave*)soundSource_.get();

	DWORD samplePerSec = source->formatWave_.nSamplesPerSec;
	DWORD bytePerSample = source->formatWave_.nBlockAlign;
	DWORD bytePerSec = source->formatWave_.nAvgBytesPerSec;

	DWORD resStreamPos = lastReadPointer_;

	memset(pMem, 0, dwSize);
	if (auto reader = source->reader_) {
		double loopStart = playStyle_.timeLoopStart_;
		double loopEnd = playStyle_.timeLoopEnd_;
		DWORD byteLoopStart = Math::FloorBase<DWORD>(loopStart * bytePerSec, bytePerSample);
		DWORD byteLoopEnd = Math::FloorBase<DWORD>(loopEnd * bytePerSec, bytePerSample);

		reader->Seek(lastReadPointer_);

		DWORD totalWritten = 0;
		auto _WriteBytes = [&](DWORD writeTargetSize) -> bool {
			if (writeTargetSize == 0) return true;
			DWORD written = reader->Read((char*)pMem + totalWritten, writeTargetSize);
			totalWritten += written;
			return written != writeTargetSize;	//If (written < target), then the read contains the EOF
		};

		while (totalWritten < dwSize) {
			DWORD byteCurrent = reader->GetFilePointer() - source->posWaveStart_;

			DWORD remain = dwSize - totalWritten;
			if (playStyle_.bLoop_ && (byteCurrent + remain > byteLoopEnd && byteLoopEnd > 0)) {
				//This read will contain the looping point
				DWORD size1 = std::min(byteLoopEnd - byteCurrent, remain);
				_WriteBytes(size1);
			}
			else {
				bool bFileEnd = _WriteBytes(remain);
				if (!bFileEnd)
					continue;
			}

			//Reset to loop start
			{
				if (playStyle_.bLoop_) {
					Seek(byteLoopStart / bytePerSample);
				}
				else {
					_SetStreamOver();
					break;
				}
			}
		}

		lastReadPointer_ = reader->GetFilePointer();
	}

	return resStreamPos;
}
bool SoundStreamingPlayerWave::Seek(double time) {
	if (soundSource_ == nullptr) return false;
	return Seek((DWORD)(time * soundSource_->formatWave_.nSamplesPerSec));
}
bool SoundStreamingPlayerWave::Seek(DWORD sample) {
	if (soundSource_ == nullptr) return false;
	SoundSourceDataWave* source = (SoundSourceDataWave*)soundSource_.get();
	{
		Lock lock(lock_);

		DWORD blockAlign = source->formatWave_.nBlockAlign;

		lastReadPointer_ = Math::FloorBase<DWORD>(source->posWaveStart_ + sample * blockAlign, blockAlign);
		source->reader_->Seek(lastReadPointer_);
	}
	return true;
}
//*******************************************************************
//SoundStreamingPlayerOgg
//*******************************************************************
SoundStreamingPlayerOgg::SoundStreamingPlayerOgg() {}
SoundStreamingPlayerOgg::~SoundStreamingPlayerOgg() {
	this->Stop();
	thread_->Join();
}
bool SoundStreamingPlayerOgg::_CreateBuffer(shared_ptr<SoundSourceData> source) {
	FileManager* fileManager = FileManager::GetBase();
	DirectSoundManager* soundManager = DirectSoundManager::GetBase();

	{
		//Lock lock(soundManager->GetLock());

		soundSource_ = source;

		if (auto pSource = std::dynamic_pointer_cast<SoundSourceDataOgg>(source)) {
			shared_ptr<FileReader> reader = pSource->reader_;

			try {
				DWORD sizeBuffer = std::min(2 * pSource->formatWave_.nAvgBytesPerSec, (DWORD)pSource->audioSizeTotal_);

				DSBUFFERDESC desc;
				ZeroMemory(&desc, sizeof(DSBUFFERDESC));
				desc.dwSize = sizeof(DSBUFFERDESC);
				desc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN | DSBCAPS_CTRLFREQUENCY
					| DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_GETCURRENTPOSITION2
					| DSBCAPS_LOCSOFTWARE | DSBCAPS_GLOBALFOCUS;
				desc.dwBufferBytes = sizeBuffer;
				desc.lpwfxFormat = &pSource->formatWave_;
				HRESULT hrBuffer = soundManager->GetDirectSound()->CreateSoundBuffer(&desc,
					(LPDIRECTSOUNDBUFFER*)&pDirectSoundBuffer_, nullptr);
				if (FAILED(hrBuffer))
					throw gstd::wexception("IDirectSound8::CreateSoundBuffer failure");

				sizeCopy_ = pSource->formatWave_.nAvgBytesPerSec;
				lastReadPointer_ = 0;

				bStreaming_ = sizeBuffer != pSource->audioSizeTotal_;
				if (!bStreaming_) {
					sizeCopy_ = pSource->audioSizeTotal_;
					_CopyStream(0);
				}
				else {
					_CreateSoundEvent(pSource->formatWave_);
				}
			}
			catch (bool) {
				return false;
			}
			catch (gstd::wexception& e) {
				throw e;
			}
		}
		else return false;
	}

	return true;
}
DWORD SoundStreamingPlayerOgg::_CopyBuffer(LPVOID pMem, DWORD dwSize) {
	SoundSourceDataOgg* source = (SoundSourceDataOgg*)soundSource_.get();
	
	DWORD samplePerSec = source->formatWave_.nSamplesPerSec;
	DWORD bytePerSample = source->formatWave_.nBlockAlign;
	DWORD bytePerSec = source->formatWave_.nAvgBytesPerSec;

	DWORD resStreamPos = lastReadPointer_ * bytePerSample;

	memset((char*)pMem, 0, dwSize);
	if (OggVorbis_File* pFileOgg = source->fileOgg_) {
		double loopStart = playStyle_.timeLoopStart_;
		double loopEnd = playStyle_.timeLoopEnd_;
		DWORD byteLoopStart = Math::FloorBase<DWORD>(loopStart * bytePerSec, bytePerSample);
		DWORD byteLoopEnd = Math::FloorBase<DWORD>(loopEnd * bytePerSec, bytePerSample);

		ov_pcm_seek(pFileOgg, lastReadPointer_);

		DWORD totalWritten = 0;
		auto _DecodeOgg = [&](DWORD writeTargetSize) -> bool {
			if (writeTargetSize == 0) return true;
			DWORD written = 0;
			while (written < writeTargetSize) {
				DWORD _remain = writeTargetSize - written;
				DWORD _write = ov_read(pFileOgg, (char*)pMem + totalWritten, _remain, 0, 2, 1, nullptr);
				if (_write == 0)
					return true;	//EOF
				written += _write;
				totalWritten += _write;
			}
			return false;
		};

		while (totalWritten < dwSize) {
			DWORD byteCurrent = ov_pcm_tell(pFileOgg) * bytePerSample;

			DWORD remain = dwSize - totalWritten;
			if (playStyle_.bLoop_ && (byteCurrent + remain > byteLoopEnd && byteLoopEnd > 0)) {
				//This read will contain the looping point
				DWORD size1 = std::min(byteLoopEnd - byteCurrent, remain);
				_DecodeOgg(size1);
			}
			else {
				bool bFileEnd = _DecodeOgg(remain);
				if (!bFileEnd)
					continue;
			}

			//Reset to loop start
			{
				if (playStyle_.bLoop_) {
					Seek(byteLoopStart / bytePerSample);
				}
				else {
					_SetStreamOver();
					break;
				}
			}
		}

		lastReadPointer_ = ov_pcm_tell(pFileOgg);
	}
	
	return resStreamPos;
}
bool SoundStreamingPlayerOgg::Seek(double time) {
	if (soundSource_ == nullptr) return false;
	return Seek((DWORD)(time * soundSource_->formatWave_.nSamplesPerSec));
}
bool SoundStreamingPlayerOgg::Seek(DWORD sample) {
	if (soundSource_ == nullptr) return false;
	SoundSourceDataOgg* source = (SoundSourceDataOgg*)soundSource_.get();
	{
		Lock lock(lock_);
		ov_pcm_seek(source->fileOgg_, sample);
		lastReadPointer_ = sample;
	}
	return true;
}
