#pragma once

namespace gfx {
	std::string GetGFxTypeString(int type);

	std::string GetItemListPathForItemMenu(std::string_view menuName);

	void InvokeInt(std::string_view menuPath, std::string target, int arg);

	void SetItemMenuSelection(std::string_view menuPath, int index);

	std::string GetGfxValueAsString(const RE::GFxValue& gfxValue);

	std::string GetGfxValueAsString(RE::GFxValue& gfxValue);

	bool IsGfxMemberValid(const RE::GFxValue& gfxValue, std::string gfxName = "");

	bool IsGfxMemberValid(RE::GFxValue& gfxValue, std::string gfxName = "");

	bool GFxMemberNameIsValid(std::string name);

	void LogGFxMembers(const RE::GFxValue& gfxValue, std::string gfxName);

	void LogGFxMembers(RE::GPtr<RE::GFxMovieView> mv, std::vector<std::string> memberStrings);

	void EraseQuantityStringFromUIitemName(std::string& uiItemName);

	std::string GetGFxListEntryText(RE::GFxValue& listEntry);

	int GetEntryDataArrayLength(std::string_view menuName, RE::GPtr<RE::GFxMovieView>mv);

	int GetEntryDataArrayLength(std::string_view menuName);

	int GetIndexForMenuItem(std::string_view menuName, std::string sItemName);

	int GetSelectedEntryIndex(std::string_view menuName);

	int GetItemMenuActiveSegment(std::string_view menuName);

	std::string GetSelectedEntryText(std::string_view menuName);

	RE::GFxValue GetSelectedEntry(std::string_view menuName);
}