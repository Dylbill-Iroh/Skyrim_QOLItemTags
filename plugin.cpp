#include <Windows.h>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include "SendUIMessage.h"
#include "logger.h"
#include "mini/ini.h"
#include "GeneralFunctions.h"
#include "ConsoleUtil.h"
#include "Serialization.h"

bool isSerializing = false;
bool pluginLoaded = false;
RE::Calendar* calendar;
RE::UI* ui;
RE::PlayerCharacter* player;
RE::FormID playerFormID;
RE::VMHandle playerHandle;
std::string storageScriptName = "QolItemTagsStorage";
RE::BSFixedString bsStorageScriptName = "QolItemTagsStorage";
uint32_t recentItemsRecord = 'RIra';
uint32_t questItemsRecord = 'QIra';
std::mutex recentItemsMutex;
std::mutex questItemsMutex;
std::vector<RE::TESForm*> recentItems;
std::vector<RE::TESObjectREFR*> questItems;
int numOfRecentItemsDisplayed = 5;
int numOfRecentItemsSaved = 20;
std::string questTag = "(Q) ";
std::string recentTag = "(R) ";
int recentTagLength = 0;
int questTagLength = 0;
RE::FormID goldFormID = 0xf;

//std::string recentTag = "<font color='#00e600'>[R]</font>"; //color works for hud menu but not item menus

void LoadSettingsFromIni() {
    auto ini = mINI::GetIniFile("Data/SKSE/Plugins/QOLItemTags.ini");

    questTag = mINI::GetIniString(ini, "Main", "sQuestTag", "(Q)");
    if (questTag != "") {
        questTag += " ";
    }

    recentTag = mINI::GetIniString(ini, "Main", "sRecentTag", "(N)");
    if (recentTag != "") {
        recentTag += " ";
    }

    numOfRecentItemsDisplayed = mINI::GetIniInt(ini, "Main", "iNumOfRecentItemsDisplayed", 5);

    if (numOfRecentItemsDisplayed < 1) {
        numOfRecentItemsDisplayed = 1;
    }

    logger::info("{}: questTag[{}] recentTag[{}] numOfRecentItemsDisplayed[{}]",
        __func__, questTag, recentTag, numOfRecentItemsDisplayed);

    recentTagLength = recentTag.length();
    questTagLength = questTag.length();
}

std::string AddRecentItemTagToString(std::string s) {
    //logger::trace("{} s[{}]", __func__, s);
    if (s != "") {
        std::size_t recentTagIndex = s.find(recentTag);
        if (recentTagIndex == std::string::npos) {
            s = (recentTag + s);
        }
    }
    //logger::trace("{} s[{}]", __func__, s);
    return s;
}

std::string RemoveRecentItemTagFromString(std::string s) {
    //logger::trace("{} s[{}]", __func__, s);
    if (s != "") {
        std::size_t recentTagIndex = s.find(recentTag);
        if (recentTagIndex != std::string::npos) {
            s.erase(recentTagIndex, recentTagLength);
        }
    }
    //logger::trace("{} s[{}]", __func__, s);
    return s;
}

std::string AddQuestItemTagToString(std::string s) {
    //logger::trace("{} s[{}]", __func__, s);
    if (s != "") {
        std::size_t questTagIndex = s.find(questTag);
        if (questTagIndex == std::string::npos) {
            s = (questTag + s);
        }
    }
    //logger::trace("{} s[{}]", __func__, s);
    return s;
}

std::string RemoveQuestItemTagFromString(std::string s) {
    //logger::trace("{} s[{}]", __func__, s);
    if (s != "") {
        std::size_t questTagIndex = s.find(questTag);
        if (questTagIndex != std::string::npos) {
            s.erase(questTagIndex, questTagLength);
        }
    }
    //logger::trace("{} s[{}]", __func__, s);
    return s;
}

