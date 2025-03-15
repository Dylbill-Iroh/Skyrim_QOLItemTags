#include <Windows.h>
#include <iostream>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>
#include <functional>
#include "SendUIMessage.h"
#include "logger.h"
#include "mini/ini.h"
#include "GeneralFunctions.h"
#include "KeyInput.h"
#include "mINIHelper.h"
#include "ConsoleUtil.h"
#include "Serialization.h"
#include "UIGfx.h"
#include "Utility.h"

bool isSerializing = false;
bool pluginLoaded = false;
RE::Calendar* calendar;
RE::UI* ui;
RE::BSScript::Internal::VirtualMachine* vm;
RE::PlayerCharacter* player;
RE::FormID playerFormID;
RE::VMHandle playerHandle;

std::vector<std::string_view> itemMenus = {
    RE::InventoryMenu::MENU_NAME,
    RE::BarterMenu::MENU_NAME,
    RE::ContainerMenu::MENU_NAME,
    RE::GiftMenu::MENU_NAME,
    RE::CraftingMenu::MENU_NAME,
    RE::FavoritesMenu::MENU_NAME
};
std::string_view lastItemMenuOpened;
bool IsItemMenuOpen = false;

std::string storageScriptName = "QolItemTagsStorage";
RE::BSFixedString bsStorageScriptName = "QolItemTagsStorage";
uint32_t recentItemIdsRecord = 'tsRI';
uint32_t recentItemsRecord = 'RIra';
uint32_t questItemsRecord = 'QIra';
std::mutex recentItemsMutex;
std::mutex questItemsMutex;
std::vector<uint32_t> recentItemIds;
std::vector<RE::TESForm*> recentItems;
std::vector<RE::TESObjectREFR*> questItems;
std::vector<uint32_t> ignoredFormIDs;
std::vector<uint32_t> ignoredRefIDs;
int iMenuRefreshMillisecondDelay = 100;
int numOfRecentItemsDisplayed = 5;
std::string questTag = "";
std::string recentTag = "";
int recentTagLength = 0;
int questTagLength = 0;
RE::FormID goldFormID = 0xf;
int menuCategoryListActiveIndex = 0;
bool containerMenuOpen = false;
bool barterMenuOpen = false;

//std::string recentTag = "<font color='#00e600'>[R]</font>"; //color works for hud menu but not item menus

bool IsItemMenu(std::string_view menuName) {
    for (auto& menu : itemMenus) {
        if (menuName == menu) {
            return true;
        }
    }
    return false;
}

