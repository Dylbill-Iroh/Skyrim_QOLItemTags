#include "UIGfx.h"
#include "GeneralFunctions.h"

namespace logger = SKSE::log;

namespace gfx {
    std::string GetGFxTypeString(int type) {
        std::string sType = std::to_string(type);
        switch (type) {
            case 0: { return "kUndefined"; }
            case 1: { return "Null"; }
            case 2: { return "Bool"; }
            case 3: { return "Number"; }
            case 4: { return "String"; }
            case 5: { return "StringW"; }
            case 6: { return "Object"; }
            case 7: { return "Array"; }
            case 8: { return "DisplayObject"; }
        }
        return ("unrecognize type [" + std::to_string(type) + "]");
    }

    std::string GetItemListPathForItemMenu(std::string_view menuName) {
        if (menuName == RE::CraftingMenu::MENU_NAME) {
            return "_root.Menu.InventoryLists.panelContainer.itemList";
        }
        else if (menuName == RE::FavoritesMenu::MENU_NAME) {
            return "_root.MenuHolder.Menu_mc.itemList";
        }
        else {
           //for these menus :
           //"inventorymenu"
           //"bartermenu"
           //"containermenu"
           //"giftmenu"
           //"magicmenu"
            return "_root.Menu_mc.inventoryLists.itemList";
        }
    }

    void InvokeInt(std::string_view menuPath, std::string target, int arg) {
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        auto* args = RE::MakeFunctionArguments((std::string)menuPath, (std::string)target, (int)arg);
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> result;
        result.reset();
        vm->DispatchStaticCall("UI", "InvokeInt", args, result);
    }

    void SetItemMenuSelection(std::string_view menuPath, int index) {
        //std::string path = (gfx::GetItemListPathForItemMenu(menuPath) + ".ItemsListEntry" + std::to_string(topDownIndex) + ".this._parent.onItemRollOver");
        std::string path = (gfx::GetItemListPathForItemMenu(menuPath) + ".onItemRollOver");
        
        InvokeInt(menuPath, path, index);
    }

    std::string GetGfxValueAsString(RE::GFxValue& gfxValue) {
        std::string returnString;
        if (gfxValue.IsString()) {
            returnString = gfxValue.GetString();
        }
        else if (gfxValue.IsStringW()) {
            const wchar_t* txt = gfxValue.GetStringW();
            std::wstring ws(txt);
            std::string str(ws.begin(), ws.end());
            returnString = str;
        }
        else if (gfxValue.IsNumber()) {
            returnString = std::to_string(gfxValue.GetNumber());
        }
        else if (gfxValue.IsBool()) {
            returnString = std::to_string(gfxValue.GetBool());
        }
        else if (gfxValue.IsObject()) {
            returnString = "object";
        }
        else if (gfxValue.IsDisplayObject()) {
            returnString = "display object";
        }
        else if (gfxValue.IsArray()) {
            returnString = "array";
        }
        return returnString;
    }

    std::string GetGfxValueAsString(const RE::GFxValue& gfxValue) {
        std::string returnString = "";
        if (gfxValue.IsString()) {
            returnString = gfxValue.GetString();
        }
        else if (gfxValue.IsStringW()) {
            const wchar_t* txt = gfxValue.GetStringW();
            std::wstring ws(txt);
            std::string str(ws.begin(), ws.end());
            returnString = str;
        }
        else if (gfxValue.IsNumber()) {
            returnString = std::to_string(gfxValue.GetNumber());
        }
        else if (gfxValue.IsBool()) {
            returnString = std::to_string(gfxValue.GetBool());
        }
        else if (gfxValue.IsObject()) {
            returnString = "object";
        }
        else if (gfxValue.IsDisplayObject()) {
            returnString = "display object";
        }
        else if (gfxValue.IsArray()) {
            returnString = "array";
        }
        return returnString;
    }

    bool IsGfxMemberValid(const RE::GFxValue& gfxValue, std::string gfxName) {
        if (gfxValue == NULL) {
            //logger::error("gfxValue [{}] is NULL", gfxName);
            return false;
        }
        else if (gfxValue.IsNull()) {
            //logger::error("gfxValue [{}] is IsNull", gfxName);
            return false;
        }
        else if (gfxValue.IsUndefined()) {
            //logger::error("gfxValue [{}] is IsUndefined", gfxName);
            return false;
        }
        return true;
    }

    bool IsGfxMemberValid(RE::GFxValue& gfxValue, std::string gfxName) {
        if (gfxValue == NULL) {
            //logger::error("gfxValue [{}] is NULL", gfxName);
            return false;
        }
        else if (gfxValue.IsNull()) {
            //logger::error("gfxValue [{}] is IsNull", gfxName);
            return false;
        }
        else if (gfxValue.IsUndefined()) {
            //logger::error("gfxValue [{}] is IsUndefined", gfxName);
            return false;
        }
        return true;
    }