std::string RemoveItemTagsFromString(std::string s, bool isQuestObject) {
    //logger::trace("{}: s[{}] isQuestObject[{}]", __func__, s, isQuestObject);

    if (s != "") {
        s = RemoveRecentItemTagFromString(s);

        if (!isQuestObject) {
            s = RemoveQuestItemTagFromString(s);
        }
        if (s.at(0) == ' ') {
            s.erase(0, 1);
        }
    }
    //logger::trace("{}: s[{}] isQuestObject[{}]", __func__, s, isQuestObject);
    return s;
}

void RemoveItemTags(RE::TESObjectREFR* ref, RE::TESForm* baseObj) {
    std::string baseName = baseObj->GetName();

    if (gfuncs::IsFormValid(ref)) {
        bool isQuestObject = gfuncs::IsQuestObject(ref);
        std::string refName = ref->GetDisplayFullName();
        if (refName != "") {
            //logger::trace("{}: ref[{}] isQuestObject[{}]", __func__, refName, isQuestObject);
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
            //logger::trace("{}: baseObj[{}]", __func__, baseName);
            std::string newName = RemoveItemTagsFromString(baseName, false);
            if (newName != baseName) {
                gfuncs::SetFormName(baseObj, newName);
            }
        }
    }
}

std::string AddItemTagsToString(std::string s, bool isQuestObject) {
    //logger::trace("{}: s[{}] isQuestObject[{}]", __func__, s, isQuestObject);
    if (s != "") {
        s = RemoveQuestItemTagFromString(s);
        s = AddRecentItemTagToString(s);
        if (isQuestObject) {
            s = AddQuestItemTagToString(s);
        }
    }
    //logger::trace("{}: s[{}] isQuestObject[{}]", __func__, s, isQuestObject);
    return s;
}

void AddItemTags(RE::TESObjectREFR* ref, RE::TESForm* baseObj) {
    //logger::trace("{}", __func__);
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
        //logger::trace("{}: ref[{}] isQuestObject[{}]", __func__, refName, isQuestObject);

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
        //logger::trace("{}: baseObj[{}]", __func__, baseName);
        std::string newName = AddItemTagsToString(baseName, false);
        if (newName != baseName) {
            gfuncs::SetFormName(baseObj, newName);
        }
    }
}