std::vector<RE::TESObjectREFR*> GetQuestObjectRefsInContainer(RE::TESObjectREFR* containerRef) {
    //logger::trace("called");

    std::vector<RE::TESObjectREFR*> invQuestItems;
    RE::TESDataHandler* dataHandler = RE::TESDataHandler::GetSingleton();

    if (!gfuncs::IsFormValid(containerRef)) {
        logger::warn("containerRef doesn't exist");
        return invQuestItems;
    }

    auto inventory = containerRef->GetInventory();

    if (inventory.size() == 0) {
        logger::debug("containerRef[{}] doesn't contain any items", gfuncs::GetFormName(containerRef));
        return invQuestItems;
    }

    if (dataHandler) {
        RE::BSTArray<RE::TESForm*>* akArray = &(dataHandler->GetFormArray(RE::FormType::Quest));
        RE::BSTArray<RE::TESForm*>::iterator itrEndType = akArray->end();

        //logger::debug("number of quests is {}", akArray->size());
        int ic = 0;

        for (RE::BSTArray<RE::TESForm*>::iterator itr = akArray->begin(); itr != akArray->end() && ic < akArray->size(); itr++, ic++) {
            RE::TESForm* baseForm = *itr;

            if (gfuncs::IsFormValid(baseForm)) {
                RE::TESQuest* quest = baseForm->As<RE::TESQuest>();
                if (gfuncs::IsFormValid(quest)) {
                    if (quest->aliases.size() > 0) {
                        for (int i = 0; i < quest->aliases.size(); i++) {
                            if (quest->aliases[i]) {
                                if (quest->aliases[i]->IsQuestObject()) {
                                    RE::BGSRefAlias* refAlias = static_cast<RE::BGSRefAlias*>(quest->aliases[i]);
                                    if (refAlias) {
                                        RE::TESObjectREFR* akRef = refAlias->GetReference();
                                        if (gfuncs::ContainerContainsRef(containerRef, akRef)) {
                                            invQuestItems.push_back(akRef);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return invQuestItems;
}

void AddToRecentItemIds(uint32_t& id) {
    recentItemsMutex.lock();
    auto it = std::find(recentItemIds.begin(), recentItemIds.end(), id);
    if (it != recentItemIds.end()) {
        recentItemIds.erase(it);
    }
    recentItemIds.push_back(id);
    recentItemsMutex.unlock();
}

std::vector<std::string> GetFilesInFolder(std::string folderPath) {
    std::vector<std::string> filePaths;
    for (const auto& entry : std::filesystem::directory_iterator(folderPath)) {
        filePaths.push_back(entry.path().generic_string());
    }
    return filePaths;
}

std::string GetFileDataString(std::string filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return ""; // Handle file opening error as needed
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

RE::FormID HexStringToFormID(std::string s, std::string modIndex) {
    //logger::info("s[{}]", s);

    RE::FormID id = 0;

    //remove whitespaces from string s
    s.erase(std::remove_if(s.begin(), s.end(), ::isspace), s.end());

    if (modIndex == "") {
        return id;
    }

    if (!gfuncs::IsHexString(s)) {
        //logger::info("s[{}] is not a valid hex number", s);
        return id;
    }

    std::string hexId = s.substr(0, 2);
    transform(hexId.begin(), hexId.end(), hexId.begin(), ::tolower);

    if (hexId == "fe") {
        //light mod form id. fexxxyyy, only y is the actual id. x is mod load order index.
        hexId = (hexId + modIndex + s.substr(5));
    }
    else {
        //not light mod form id. xxyyyyyy, only y is the actual id. x is mod load order index.
        hexId = (modIndex + (s.substr(2)));
    }

    id = gfuncs::HexToIntPapyrus(hexId);

    //logger::trace("modIndex[{}] s[{}] hexId[{}] id[{:x}] intID[{}]", modIndex, s, hexId, id, id);
    return id;
}

void FillIgnoredIDsFromFile(std::string& filePath, std::string startString, std::string endString, std::vector<uint32_t>& v) {
    std::string fileData = GetFileDataString(filePath);
    if (fileData == "") {
        return;
    }
    logger::trace("filePath[{}]", filePath);

    size_t iStart = fileData.find(startString);
    if (iStart != std::string::npos) {
        size_t iEnd = fileData.find(endString);
        if (iEnd != std::string::npos) {
            iEnd += (endString.size());
            fileData = fileData.substr(iStart, (iEnd - iStart));
            iStart = fileData.find('\n', 0);
            if (iStart != std::string::npos) {
                iStart++;
                iEnd = fileData.find('\n', iStart);
                std::string sModIndex = "";

                while (iEnd != std::string::npos) {
                    std::string line = fileData.substr(iStart, (iEnd - iStart));
                    //logger::trace("line[{}]", line);

                    int iModNameStart = line.find("(");
                    if (iModNameStart != std::string::npos) {
                        //logger::trace("[ found");
                        iModNameStart++;
                        std::string modName;
                        int iModNameEnd = line.find(")", iModNameStart);
                        if (iModNameEnd != std::string::npos) {
                            //logger::trace("] found");
                            modName = line.substr(iModNameStart, (iModNameEnd - iModNameStart));
                            bool isLightMod = false;
                            int iModIndex = GetLoadedModIndex(modName);
                            if (iModIndex == -1) {
                                iModIndex = GetLoadedLightModIndex(modName);
                                isLightMod = true;
                            }
                            if (iModIndex != -1) {
                                sModIndex = gfuncs::IntToHexPapyrus(iModIndex);
                                int sModIndexSize = sModIndex.size();
                                if (isLightMod) {
                                    //light mod index string must be 3 characters to get an accurate int for formId. 
                                    //light mod form ids are fexxxyyy where xxx is the mod index.
                                    while (sModIndexSize < 3) {
                                        sModIndex = ("0" + sModIndex);
                                        sModIndexSize++;
                                    }
                                }
                                else {
                                    //normal mod index string must be 2 characters to get an accurate int for formId. 
                                    //normal mod form ids are xxyyyyyy where xx is the mod index.
                                    while (sModIndexSize < 2) {
                                        sModIndex = ("0" + sModIndex);
                                        sModIndexSize++;
                                    }
                                }
                            }
                            else {
                                sModIndex = "";
                            }
                            logger::trace("modName changed to [{}] index[{}]", modName, iModIndex);
                        }
                        else {
                            sModIndex = "";
                        }
                    }
                    else {
                        RE::FormID id = HexStringToFormID(line, sModIndex);
                        if (id != 0) {
                            v.push_back(id);
                        }
                    }

                    iStart = (iEnd + 1);
                    iEnd = fileData.find('\n', iStart);
                }
            }
        }
    }
}

void LoadIgnoredFormIDs() {
    //logger::info("");
    std::vector<std::string> ignoredFilePaths = GetFilesInFolder("Data/SKSE/Plugins/QOLItemTags_IgnoredFormIds");

    ignoredFormIDs.clear();

    for (auto& path : ignoredFilePaths) {
        FillIgnoredIDsFromFile(path, "[IgnoredBaseFormIds]", "[EndIgnoredBaseFormIds]", ignoredFormIDs);
    }

    gfuncs::RemoveDuplicates(ignoredFormIDs);
    logger::trace("ignoredFormIDs.size() = {}", ignoredFormIDs.size());
    for (auto& id : ignoredFormIDs) {
        logger::trace("ignoredFormID[{:x}]", id);
    }

    ignoredRefIDs.clear();
    for (auto& path : ignoredFilePaths) {
        FillIgnoredIDsFromFile(path, "[IgnoredRefFormIds]", "[EndIgnoredRefFormIds]", ignoredRefIDs);
    }

    gfuncs::RemoveDuplicates(ignoredRefIDs);

    logger::trace("ignoredRefIDs.size() = {}", ignoredRefIDs.size());
    for (auto& id : ignoredRefIDs) {
        logger::trace("ignoredRefID[{:x}]", id);
    }
}

void LoadSettingsFromIni() {
    mINI::INIFile file("Data/SKSE/Plugins/QOLItemTags.ini");
    mINI::INIStructure ini;
    file.read(ini);

    questTag = mINI::GetIniString(ini, "Main", "sQuestTag", "(Q)");
    if (questTag != "") {
        questTag += " ";
    }

    recentTag = mINI::GetIniString(ini, "Main", "sRecentTag", "(N)");
    if (recentTag != "") {
        recentTag += " ";
    }

    recentTagLength = recentTag.length();
    questTagLength = questTag.length();

    numOfRecentItemsDisplayed = mINI::GetIniInt(ini, "Main", "iNumOfRecentItemsDisplayed", 5);
    if (numOfRecentItemsDisplayed < 1) {
        numOfRecentItemsDisplayed = 1;
    }

    iMenuRefreshMillisecondDelay = mINI::GetIniInt(ini, "Main", "iMenuRefreshMillisecondDelay", 100);
    if (iMenuRefreshMillisecondDelay < 10) {
        iMenuRefreshMillisecondDelay = 10;
    }

    logger::info("questTag[{}] recentTag[{}] numOfRecentItemsDisplayed[{}] iMenuRefreshMillisecondDelay[{}]",
        questTag, recentTag, numOfRecentItemsDisplayed, iMenuRefreshMillisecondDelay);
}

std::string AddRecentItemTagToString(std::string s) {
    //logger::trace("s[{}]", s);
    if (s != "") {
        std::size_t recentTagIndex = s.find(recentTag);
        if (recentTagIndex == std::string::npos) {
            s = (recentTag + s);
        }
    }
    //logger::trace("s[{}]", s);
    return s;
}

std::string RemoveRecentItemTagFromString(std::string s) {
    //logger::trace("s[{}]", s);
    if (s != "") {
        std::size_t recentTagIndex = s.find(recentTag);
        if (recentTagIndex != std::string::npos) {
            s.erase(recentTagIndex, recentTagLength);
        }
    }
    //logger::trace("s[{}]", s);
    return s;
}

std::string AddQuestItemTagToString(std::string s) {
    //logger::trace("s[{}]", s);
    if (s != "") {
        std::size_t questTagIndex = s.find(questTag);
        if (questTagIndex == std::string::npos) {
            s = (questTag + s);
        }
    }
    //logger::trace("s[{}]", s);
    return s;
}

std::string RemoveQuestItemTagFromString(std::string s) {
    //logger::trace("s[{}]", s);
    if (s != "") {
        std::size_t questTagIndex = s.find(questTag);
        if (questTagIndex != std::string::npos) {
            s.erase(questTagIndex, questTagLength);
        }
    }
    //logger::trace("s[{}]", s);
    return s;
}

std::string RemoveItemTagsFromString(std::string s, bool isQuestObject) {
    //logger::trace("s[{}] isQuestObject[{}]", s, isQuestObject);

    if (s != "") {
        s = RemoveRecentItemTagFromString(s);

        if (!isQuestObject) {
            s = RemoveQuestItemTagFromString(s);
        }
        if (s.at(0) == ' ') {
            s.erase(0, 1);
        }
    }
    //logger::trace("s[{}] isQuestObject[{}]", s, isQuestObject);
    return s;
}

void RemoveItemTags(RE::TESObjectREFR* ref, RE::TESForm* baseObj) {
    std::string baseName = baseObj->GetName();

    if (gfuncs::IsFormValid(ref)) {
        bool isQuestObject = gfuncs::IsQuestObject(ref);
        std::string refName = ref->GetDisplayFullName();
        if (refName != "") {
            //logger::trace("ref[{}] isQuestObject[{}]", refName, isQuestObject);
            std::string newName = RemoveItemTagsFromString(refName, isQuestObject);
            if (baseName == refName && !isQuestObject) {
                if (newName != refName) {
                    gfuncs::SetFormName(baseObj, newName);
                    if (ref->GetDisplayFullName() != newName) {
                        gfuncs::SetFormName(baseObj, baseName);
                        ref->SetDisplayName(newName, true);
                    }
                }
            }
            else if (newName != refName) {
                ref->SetDisplayName(newName, true);
            }
            if (!isQuestObject) {
                questItemsMutex.lock();
                int index = gfuncs::GetIndexInVector(questItems, ref);
                if (index > -1) {
                    auto it = questItems.begin();
                    it += index;
                    questItems.erase(it);
                }
                questItemsMutex.unlock();
            }
        }
    }
    else {
        if (baseName != "") {
            //logger::trace("baseObj[{}]", baseName);
            std::string newName = RemoveItemTagsFromString(baseName, false);
            if (newName != baseName) {
                gfuncs::SetFormName(baseObj, newName);
            }
        }
    }
}

std::string AddItemTagsToString(std::string s, bool isQuestObject) {
    //logger::trace("s[{}] isQuestObject[{}]", s, isQuestObject);
    if (s != "") {
        s = RemoveQuestItemTagFromString(s);
        s = AddRecentItemTagToString(s);
        if (isQuestObject) {
            s = AddQuestItemTagToString(s);
        }
    }
    //logger::trace("s[{}] isQuestObject[{}]", s, isQuestObject);
    return s;
}

void AddItemTags(RE::TESObjectREFR* ref, RE::TESForm* baseObj) {
    //logger::trace("");
    std::string baseName = "";
    if (gfuncs::IsFormValid(baseObj)) {
        baseName = baseObj->GetName();
    }

    if (gfuncs::IsFormValid(ref)) {
        if (!gfuncs::IsFormValid(baseObj)) {
            baseObj = ref->GetBaseObject();
        }

        bool isQuestObject = gfuncs::IsQuestObject(ref);
        std::string refName = ref->GetDisplayFullName();
        std::string newName = AddItemTagsToString(refName, isQuestObject);
        //logger::trace("ref[{}] isQuestObject[{}]", refName, isQuestObject);

        if (refName == baseName && newName != "" && newName != refName && !isQuestObject) {
            //first try setting base form name 
            gfuncs::SetFormName(baseObj, newName);

            if (ref->GetDisplayFullName() != newName) {
                //ref has a unique name
                gfuncs::SetFormName(baseObj, baseName);
                ref->SetDisplayName(newName, true);
            }
        }
        else if (newName != "" && newName != refName) {
            //ref has a unique name
            ref->SetDisplayName(newName, true);
        }

        if (isQuestObject) {
            questItemsMutex.lock();
            if (gfuncs::GetIndexInVector(questItems, ref) == -1) {
                questItems.push_back(ref);
            }
            questItemsMutex.unlock();
        }
    }
    else if (baseName != "") {
        //logger::trace("baseObj[{}]", baseName);
        std::string newName = AddItemTagsToString(baseName, false);
        if (newName != baseName) {
            gfuncs::SetFormName(baseObj, newName);
        }
    }
}

void AddTagsToFormOrRef(RE::TESForm* form) {
    //logger::trace("");
    if (gfuncs::IsFormValid(form)) {
        RE::TESObjectREFR* formRef = form->AsReference();
        if (gfuncs::IsFormValid(formRef)) {
            auto boundObj = formRef->GetBaseObject();
            if (boundObj) {
                form = boundObj->As<RE::TESForm>();
                if (gfuncs::IsFormValid(form)) {
                    AddItemTags(formRef, form);
                }
            }
        }
        else {
            AddItemTags(nullptr, form);
        }
    }
}

void RemoveTagsFromFormOrRef(RE::TESForm* form) {
    //logger::trace("");
    if (gfuncs::IsFormValid(form)) {
        RE::TESObjectREFR* formRef = form->AsReference();
        if (gfuncs::IsFormValid(formRef)) {
            auto boundObj = formRef->GetBaseObject();
            if (boundObj) {
                form = boundObj->As<RE::TESForm>();
                if (gfuncs::IsFormValid(form)) {
                    RemoveItemTags(formRef, form);
                }
            }
        }
        else {
            RemoveItemTags(nullptr, form);
        }
    }
}

void RemoveTags() {
    recentItemsMutex.lock();
    size_t size = recentItems.size();
    for (size_t i = 0; i < size; i++) {
        auto* form = recentItems[i];
        if (gfuncs::IsFormValid(form)) {
            int itemCount = gfuncs::GetItemCount_NoChecks(player, form);
            if (itemCount > 0) {
                auto* ref = form->AsReference();
                if (gfuncs::IsFormValid(ref)) {
                    RE::TESForm* baseForm = ref->GetBaseObject();
                    if (gfuncs::IsFormValid(baseForm)) {
                        std::string baseName = baseForm->GetName();
                        std::string refName = ref->GetDisplayFullName();
                        if (baseName == refName && baseName != "") {
                            std::string newName = RemoveRecentItemTagFromString(baseName);
                            gfuncs::SetFormName(baseForm, newName);
                            if (ref->GetDisplayFullName() != newName) {
                                gfuncs::SetFormName(baseForm, baseName);
                                ref->SetDisplayName(newName, true);
                            }
                        }
                        else if (refName != "") {
                            std::string newName = RemoveRecentItemTagFromString(refName);
                            ref->SetDisplayName(newName, true);
                        }
                    }
                }
                else {
                    std::string baseName = form->GetName();
                    if (baseName != "") {
                        std::string newName = RemoveRecentItemTagFromString(baseName);
                        gfuncs::SetFormName(form, newName);
                    }
                }
            }
        }
    }
    recentItems.clear();
    recentItemsMutex.unlock();

    questItemsMutex.lock();
    for (auto* ref : questItems) {
        if (gfuncs::IsFormValid(ref)) {
            std::string refName = ref->GetDisplayFullName();
            if (refName != "") {
                std::string newName = RemoveQuestItemTagFromString(refName);
                if (newName != refName) {
                    ref->SetDisplayName(newName, true);
                    //logger::info("Setting display name to [{}]", newName);
                    auto it = std::find(questItems.begin(), questItems.end(), ref);
                    if (it == questItems.end()) {
                        questItems.push_back(ref);
                    }
                }
            }
        }
    }
    questItems.clear();
    questItemsMutex.unlock();
}

void AddTags() {
    //RE::DebugNotification("Adding tags");
    questItemsMutex.lock();
    auto playerQuestItems = GetQuestObjectRefsInContainer(player);
    for (auto* ref : playerQuestItems) {
        if (gfuncs::IsFormValid(ref)) {
            uint32_t refId = ref->GetFormID(); 
            auto itr = std::find(ignoredRefIDs.begin(), ignoredRefIDs.end(), refId);
            if (itr == ignoredRefIDs.end()) {
                std::string refName = ref->GetDisplayFullName();
                if (refName != "") {
                    std::string newName = AddQuestItemTagToString(refName);
                    if (newName != refName) {
                        ref->SetDisplayName(newName, true);
                        //logger::info("Setting display name to [{}]", newName);
                    }
                }
                auto it = std::find(questItems.begin(), questItems.end(), ref);
                if (it == questItems.end()) {
                    questItems.push_back(ref);
                }
            }
        }
    }
    questItemsMutex.unlock();

    recentItemsMutex.lock();
    std::vector<uint32_t> ids;
    size_t size = recentItemIds.size();
    //logger::info("size[{}]", size);
    int count = 0;
    for (int i = (size - 1); i >= 0 && count < numOfRecentItemsDisplayed; i--) {
        auto* form = RE::TESForm::LookupByID(recentItemIds[i]);
        if (gfuncs::IsFormValid(form)) {
            int itemCount = gfuncs::GetItemCount_NoChecks(player, form);
            //logger::info("form[{}] itemCount[{}]", gfuncs::GetFormName(form), itemCount);
            if (itemCount > 0) {
                bool changedName = false;
                auto* ref = form->AsReference();
                if (gfuncs::IsFormValid(ref)) {
                    auto refitr = std::find(ignoredRefIDs.begin(), ignoredRefIDs.end(), recentItemIds[i]);
                    if (refitr == ignoredRefIDs.end()) {
                        RE::TESForm* baseForm = ref->GetBaseObject();
                        if (gfuncs::IsFormValid(baseForm)) {
                            uint32_t baseId = baseForm->GetFormID();
                            auto baseitr = std::find(ignoredFormIDs.begin(), ignoredFormIDs.end(), baseId);
                            if (baseitr == ignoredFormIDs.end()) {
                                std::string baseName = baseForm->GetName();
                                std::string refName = ref->GetDisplayFullName();
                                if (baseName == refName && baseName != "") {
                                    changedName = true;
                                    std::string newName = AddRecentItemTagToString(baseName);
                                    if (newName != baseName) {
                                        gfuncs::SetFormName(baseForm, newName);
                                        //logger::info("setting form name to [{}]", newName);
                                        if (ref->GetDisplayFullName() != newName) {
                                            gfuncs::SetFormName(baseForm, baseName);
                                            ref->SetDisplayName(newName, true);
                                            //logger::info("form name not set. setting display name to [{}]", newName);
                                        }
                                    }
                                }
                                else if (refName != "") {
                                    changedName = true;
                                    std::string newName = AddRecentItemTagToString(refName);
                                    if (newName != refName) {
                                        ref->SetDisplayName(newName, true);
                                        //logger::info("Setting display name to [{}]", newName);
                                    }
                                }
                            }
                        }
                    }
                }
                else {
                    auto baseitr = std::find(ignoredFormIDs.begin(), ignoredFormIDs.end(), recentItemIds[i]);
                    if (baseitr == ignoredFormIDs.end()) {
                        std::string baseName = form->GetName();
                        if (baseName != "") {
                            changedName = true;
                            std::string newName = AddRecentItemTagToString(baseName);
                            if (newName != baseName) {
                                gfuncs::SetFormName(form, newName);
                                //logger::info("setting form name to [{}]", newName);
                            }
                        }
                    }
                }

                if (changedName) {
                    recentItems.push_back(form);
                    ids.push_back(recentItemIds[i]);
                    count++;
                }
            }
        }
    }
    recentItemIds = ids;
    recentItemsMutex.unlock();

    std::this_thread::sleep_for(std::chrono::milliseconds(iMenuRefreshMillisecondDelay));
    gfuncs::RefreshItemMenu();
}

struct MenuOpenCloseEventSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    bool sinkAdded = false;

    RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*/*source*/) {
        if (!event) {
            logger::warn("MenuOpenClose Event doesn't exist");
            return RE::BSEventNotifyControl::kContinue;
        }

        if (IsBadReadPtr(event, sizeof(event))) {
            logger::warn("MenuOpenCloseEventSink IsBadReadPtr");
            return RE::BSEventNotifyControl::kContinue;
        }

        auto menuName = event->menuName;
        auto opening = event->opening;
        //logger::trace("MenuOpenCloseEvent menu[{}] opening[{}]", menuName, opening);

        bool validInputSinkMenu = false;
        if (opening) {
            if (IsItemMenu(menuName)) {
                if (IsItemMenuOpen) {
                    RemoveTags();
                }
                IsItemMenuOpen = true;
                lastItemMenuOpened = menuName;
                std::thread t(AddTags);
                t.detach();
            }
        }
        //menu closing
        else {
            if (IsItemMenuOpen) {
                if (!ui->IsItemMenuOpen()) {
                    IsItemMenuOpen = false;
                    RemoveTags();
                }
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }
};

MenuOpenCloseEventSink* menuOpenCloseEventSink;

struct ContainerChangedEventSink : public RE::BSTEventSink<RE::TESContainerChangedEvent> {
    bool sinkAdded = false;

    RE::BSEventNotifyControl ProcessEvent(const RE::TESContainerChangedEvent* event, RE::BSTEventSource<RE::TESContainerChangedEvent>*/*source*/) {
        //logger::trace("ContainerChangedEventSink");

        if (!event) {
            logger::warn("Container change event doesn't exist");
            return RE::BSEventNotifyControl::kContinue;
        }

        if (IsBadReadPtr(event, sizeof(event))) {
            logger::warn("Container change event IsBadReadPtr");
            return RE::BSEventNotifyControl::kContinue;
        }

        if (event->newContainer == playerFormID) {
            uint32_t baseId = event->baseObj;
            RE::ObjectRefHandle refHandle = event->reference;
            //uint32_t itemCount = event->itemCount;

            if (baseId == goldFormID) {
                //logger::trace("player added gold");
                return RE::BSEventNotifyControl::kContinue;
            }

            RE::TESObjectREFR* itemReference = gfuncs::GetRefFromObjectRefHandle(refHandle);
            if (itemReference) {
                uint32_t refId = itemReference->GetFormID();
                AddToRecentItemIds(refId);
            }
            else {
                AddToRecentItemIds(baseId);
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }
};

ContainerChangedEventSink* containerChangedEventSink;

void InstallQOLItemTags() {
    gfuncs::Install();
    LoadIgnoredFormIDs();
    LoadSettingsFromIni();

    player = RE::PlayerCharacter::GetSingleton();

    if (!player) {
        logger::critical("player character not found, QOL Item Tags aborting install.");
        return;
    }

    playerFormID = player->GetFormID();
    playerHandle = gfuncs::GetHandle(player);

    vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
    ui = RE::UI::GetSingleton();
    calendar = RE::Calendar::GetSingleton();

    auto* eventSourceholder = RE::ScriptEventSourceHolder::GetSingleton();
    if (!eventSourceholder && !containerChangedEventSink->sinkAdded) {
        logger::critical("eventSourceholder not found, QOL Item Tags aborting install."); 
        return;
    }

    if (!containerChangedEventSink) { containerChangedEventSink = new ContainerChangedEventSink; }
    if (!containerChangedEventSink->sinkAdded) {
        containerChangedEventSink->sinkAdded = true;
        eventSourceholder->AddEventSink(containerChangedEventSink);
    }

    if (!menuOpenCloseEventSink) { menuOpenCloseEventSink = new MenuOpenCloseEventSink(); }
    if (!menuOpenCloseEventSink->sinkAdded) {
        menuOpenCloseEventSink->sinkAdded = true;
        ui->AddEventSink<RE::MenuOpenCloseEvent>(menuOpenCloseEventSink);
    }

}

void LoadRecentItems(SKSE::SerializationInterface* ssi, std::vector<RE::TESForm*> loadedRecentItems, std::vector<RE::TESObjectREFR*> loadedQuestItems, std::vector<uint32_t> ids) {
    RemoveTags();
    recentItemsMutex.lock();
    recentItemIds = ids;
    recentItemsMutex.unlock();
    
    std::vector<RE::TESObjectREFR*> playerQuestItems = GetQuestObjectRefsInContainer(player);

    logger::trace("size[{}] loadedQuestItems size[{}] playerQuestItems size[{}] recentItemIds size[{}]",
        loadedRecentItems.size(), loadedQuestItems.size(), playerQuestItems.size(), recentItemIds.size());

    //LoadSettingsFromIni();

    int i = 0;
    int size = loadedRecentItems.size();

    for (i; i < size; i++) {
        RE::TESForm* akForm = loadedRecentItems[i];
        if (gfuncs::IsFormValid(akForm)) {
            RemoveTagsFromFormOrRef(akForm);
        }
    }

    for (RE::TESObjectREFR* ref : loadedQuestItems) {
        if (gfuncs::IsFormValid(ref)) {
            std::string refName = ref->GetDisplayFullName();
            std::string newName = RemoveQuestItemTagFromString(refName);
            if (newName != refName) {
                ref->SetDisplayName(newName, true);
            }
        }
    }

    for (RE::TESObjectREFR* ref : playerQuestItems) {
        std::string refName = ref->GetDisplayFullName();
        std::string newName = RemoveQuestItemTagFromString(refName);
        if (newName != refName) {
            ref->SetDisplayName(newName, true);
        }
    }
}

void LoadCallback(SKSE::SerializationInterface* ssi) {
    //logger::trace("called");
    if (ssi) {
        if (!isSerializing) {
            isSerializing = true;
            
            std::vector<RE::TESForm*> loadedRecentItems;
            std::vector<RE::TESObjectREFR*> loadedQuestItems;
            std::vector<uint32_t> ids;
            std::uint32_t type, version, length;

            while (ssi->GetNextRecordInfo(type, version, length)) {
                if (type == recentItemsRecord) {
                    loadedRecentItems = serialize::LoadFormVector(recentItemsRecord, ssi);
                }
                else if (type == questItemsRecord) {
                    loadedQuestItems = serialize::LoadObjectRefVector(questItemsRecord, ssi);
                }
                else if (type == recentItemIdsRecord) {
                    ids = serialize::LoadFormIDVector(recentItemIdsRecord, ssi);
                    
                }
            }

            LoadRecentItems(ssi, loadedRecentItems, loadedQuestItems, ids);
            isSerializing = false;
            //logger::trace("completed");
        }
        else {
            logger::debug("already loading or saving, aborting load.");
        }
    }
    else {
        logger::error("ssi doesn't exist, aborting load.");
    }
}

void SaveCallback(SKSE::SerializationInterface* ssi) {
    //logger::trace("called");
    if (ssi) {
        if (!isSerializing) {
            //isSerializing = true; //

            //recentItemsMutex.lock();
            //serialize::SaveFormVector(recentItems, recentItemsRecord, ssi);
            //recentItemsMutex.unlock();

            //questItemsMutex.lock();
            //serialize::SaveObjectRefVector(questItems, questItemsRecord, ssi);
            //questItemsMutex.unlock();

            recentItemsMutex.lock();
            serialize::SaveFormIDVector(recentItemIds, recentItemIdsRecord, ssi);
            recentItemsMutex.unlock();
            logger::trace("completed");
        }
        else {
            logger::debug("already loading or saving, aborting load.");
        }
    }
    else {
        logger::error("ssi doesn't exist, aborting load.");
    }
    logger::trace("completed");
}

void MessageListener(SKSE::MessagingInterface::Message* message) {
    //logger::trace("");

    switch (message->type) {
        // Descriptions are taken from the original skse64 library
        // See:
        // https://github.com/ianpatt/skse64/blob/09f520a2433747f33ae7d7c15b1164ca198932c3/skse64/PluginAPI.h#L193-L212

     //case SKSE::MessagingInterface::kPostLoad: //
        //    logger::info("kPostLoad: sent to registered plugins once all plugins have been loaded");
        //    break;

    //case SKSE::MessagingInterface::kPostPostLoad:
        //    logger::info(
        //        "kPostPostLoad: sent right after kPostLoad to facilitate the correct dispatching/registering of "
        //        "messages/listeners");
        //    break;

    //case SKSE::MessagingInterface::kPreLoadGame:
        //    // message->dataLen: length of file path, data: char* file path of .ess savegame file
        //    logger::info("kPreLoadGame: sent immediately before savegame is read");
        //    break;

    //case SKSE::MessagingInterface::kPostLoadGame:
        // You will probably want to handle this event if your plugin uses a Preload callback
        // as there is a chance that after that callback is invoked the game will encounter an error
        // while loading the saved game (eg. corrupted save) which may require you to reset some of your
        // plugin state.

        //logger::trace("kPostLoadGame: sent after an attempt to load a saved game has finished");
        //break;

    //case SKSE::MessagingInterface::kSaveGame:
        //    logger::info("kSaveGame");
        //    break;

    //case SKSE::MessagingInterface::kDeleteGame:
        //    // message->dataLen: length of file path, data: char* file path of .ess savegame file
        //    logger::info("kDeleteGame: sent right before deleting the .skse cosave and the .ess save");
        //    break;

    //case SKSE::MessagingInterface::kInputLoaded:
        //logger::info("kInputLoaded: sent right after game input is loaded, right before the main menu initializes");

        //break;

    //case SKSE::MessagingInterface::kNewGame:
        //logger::trace("kNewGame: sent after a new game is created, before the game has loaded");
        //AttachPapyrusStorageScript();
        //break;

    case SKSE::MessagingInterface::kDataLoaded:
        //logger::trace("kDataLoaded: sent after the data handler has loaded all its forms");
        InstallQOLItemTags();
        break;

        //default: //
            //    logger::info("Unknown system message of type: {}", message->type);
            //    break;
    }
}

//init================================================================================================================================================================
SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);

    SetupLog("Data/SKSE/Plugins/QOLItemTags.ini");
    SKSE::GetMessagingInterface()->RegisterListener(MessageListener);

    auto* serialization = SKSE::GetSerializationInterface();
    serialization->SetUniqueID('QOLI');
    serialization->SetSaveCallback(SaveCallback);
    serialization->SetLoadCallback(LoadCallback);

    return true;
}