    bool GFxMemberNameIsValid(std::string name) {
        if (name.find("constraints") != std::string::npos) {
            return false;
        }
        if (name.find("scope") != std::string::npos) {
            return false;
        }
        if (name.find("track") != std::string::npos) {
            return false;
        }
        if (name.find("focusTarget") != std::string::npos) {
            return false;
        }

        if (gfuncs::StringHasNCharacters(name, '.', 10)) {
            return false;
        }

        std::string::difference_type n = std::count(name.begin(), name.end(), '.');

        // optional: convert to int
        int count = static_cast<int>(n);

        return true;
    }

    void LogGFxMembers(const RE::GFxValue& gfxValue, std::string gfxName) {
        if (!IsGfxMemberValid(gfxValue, gfxName)) {
            return;
        }

        std::string gfxValueString = GetGfxValueAsString(gfxValue);
        int gfxtype = static_cast<int>(gfxValue.GetType());
        logger::info("logging gfxValue: type[{}] valueString[{}] gfxValue[{}] ", GetGFxTypeString(gfxtype), gfxValueString, gfxName);

        //type not bool, number, string or stringW
        if (gfxtype < 2 || gfxtype > 5) {
            gfxValue.VisitMembers([&]([[maybe_unused]] const char* name, const RE::GFxValue& value) {
                //gfxValue.VisitMembers([&](const char* name, const RE::GFxValue& value) {
                int type = -1;
                std::string valueString = "null";
                std::string memberName = (gfxName + "." + name);

                if (IsGfxMemberValid(value, memberName)) {
                    type = static_cast<int>(value.GetType());
                    valueString = GetGfxValueAsString(value);
                }
                logger::info("type[{}] value[{}] member[{}] ", GetGFxTypeString(type), valueString, memberName);
            });
        } 
    }

    void LogGFxMembers(RE::GPtr<RE::GFxMovieView> mv, std::vector<std::string> memberStrings) {
        if (mv) {
            auto size = memberStrings.size();
            for (int i = 0; i < size; i++) {
                RE::GFxValue gfxValue;
                std::string name = memberStrings[i];
                if (mv->GetVariable(&gfxValue, name.c_str())) {
                    LogGFxMembers(gfxValue, name);
                }
                else {
                    logger::error("failed to get gfxValue for [{}]", name);
                }
                logger::info("\n\n");
            }
        }
    }

    void EraseQuantityStringFromUIitemName(std::string& uiItemName) {
        if (uiItemName != "") {
            int iStart = uiItemName.find(" (");
            if (iStart != std::string::npos) {
                int iEnd = uiItemName.find(")", iStart);
                if (iEnd != std::string::npos) {
                    int subStringStart = (iStart + 2);
                    std::string subString = uiItemName.substr(subStringStart, (iEnd - subStringStart));
                    if (gfuncs::IsDecString(subString)) {
                        uiItemName.erase(iStart, (iEnd - iStart + 1));
                    }
                }
            }
        }
    }

    //if the GFxValue has a member named "text" get its string value
    std::string GetGFxListEntryText(RE::GFxValue& listEntry) {
        std::string text = "";
        if (!IsGfxMemberValid(listEntry)) {
            return text;
        }

        int gfxtype = static_cast<int>(listEntry.GetType());
        std::string sText = "text";
        //const char* cText = sText.c_str();
        //type not bool, number, string or stringW
        if (gfxtype < 2 || gfxtype > 5) {
            listEntry.VisitMembers([&]([[maybe_unused]] const char* name, const RE::GFxValue& value) {
                //gfxValue.VisitMembers([&](const char* name, const RE::GFxValue& value) {

                std::string sName(name);
                if (sName == sText) {
                    if (value != NULL) {
                        if (value.IsString()) {
                            text = value.GetString();
                            logger::info("name[{}] text[{}]", name, text);
                        }
                    }
                }
            });
        }
        return text;
    } 

    int GetEntryDataArrayLength(std::string_view menuName, RE::GPtr<RE::GFxMovieView>mv) {
        std::string path = GetItemListPathForItemMenu(menuName) + ".listEnumeration._entryData.length";
        if (mv) {
            RE::GFxValue gfxArrayLength;
            if (mv->GetVariable(&gfxArrayLength, path.c_str())) {
                if (gfxArrayLength.IsNumber()) {
                    return  int(gfxArrayLength.GetNumber());
                }
            }
        }
        return 0;
    } 

    int GetEntryDataArrayLength(std::string_view menuName) {
        auto* ui = RE::UI::GetSingleton();
        if (ui) {
            auto mv = ui->GetMovieView(menuName);
            return GetEntryDataArrayLength(menuName, mv);
        }
        return 0;
    }