void AddTagsToFormOrRef(RE::TESForm* form) {
    //logger::trace("{}", __func__);
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
    //logger::trace("{}", __func__);
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

void AddRecentItemToVec(RE::TESObjectREFR* ref, RE::TESForm* baseForm, bool addRef) {
    //logger::trace("{}", __func__);
    std::string oldName = "";
    std::string newName = "";

    if (addRef) {
        oldName = ref->GetDisplayFullName();
    }
    else {
        oldName = baseForm->GetName();
    }

    AddItemTags(ref, baseForm);
    recentItemsMutex.lock();

    if (addRef) {
        int index = gfuncs::GetIndexInVector(recentItems, ref);
        if (index > -1) { //ref is already in vector, remove it and then push_back to top of list
            auto it = recentItems.begin();
            it += index;
            recentItems.erase(it);
        }
        recentItems.push_back(ref);
    }
    else {
        int index = gfuncs::GetIndexInVector(recentItems, baseForm);
        if (index > -1) { //ref is already in vector, remove it and then push_back to top of list
            auto it = recentItems.begin();
            it += index;
            recentItems.erase(it);
        }
        recentItems.push_back(baseForm);
    }

    int size = recentItems.size();
    while (size > numOfRecentItemsDisplayed && size > 0) {
        RE::TESForm* form = recentItems[0];

        recentItems.erase(recentItems.begin());
        RemoveTagsFromFormOrRef(form);
        size = recentItems.size();
    }

    recentItemsMutex.unlock();

    if (addRef) {
        newName = ref->GetDisplayFullName();
    }
    else {
        newName = baseForm->GetName();
    }
    if (oldName != newName) {
        gfuncs::RefreshItemMenu();
    }
}

void RemoveRecentItemFromVec(RE::TESObjectREFR* ref, RE::TESForm* baseForm, int baseCount) {
    //logger::trace("{}", __func__);
    recentItemsMutex.lock();

    int index = gfuncs::GetIndexInVector(recentItems, ref);

    std::string oldName = "";
    std::string newName = "";

    if (index > -1) {
        oldName = ref->GetDisplayFullName();
        //logger::trace("{}: ref[{}]", __func__, ref->GetDisplayFullName());
        auto it = recentItems.begin();
        it += index;
        recentItems.erase(it);
        recentItemsMutex.unlock();
        RemoveItemTags(ref, baseForm);
        newName = ref->GetDisplayFullName();
    }
    else if (gfuncs::GetIndexInVector(questItems, ref) > -1) {
        oldName = ref->GetDisplayFullName();
        recentItemsMutex.unlock();
        RemoveItemTags(ref, baseForm);
        newName = ref->GetDisplayFullName();
    }

    else if (baseCount == 0) {
        //logger::trace("{}: baseForm[{}]", __func__, baseForm->GetName());
        oldName = baseForm->GetName();
        index = gfuncs::GetIndexInVector(recentItems, baseForm);
        if (index > -1) {
            auto it = recentItems.begin();
            it += index;
            recentItems.erase(it);
            recentItemsMutex.unlock();
            RemoveItemTags(nullptr, baseForm);
        }
        else {
            recentItemsMutex.unlock();
        }
        newName = baseForm->GetName();
    }
    else {
        recentItemsMutex.unlock();
    }

    if (oldName != newName) {
        gfuncs::RefreshItemMenu();
    }
}

void ProcessItemAddedOrRemoved(RE::TESForm* baseObj, RE::TESObjectREFR* itemReference, RE::FormID newContainer, RE::FormID oldContainer, int itemCount) {
    //must wait a little after containerChange event for itemCount to be accurate
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    int baseCount = 0;

    if (gfuncs::IsFormValid(baseObj)) {
        baseCount = player->GetItemCount(baseObj->As<RE::TESBoundObject>());
        //baseCount = gfuncs::GetBaseFormCount(player, baseObj->As<RE::TESBoundObject>());

        bool addRef = false;
        std::string refName = "null";
        bool playerHasItem = false;

        if (gfuncs::IsFormValid(itemReference)) {
            refName = itemReference->GetDisplayFullName();
            addRef = true;
        }

        if (newContainer == playerFormID) {
            logger::trace("{}: player added baseObj[{}] ref[{}] baseCount[{}] numberAdded[{}]",
                __func__, gfuncs::GetFormName(baseObj), refName, baseCount, itemCount);

            std::thread addItem(AddRecentItemToVec, itemReference, baseObj, addRef);
            addItem.join();
        }
        else if (oldContainer == playerFormID) {
            logger::trace("{}: player removed baseObj[{}] ref[{}] baseCount[{}] numberRemoved[{}]",
                __func__, gfuncs::GetFormName(baseObj), refName, baseCount, itemCount);

            std::thread removeItem(RemoveRecentItemFromVec, itemReference, baseObj, baseCount);
            removeItem.join();
        }
    }
}

struct ContainerChangedEventSink : public RE::BSTEventSink<RE::TESContainerChangedEvent> {
    bool sinkAdded = false;

    RE::BSEventNotifyControl ProcessEvent(const RE::TESContainerChangedEvent* event, RE::BSTEventSource<RE::TESContainerChangedEvent>*/*source*/) {
        //logger::trace("{} ContainerChangedEventSink", __func__);

        if (!event) {
            logger::error("Container change event doesn't exist");
            return RE::BSEventNotifyControl::kContinue;
        }

        if (IsBadReadPtr(event, sizeof(event))) {
            logger::error("Container change event IsBadReadPtr");
            return RE::BSEventNotifyControl::kContinue;
        }

        //player added or removed item
        if (event->newContainer == playerFormID || event->oldContainer == playerFormID) {
            if (event->baseObj == goldFormID) {
                //logger::trace("player added or removed gold");
                return RE::BSEventNotifyControl::kContinue;
            }
            int itemCount = event->itemCount;

            RE::TESForm* baseObj = nullptr;
            if (event->baseObj) {
                baseObj = RE::TESForm::LookupByID(event->baseObj);
                if (!gfuncs::IsFormValid(baseObj)) {
                    baseObj = nullptr;
                }
            }

            RE::TESObjectREFR* itemReference = gfuncs::GetRefFromObjectRefHandle(event->reference);

            std::thread tProcessItemAddedOrRemoved(ProcessItemAddedOrRemoved, baseObj, itemReference, event->newContainer, event->oldContainer, itemCount);
            tProcessItemAddedOrRemoved.detach();
        }

        return RE::BSEventNotifyControl::kContinue;
    }
};

ContainerChangedEventSink* containerChangedEventSink;

void InstallQOLItemTags() {
    gfuncs::Install();
    LoadSettingsFromIni();

    player = RE::PlayerCharacter::GetSingleton();

    if (!player) {
        logger::error("{}: player character not found, QOL Item Tags aborting install.", __func__);
        return;
    }

    playerFormID = player->GetFormID();
    playerHandle = gfuncs::GetHandle(player);

    ui = RE::UI::GetSingleton();
    calendar = RE::Calendar::GetSingleton();

    if (!containerChangedEventSink) { containerChangedEventSink = new ContainerChangedEventSink; }
    auto* eventSourceholder = RE::ScriptEventSourceHolder::GetSingleton();
    if (!eventSourceholder && !containerChangedEventSink->sinkAdded) {
        logger::error("{}: eventSourceholder not found, QOL Item Tags aborting install.", __func__);
        return;
    }
    if (!containerChangedEventSink->sinkAdded) {
        containerChangedEventSink->sinkAdded = true;
        eventSourceholder->AddEventSink(containerChangedEventSink);
    }
}

std::vector<RE::TESObjectREFR*> GetQuestObjectRefsInContainer(RE::TESObjectREFR* containerRef) {
    //logger::trace("{} called", __func__);

    std::vector<RE::TESObjectREFR*> invQuestItems;
    RE::TESDataHandler* dataHandler = RE::TESDataHandler::GetSingleton();

    if (!gfuncs::IsFormValid(containerRef)) {
        logger::warn("{} containerRef doesn't exist", __func__);
        return invQuestItems;
    }

    auto inventory = containerRef->GetInventory();

    if (inventory.size() == 0) {
        logger::debug("{} {} containerRef doesn't contain any items", __func__, gfuncs::GetFormName(containerRef));
        return invQuestItems;
    }

    if (dataHandler) {
        RE::BSTArray<RE::TESForm*>* akArray = &(dataHandler->GetFormArray(RE::FormType::Quest));
        RE::BSTArray<RE::TESForm*>::iterator itrEndType = akArray->end();

        //logger::debug("{} number of quests is {}", __func__, akArray->size());
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

void LoadRecentItems(SKSE::SerializationInterface* ssi, std::vector<RE::TESForm*> loadedRecentItems, std::vector<RE::TESObjectREFR*> loadedQuestItems) {
    std::vector<RE::TESObjectREFR*> playerQuestItems = GetQuestObjectRefsInContainer(player);

    logger::debug("{}: loadedRecentItems size[{}] loadedQuestItems size[{}] playerQuestItems size[{}]",
        __func__, loadedRecentItems.size(), loadedQuestItems.size(), playerQuestItems.size());

    recentItemsMutex.lock();

    for (RE::TESForm* akForm : recentItems) {
        RemoveTagsFromFormOrRef(akForm);
    }

    for (RE::TESObjectREFR* ref : questItems) {
        if (gfuncs::IsFormValid(ref)) {
            std::string refName = ref->GetDisplayFullName();
            std::string newName = RemoveQuestItemTagFromString(refName);
            if (newName != refName) {
                ref->SetDisplayName(newName, true);
            }
        }
    }

    LoadSettingsFromIni();

    recentItems.clear();
    int i = 0;
    int count = 0;
    int size = loadedRecentItems.size();

    for (i; i < size && count < numOfRecentItemsDisplayed; i++) {
        RE::TESForm* akForm = loadedRecentItems[i];
        if (gfuncs::IsFormValid(akForm)) {
            recentItems.push_back(akForm);
            AddTagsToFormOrRef(akForm);
            count++;
        }
    }

    questItems.clear();

    for (RE::TESObjectREFR* ref : loadedQuestItems) {
        if (gfuncs::IsFormValid(ref)) {
            if (gfuncs::IsQuestObject(ref)) {
                std::string refName = ref->GetDisplayFullName();
                std::string newName = AddQuestItemTagToString(refName);
                if (newName != refName) {
                    ref->SetDisplayName(newName, true);
                }
                questItems.push_back(ref);
            }
        }
    }

    for (RE::TESObjectREFR* ref : playerQuestItems) {
        if (gfuncs::GetIndexInVector(questItems, ref) == -1) {
            std::string refName = ref->GetDisplayFullName();
            std::string newName = AddQuestItemTagToString(refName);
            if (newName != refName) {
                ref->SetDisplayName(newName, true);
            }
            questItems.push_back(ref);
        }
    }
    recentItemsMutex.unlock();
}

void LoadCallback(SKSE::SerializationInterface* ssi) {
    if (ssi) {
        if (!isSerializing) {
            isSerializing = true;
            std::vector<RE::TESForm*> loadedRecentItems;
            std::vector<RE::TESObjectREFR*> loadedQuestItems;
            std::uint32_t type, version, length;

            while (ssi->GetNextRecordInfo(type, version, length)) {
                if (type == recentItemsRecord) {
                    loadedRecentItems = serialize::LoadFormVector(recentItemsRecord, ssi);
                }
                else if (type == questItemsRecord) {
                    loadedQuestItems = serialize::LoadObjectRefVector(questItemsRecord, ssi);
                }
            }

            LoadRecentItems(ssi, loadedRecentItems, loadedQuestItems);
            isSerializing = false;

            logger::trace("LoadCallback complete");
        }
        else {
            logger::debug("{}: already loading or saving, aborting load.", __func__);
        }
    }
    else {
        logger::error("{}: ssi doesn't exist, aborting load.", __func__);
    }
}

void SaveCallback(SKSE::SerializationInterface* ssi) {
    logger::trace("{}", __func__);
    if (ssi) {
        if (!isSerializing) {
            isSerializing = true;

            recentItemsMutex.lock();
            serialize::SaveFormVector(recentItems, recentItemsRecord, ssi);
            recentItemsMutex.unlock();

            questItemsMutex.lock();
            serialize::SaveObjectRefVector(questItems, questItemsRecord, ssi);
            questItemsMutex.unlock();

            isSerializing = false;
        }
        else {
            logger::debug("{}: already loading or saving, aborting load.", __func__);
        }
    }
    else {
        logger::error("{}: ssi doesn't exist, aborting load.", __func__);
    }

}

void MessageListener(SKSE::MessagingInterface::Message* message) {
    //logger::trace("{}", __func__);

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
        logger::trace("kDataLoaded: sent after the data handler has loaded all its forms");
        InstallQOLItemTags();
        break;

        //default: //
            //    logger::info("Unknown system message of type: {}", message->type);
            //    break;
    }
}

//init================================================================================================================================================================
SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    logger::trace("{}", __func__);
    SKSE::Init(skse);

    SetupLog("Data/SKSE/Plugins/QOLItemTags.ini");
    SKSE::GetMessagingInterface()->RegisterListener(MessageListener);

    auto* serialization = SKSE::GetSerializationInterface();
    serialization->SetUniqueID('QOLI');
    serialization->SetSaveCallback(SaveCallback);
    serialization->SetLoadCallback(LoadCallback);

    return true;
}