    int GetIndexForMenuItem(std::string_view menuName, std::string sItemName) {
        std::string path = GetItemListPathForItemMenu(menuName);
        //logger::trace("called");

        //sUIitemListEntryPath will be something like "_root.Menu_mc.inventoryLists.itemList.ItemsListEntry"
        //std::string sUIitemListEntryPath = path + ".listEnumeration._entryData.";
        std::string sUIitemListEntryPath = path + ".listEnumeration._entryData.";

        auto* ui = RE::UI::GetSingleton();
        if (ui) {
            auto mv = ui->GetMovieView(menuName);
            if (mv) {
                
                int i = 0;
                int iLimit = GetEntryDataArrayLength(menuName, mv);
                //std::string lString = std::format(".listEnumeration._entryData.length = {}", iLimit);
                //logger::trace("{}", lString);
                //RE::DebugNotification(lString.c_str());
                
                while (i < iLimit) {
                    std::string entryPath = (sUIitemListEntryPath + (std::to_string(i)));
                    //RE::GFxValue entrygfx;
                    std::string textPath = entryPath + ".text";
                    RE::GFxValue textgfx;
                    std::string indexPath = entryPath + ".itemIndex";
                    RE::GFxValue indexgfx;
                    std::string uiItemName = "not found";
                    int itemIndexValue = -1;

                    //if (mv->GetVariable(&entrygfx, entryPath.c_str())) {
                    //    //LogGFxMembers(entrygfx, entryPath);
                    //    uiItemName = GetGFxListEntryText(entrygfx);
                    //}

                    if (mv->GetVariable(&indexgfx, indexPath.c_str())) {
                        if (indexgfx.IsNumber()) {
                            itemIndexValue = indexgfx.GetNumber();
                        }
                    }

                    if (mv->GetVariable(&textgfx, textPath.c_str())) {
                        if (textgfx != NULL) {
                            if (textgfx.IsString()) {
                                uiItemName = textgfx.GetString();
                                if (uiItemName == sItemName) {
                                    if (itemIndexValue > -1) {
                                        //logger::trace("uiItemName[{}], itemIndex[{}] i[{}]", uiItemName, itemIndexValue, i);
                                        return itemIndexValue;
                                    }
                                }
                            }
                        }
                    }

                    //logger::trace("uiItemName[{}], itemIndex[{}] i[{}]", uiItemName, itemIndexValue, i);
                    i++;
                }
            }
        }
        return -1;
    }

    int GetItemMenuActiveSegment(std::string_view menuName) {
        //for these menus :
        //"inventorymenu"
        //"bartermenu"
        //"containermenu"
        //"giftmenu"
        //"magicmenu"

        int iReturn = -1;
        auto* ui = RE::UI::GetSingleton();
        if (ui) {
            auto mv = ui->GetMovieView(menuName);
            if (mv) {
                std::string selectedItemIndexPath = GetItemListPathForItemMenu(menuName) + ".selectedEntry.itemIndex";
                //std::string selectedItemIndexPath = GetItemListPathForItemMenu(menuName) + ".selectedIndex";
                RE::GFxValue gfxValue;
                if (mv->GetVariable(&gfxValue, "_root.Menu_mc.inventoryLists.categoryList._activeSegment")) {
                    if (gfxValue.IsNumber()) {
                        iReturn = gfxValue.GetNumber();
                    }
                }
            }
        }
        return iReturn;
    }

    int GetSelectedEntryIndex(std::string_view menuName) {
        int iReturn = -1;
        auto* ui = RE::UI::GetSingleton();
        if (ui) {
            auto mv = ui->GetMovieView(menuName);
            if (mv) {
                std::string selectedItemIndexPath = GetItemListPathForItemMenu(menuName) + ".selectedEntry.itemIndex";
                //std::string selectedItemIndexPath = GetItemListPathForItemMenu(menuName) + ".selectedIndex";
                RE::GFxValue gfxValue;
                if (mv->GetVariable(&gfxValue, selectedItemIndexPath.c_str())) {
                    if (gfxValue.IsNumber()) {
                        iReturn = gfxValue.GetNumber();
                    }
                }
            }
        } 
        return iReturn;
    }

    std::string GetSelectedEntryText(std::string_view menuName) {
        std::string sReturn = "";
        auto* ui = RE::UI::GetSingleton();
        if (ui) {
            auto mv = ui->GetMovieView(menuName);
            if (mv) {
                std::string selectedItemIndexPath = GetItemListPathForItemMenu(menuName) + ".selectedEntry.text";
                RE::GFxValue gfxValue;
                if (mv->GetVariable(&gfxValue, selectedItemIndexPath.c_str())) {
                    if (gfxValue.IsString()) {
                        sReturn = gfxValue.GetString();
                    }
                }
            }
        }
        return sReturn;
    }

    RE::GFxValue GetSelectedEntry(std::string_view menuName) {
        RE::GFxValue selectedEntryGfx;

        auto* ui = RE::UI::GetSingleton();
        if (ui) {
            auto mv = ui->GetMovieView(menuName);
            if (mv) {
                std::string selectedItemIndexPath = GetItemListPathForItemMenu(menuName) + ".selectedEntry";
                if (mv->GetVariable(&selectedEntryGfx, selectedItemIndexPath.c_str())) {
                    //logger::trace("selected entry found");
                }
            }
        }
        return selectedEntryGfx;
    }
}